// SPDX-License-Identifier: BSD-3-Clause
// Copyright 2026 Pitriss
// Per-user KDE notification watcher and best-effort lighting-sequence player.
#include "config.h"
#include "session_policy.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusServiceWatcher>
#include <QElapsedTimer>
#include <QFileSystemWatcher>
#include <QFileInfo>
#include <QDir>
#include <QHash>
#include <QJsonObject>
#include <QProcess>
#include <QQueue>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringList>
#include <QTextStream>
#include <QTimer>
#include <QDebug>

using KEncoderNotify::Configuration;

namespace {
constexpr const char *kNotifyService = "org.kencoder.Notify1";
constexpr const char *kNotifyObject = "/org/kencoder/Notify1";
constexpr const char *kPlasmaService = "org.freedesktop.Notifications";
constexpr const char *kPlasmaObject = "/org/freedesktop/Notifications";
constexpr const char *kPlasmaInterface = "org.kde.NotificationManager";
constexpr int kMaxPendingExecutions = 16; // Execution backpressure, NOT a preset/rule limit.
// Refuse forwarding to an old daemon whose player can omit Default restoration.
constexpr uint kPlaybackProtocolVersion = 2;

QStringList commandArguments(const QJsonObject &step)
{
    const QString type = step.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("off"))
        return {QStringLiteral("-o")};
    QStringList result{QStringLiteral("-b"),
                       QString::number(step.value(QStringLiteral("brightness")).toInt())};
    if (type == QLatin1String("static"))
        return result << QStringLiteral("-s");
    const int speed = step.value(QStringLiteral("speed")).toInt();
    const QString speedName = speed == 1 ? QStringLiteral("slow") :
                              speed == 3 ? QStringLiteral("fast") : QStringLiteral("medium");
    result << QStringLiteral("-a") << type << QStringLiteral("-v") << speedName;
    return result;
}

bool ruleMatches(const QJsonObject &rule, const QString &app,
                 const QString &title, const QString &body)
{
    if (!rule.value(QStringLiteral("enabled")).toBool()) return false;
    const QJsonObject match = rule.value(QStringLiteral("match")).toObject();
    const auto contains = [](const QString &haystack, const QJsonValue &needle) {
        return needle.toString().isEmpty() ||
               haystack.contains(needle.toString(), Qt::CaseInsensitive);
    };
    return contains(app, match.value(QStringLiteral("app_contains"))) &&
           contains(title, match.value(QStringLiteral("title_contains"))) &&
           contains(title + QLatin1Char('\n') + body,
                    match.value(QStringLiteral("text_contains")));
}

QJsonObject offStep()
{
    return {{QStringLiteral("type"), QStringLiteral("off")},
            {QStringLiteral("duration_ms"), 50}};
}

QJsonObject defaultStep(const Configuration &config)
{
    const QJsonArray steps = KEncoderNotify::presetById(
        config, QString::fromLatin1(KEncoderNotify::kDefaultPresetId))
        .value(QStringLiteral("steps")).toArray();
    return steps.isEmpty() ? QJsonObject{} : steps.first().toObject();
}

class Player final : public QObject
{
    Q_OBJECT
public:
    explicit Player(QObject *parent = nullptr) : QObject(parent)
    {
        hold_.setSingleShot(true);
        timeout_.setSingleShot(true);
        connect(&hold_, &QTimer::timeout, this, [this] {
            ++stepIndex_;
            advance();
        });
        connect(&timeout_, &QTimer::timeout, this, [this] {
            timedOut_ = true;
            process_.kill();
        });
        connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
            if (active_ && error == QProcess::FailedToStart)
                finish(false); // finish() preserves a pending lock/DPMS override.
        });
        connect(&process_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this](int code, QProcess::ExitStatus status) {
            if (!active_)
                return;
            timeout_.stop();
            if (overridePending_) {
                active_ = false;
                hold_.stop();
                restoring_ = false;
                QTimer::singleShot(0, this, [this] { startIfIdle(); });
                return;
            }
            if (timedOut_ || status != QProcess::NormalExit || code != 0 ||
                process_.readAllStandardOutput().contains("Command failed")) {
                finish(false);
                return;
            }
            if (restoring_) {
                // Default is a state, not a timed sequence.
                ++stepIndex_;
                advance();
            } else {
                hold_.start(currentSteps_.at(stepIndex_).toObject().value(QStringLiteral("duration_ms")).toInt());
            }
        });
    }

    void setRestoreStep(const QJsonObject &step) { restoreStep_ = step; }

    bool enqueue(const QJsonObject &preset)
    {
        if (preset.isEmpty() || queued_.size() >= kMaxPendingExecutions)
            return false;
        queued_.enqueue(preset);
        QTimer::singleShot(0, this, [this] { startIfIdle(); });
        return true;
    }

    bool idle() const { return !active_ && queued_.isEmpty(); }
    void clearQueued() { queued_.clear(); }

    // A lock/DPMS transition supersedes the remaining animation and its restore.
    // Let any CLI already writing to HID finish before dispatching the new state.
    void forceState(const QJsonObject &step)
    {
        if (step.isEmpty()) return;
        queued_.clear();
        restorePending_ = false;
        sequenceFailed_ = false;
        overrideStep_ = step;
        overridePending_ = true;
        if (!active_ || process_.state() == QProcess::NotRunning) {
            active_ = false;
            restoring_ = false;
            hold_.stop();
            timeout_.stop();
            QTimer::singleShot(0, this, [this] { startIfIdle(); });
        }
    }

Q_SIGNALS:
    void executionEnded(bool success);
    void allIdle();

private:
    void startIfIdle()
    {
        if (active_) return;
        if (overridePending_) {
            overridePending_ = false;
            restoring_ = true;
            restorePending_ = false;
            currentSteps_ = QJsonArray{overrideStep_};
            stepIndex_ = 0;
            active_ = true;
            advance();
            return;
        }
        // Finish EVERY preset by restoring Default, even if another preset is queued.
        // Never recursively restore Default after Default itself.
        if (restorePending_ && !restoreStep_.isEmpty()) {
            restorePending_ = false;
            restoring_ = true;
            currentSteps_ = QJsonArray{restoreStep_};
            stepIndex_ = 0;
            active_ = true;
            advance();
            return;
        }
        if (queued_.isEmpty()) {
            Q_EMIT allIdle();
            return;
        }
        const QJsonObject preset = queued_.dequeue();
        const bool isDefault = preset.value(QStringLiteral("id")).toString() ==
                               QLatin1String(KEncoderNotify::kDefaultPresetId);
        sequenceFailed_ = false;
        restoring_ = isDefault;
        restorePending_ = !isDefault && !restoreStep_.isEmpty();
        const QJsonArray steps = preset.value(QStringLiteral("steps")).toArray();
        currentSteps_ = isDefault ? QJsonArray{steps.first()} : steps;
        stepIndex_ = 0;
        active_ = true;
        advance();
    }

    void advance()
    {
        if (!active_)
            return;
        if (stepIndex_ >= currentSteps_.size()) {
            finish(true);
            return;
        }
        const QJsonObject step = currentSteps_.at(stepIndex_).toObject();
        if (step.value(QStringLiteral("type")).toString() == QLatin1String("wait")) {
            hold_.start(step.value(QStringLiteral("duration_ms")).toInt());
            return;
        }
        const QString cli = QStandardPaths::findExecutable(QStringLiteral("encoder"));
        if (cli.isEmpty()) {
            qWarning() << "kencoder-notify: encoder CLI is unavailable";
            finish(false);
            return;
        }
        timedOut_ = false;
        timeout_.start(8000);
        process_.start(cli, commandArguments(step)); // No shell, no hardware readback.
    }

    void finish(bool success)
    {
        if (!active_)
            return;
        const bool wasRestoring = restoring_;
        active_ = false;
        hold_.stop();
        timeout_.stop();
        if (overridePending_) {
            restoring_ = false;
            QTimer::singleShot(0, this, [this] { startIfIdle(); });
            return;
        }
        if (!success) {
            sequenceFailed_ = true;
            queued_.clear(); // Stop following effects, not the one-shot Default restore.
            if (wasRestoring) {
                restorePending_ = false; // Never retry a failed Default command.
                qCritical() << "kencoder-notify: Default restoration failed; device state is unknown";
            } else if (restorePending_ && !restoreStep_.isEmpty()) {
                qWarning() << "kencoder-notify: effect failed; attempting Default restoration once";
            } else {
                qCritical() << "kencoder-notify: effect failed; no Default restoration available";
            }
        }
        restoring_ = false;
        // Report completion only after the restore, so --play cannot hide errors.
        if (wasRestoring || !restorePending_) {
            Q_EMIT executionEnded(success && !sequenceFailed_);
            sequenceFailed_ = false;
        }
        QTimer::singleShot(0, this, [this] { startIfIdle(); });
    }

    QProcess process_;
    QTimer hold_;
    QTimer timeout_;
    QQueue<QJsonObject> queued_;
    QJsonArray currentSteps_;
    QJsonObject restoreStep_;
    QJsonObject overrideStep_;
    bool overridePending_ = false;
    int stepIndex_ = 0;
    bool active_ = false;
    bool timedOut_ = false;
    bool restoring_ = false;
    bool restorePending_ = false;
    bool sequenceFailed_ = false;
};

class Daemon;

class WatcherObject final : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.NotificationWatcher")
public:
    explicit WatcherObject(Daemon *owner);
public Q_SLOTS:
    Q_SCRIPTABLE void Notify(uint id, const QString &app_name, uint replaces_id,
                             const QString &app_icon, const QString &summary,
                             const QString &body, const QStringList &actions,
                             const QVariantMap &hints, int timeout);
    Q_SCRIPTABLE void CloseNotification(uint id) { Q_UNUSED(id); }
private:
    Daemon *owner_;
};

class ControlObject final : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kencoder.Notify1")
public:
    explicit ControlObject(Daemon *owner);
public Q_SLOTS:
    Q_SCRIPTABLE bool Reload();
    Q_SCRIPTABLE bool PlayPreset(const QString &id);
    Q_SCRIPTABLE uint PlaybackProtocolVersion();
    Q_SCRIPTABLE uint SessionPolicyProtocolVersion();
    Q_SCRIPTABLE QString ListenerStatus();
    Q_SCRIPTABLE uint MuteProtocolVersion();
    Q_SCRIPTABLE bool NotificationsMuted();
private:
    Daemon *owner_;
};

class Daemon final : public QObject
{
    Q_OBJECT
public:
    explicit Daemon(QObject *parent = nullptr)
        : QObject(parent), watcherObject_(this), controlObject_(this),
          plasmaWatcher_(QString::fromLatin1(kPlasmaService),
                         QDBusConnection::sessionBus(),
                         QDBusServiceWatcher::WatchForOwnerChange, this),
          screenSaverWatcher_(QStringLiteral("org.freedesktop.ScreenSaver"),
                         QDBusConnection::sessionBus(),
                         QDBusServiceWatcher::WatchForOwnerChange, this)
    {
        clock_.start();
        reloadTimer_.setSingleShot(true);
        reloadTimer_.setInterval(150);
        registrationRetry_.setInterval(15000);
        dpmsTimer_.setInterval(3000);
        QDBusConnection::sessionBus().connect(
            QStringLiteral("org.freedesktop.ScreenSaver"),
            QStringLiteral("/ScreenSaver"), QStringLiteral("org.freedesktop.ScreenSaver"),
            QStringLiteral("ActiveChanged"), this, SLOT(onLockChanged(bool)));
        connect(&screenSaverWatcher_, &QDBusServiceWatcher::serviceOwnerChanged, this,
                [this](const QString &, const QString &, const QString &) {
            refreshLockState();
            updateSessionGuard();
        });
        connect(&dpmsTimer_, &QTimer::timeout, this, [this] { checkDpms(); });
        connect(&dpmsCheck_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this](int code, QProcess::ExitStatus status) {
            if (code != 0 || status != QProcess::NormalExit) return;
            const QString output = QString::fromLocal8Bit(dpmsCheck_.readAllStandardOutput());
            bool parsed = false;
            bool off = false;
            if (qEnvironmentVariable("XDG_SESSION_TYPE") == QLatin1String("wayland")) {
                const QRegularExpression pattern(
                    QStringLiteral("dpms mode for screen [^\n]+: (on|off)"),
                    QRegularExpression::CaseInsensitiveOption);
                auto it = pattern.globalMatch(output);
                parsed = it.hasNext();
                off = parsed;
                while (it.hasNext())
                    if (it.next().captured(1).compare(QLatin1String("off"), Qt::CaseInsensitive) != 0)
                        off = false; // Only blank when ALL screens are off.
            } else {
                parsed = output.contains(QStringLiteral("Monitor is On"), Qt::CaseInsensitive) ||
                         output.contains(QStringLiteral("Monitor is Off"), Qt::CaseInsensitive);
                off = output.contains(QStringLiteral("Monitor is Off"), Qt::CaseInsensitive);
            }
            if (parsed && off != screenOff_) {
                screenOff_ = off;
                updateSessionGuard();
            }
        });
        connect(&reloadTimer_, &QTimer::timeout, this, [this] { reload(); });
        connect(&registrationRetry_, &QTimer::timeout, this, [this] {
            if (config_.enabled && !registered_) registerWatcher();
        });
        connect(&fileWatcher_, &QFileSystemWatcher::directoryChanged, this,
                [this](const QString &) { reloadTimer_.start(); });
        connect(&fileWatcher_, &QFileSystemWatcher::fileChanged, this,
                [this](const QString &) { reloadTimer_.start(); });
        connect(&plasmaWatcher_, &QDBusServiceWatcher::serviceOwnerChanged, this,
                [this](const QString &, const QString &, const QString &newOwner) {
            registered_ = false;
            if (!newOwner.isEmpty() && config_.enabled)
                registerWatcher();
        });
    }

    bool start()
    {
        auto bus = QDBusConnection::sessionBus();
        if (!bus.isConnected() || !bus.registerService(QString::fromLatin1(kNotifyService))) {
            qCritical() << "kencoder-notify: another listener is running, or user D-Bus is unavailable";
            return false;
        }
        if (!bus.registerObject(QStringLiteral("/NotificationWatcher"), &watcherObject_,
                                QDBusConnection::ExportScriptableSlots) ||
            !bus.registerObject(QString::fromLatin1(kNotifyObject), &controlObject_,
                                QDBusConnection::ExportScriptableSlots)) {
            qCritical() << "kencoder-notify: cannot expose D-Bus listener objects";
            bus.unregisterService(QString::fromLatin1(kNotifyService));
            return false;
        }
        refreshLockState();
        const bool ok = reload();
        refreshWatchPaths();
        return ok;
    }

    ~Daemon() override
    {
        unregisterWatcher();
    }

    bool reload()
    {
        Configuration fresh;
        QString error;
        if (!KEncoderNotify::load(&fresh, &error)) {
            // Fail closed if either JSON file is malformed; never keep stale rules active.
            registrationRetry_.stop();
            unregisterWatcher();
            player_.clearQueued();
            config_ = Configuration{};
            // An active sequence still needs its last validated Default state.
            // Do not erase the recovery command during a malformed-file reload.
            qWarning().noquote() << "kencoder-notify: configuration invalid:" << error;
            refreshWatchPaths();
            return false;
        }
        KEncoderSession::Policy policy;
        if (!KEncoderSession::loadPolicy(&policy, &error)) {
            qWarning().noquote() << "kencoder-notify: invalid session policy:" << error;
            registrationRetry_.stop();
            unregisterWatcher();
            player_.clearQueued();
            config_.enabled = false;
            return false;
        }
        // A damaged mute file must fail closed for notification reactions.
        bool requestedMute = false;
        QString muteError;
        if (!KEncoderNotify::loadMute(&requestedMute, &muteError)) {
            qWarning().noquote() << "kencoder-notify:" << muteError;
            requestedMute = true;
        }
        const bool newlyMuted = requestedMute && !muted_;
        muted_ = requestedMute;
        config_ = fresh;
        policy_ = policy;
        updateDpmsTimer();
        updateSessionGuard();
        if (newlyMuted && !player_.idle())
            player_.forceState(guarded_ ? offStep() : defaultStep(config_));
        lastTriggered_.clear();
        if (!config_.enabled) {
            registrationRetry_.stop();
            unregisterWatcher();
            player_.clearQueued();
        } else {
            registrationRetry_.start();
            if (!registered_) registerWatcher();
        }
        refreshWatchPaths();
        return true;
    }

    QString listenerStatus() const
    {
        if (!config_.enabled) return QStringLiteral("disabled");
        return registered_ ? QStringLiteral("active") : QStringLiteral("unavailable");
    }
    bool notificationsMuted() const { return muted_; }

    bool play(const QString &id)
    {
        const auto preset = KEncoderNotify::presetById(config_, id);
        return player_.enqueue(preset);
    }

    bool playManual(const QString &id)
    {
        // --play forwarded over D-Bus can arrive before QFileSystemWatcher reloads.
        // Read the saved preset and Default fresh without resetting notification cooldowns.
        Configuration latest;
        QString error;
        if (!KEncoderNotify::load(&latest, &error)) {
            qWarning().noquote() << "kencoder-notify: invalid manual playback configuration:" << error;
            return false;
        }
        const auto preset = KEncoderNotify::presetById(latest, id);
        if (preset.isEmpty()) return false;
        player_.setRestoreStep(guarded_ ? offStep() : defaultStep(latest));
        return player_.enqueue(preset);
    }

    void onNotification(uint id, uint replacesId, const QString &app,
                        const QString &title, const QString &body)
    {
        Q_UNUSED(id);
        // Changes to an existing notification can be frequent. Trigger only for new items.
        if (replacesId != 0 || muted_ || !config_.enabled || !registered_ ||
            (!policy_.notifyWhenInactive && (locked_ || screenOff_)))
            return;
        for (const auto &entry : config_.rules) {
            const auto rule = entry.toObject();
            if (!ruleMatches(rule, app, title, body))
                continue;
            const QString id = rule.value(QStringLiteral("id")).toString();
            const qint64 now = clock_.elapsed();
            const int cooldown = rule.value(QStringLiteral("cooldown_ms")).toInt();
            if (lastTriggered_.contains(id) && now - lastTriggered_.value(id) < cooldown)
                continue;
            const QString presetId = rule.value(QStringLiteral("preset_id")).toString();
            // Ordered priority: the first eligible matching rule wins.
            if (play(presetId)) {
                lastTriggered_.insert(id, now);
                break;
            }
        }
    }

private Q_SLOTS:
    void onLockChanged(bool locked)
    {
        if (locked_ == locked) return;
        locked_ = locked;
        updateSessionGuard();
    }

private:
    void refreshLockState()
    {
        const QDBusMessage call = QDBusMessage::createMethodCall(
            QStringLiteral("org.freedesktop.ScreenSaver"), QStringLiteral("/ScreenSaver"),
            QStringLiteral("org.freedesktop.ScreenSaver"), QStringLiteral("GetActive"));
        const QDBusMessage reply = QDBusConnection::sessionBus().call(call, QDBus::Block, 700);
        if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty())
            locked_ = reply.arguments().first().toBool();
    }

    void updateDpmsTimer()
    {
        const bool needed = policy_.offOnScreenOff ||
                            (config_.enabled && !policy_.notifyWhenInactive);
        if (needed) {
            dpmsTimer_.start();
            checkDpms();
        } else {
            dpmsTimer_.stop();
            if (screenOff_) {
                screenOff_ = false;
            }
        }
    }

    void checkDpms()
    {
        if (dpmsCheck_.state() != QProcess::NotRunning) return;
        const QString type = qEnvironmentVariable("XDG_SESSION_TYPE");
        const QString executable = QStandardPaths::findExecutable(
            type == QLatin1String("wayland") ? QStringLiteral("kscreen-doctor")
                                             : QStringLiteral("xset"));
        if (executable.isEmpty()) return;
        dpmsCheck_.start(executable, type == QLatin1String("wayland")
            ? QStringList{QStringLiteral("--dpms"), QStringLiteral("show")}
            : QStringList{QStringLiteral("q")});
        QTimer::singleShot(2000, this, [this] {
            if (dpmsCheck_.state() != QProcess::NotRunning) dpmsCheck_.kill();
        });
    }

    void updateSessionGuard()
    {
        const bool off = (policy_.offOnLock && locked_) ||
                         (policy_.offOnScreenOff && screenOff_);
        player_.setRestoreStep(off ? offStep() : defaultStep(config_));
        if (off != guarded_) {
            guarded_ = off;
            player_.forceState(off ? offStep() : defaultStep(config_));
        }
    }

    void refreshWatchPaths()
    {
        const QString directory = KEncoderNotify::configDirectory();
        QDir().mkpath(directory);
        const QStringList wanted{directory, KEncoderNotify::presetsFile(),
                                 KEncoderNotify::rulesFile(), KEncoderSession::policyFile(),
                                 KEncoderNotify::muteFile()};
        for (const QString &path : fileWatcher_.files() + fileWatcher_.directories()) {
            if (!wanted.contains(path)) fileWatcher_.removePath(path);
        }
        for (const QString &path : wanted) {
            if (QFileInfo::exists(path) &&
                !fileWatcher_.files().contains(path) &&
                !fileWatcher_.directories().contains(path))
                fileWatcher_.addPath(path);
        }
    }

    bool registerWatcher()
    {
        if (registered_)
            return true;
        auto msg = QDBusMessage::createMethodCall(QString::fromLatin1(kPlasmaService),
                                                   QString::fromLatin1(kPlasmaObject),
                                                   QString::fromLatin1(kPlasmaInterface),
                                                   QStringLiteral("RegisterWatcher"));
        const auto reply = QDBusConnection::sessionBus().call(msg, QDBus::Block, 1500);
        registered_ = reply.type() == QDBusMessage::ReplyMessage;
        if (!registered_)
            qWarning() << "kencoder-notify: KDE watcher API unavailable; monitoring disabled";
        return registered_;
    }

    void unregisterWatcher()
    {
        if (!registered_) return;
        auto msg = QDBusMessage::createMethodCall(QString::fromLatin1(kPlasmaService),
                                                   QString::fromLatin1(kPlasmaObject),
                                                   QString::fromLatin1(kPlasmaInterface),
                                                   QStringLiteral("UnRegisterWatcher"));
        QDBusConnection::sessionBus().call(msg, QDBus::NoBlock);
        registered_ = false;
    }

    Configuration config_;
    KEncoderSession::Policy policy_;
    bool muted_ = false;
    Player player_;
    bool locked_ = false;
    bool screenOff_ = false;
    bool guarded_ = false;
    QTimer dpmsTimer_;
    QProcess dpmsCheck_;
    WatcherObject watcherObject_;
    ControlObject controlObject_;
    QFileSystemWatcher fileWatcher_;
    QDBusServiceWatcher plasmaWatcher_;
    QDBusServiceWatcher screenSaverWatcher_;
    QTimer reloadTimer_;
    QTimer registrationRetry_;
    QElapsedTimer clock_;
    QHash<QString, qint64> lastTriggered_;
    bool registered_ = false;
};

WatcherObject::WatcherObject(Daemon *owner) : QObject(owner), owner_(owner) {}
void WatcherObject::Notify(uint id, const QString &app_name, uint replaces_id,
                           const QString &app_icon, const QString &summary,
                           const QString &body, const QStringList &actions,
                           const QVariantMap &hints, int timeout)
{
    Q_UNUSED(app_icon);
    Q_UNUSED(actions);
    Q_UNUSED(hints);
    Q_UNUSED(timeout);
    owner_->onNotification(id, replaces_id, app_name, summary, body);
}
ControlObject::ControlObject(Daemon *owner) : QObject(owner), owner_(owner) {}
bool ControlObject::Reload() { return owner_->reload(); }
bool ControlObject::PlayPreset(const QString &id) { return owner_->playManual(id); }
uint ControlObject::PlaybackProtocolVersion() { return kPlaybackProtocolVersion; }
uint ControlObject::SessionPolicyProtocolVersion() { return 1; }
QString ControlObject::ListenerStatus() { return owner_->listenerStatus(); }
uint ControlObject::MuteProtocolVersion() { return 1; }
bool ControlObject::NotificationsMuted() { return owner_->notificationsMuted(); }

bool dryRun(const Configuration &config, const QString &id)
{
    const auto preset = KEncoderNotify::presetById(config, id);
    if (preset.isEmpty()) return false;
    QTextStream stream(stdout);
    stream << "DRY-RUN preset=" << id << '\n';
    const bool isDefault = id == QLatin1String(KEncoderNotify::kDefaultPresetId);
    const QJsonArray steps = preset.value(QStringLiteral("steps")).toArray();
    const QJsonArray displayed = isDefault ? QJsonArray{steps.first()} : steps;
    for (const auto &value : displayed) {
        const auto step = value.toObject();
        if (step.value(QStringLiteral("type")).toString() == QLatin1String("wait"))
            stream << "wait " << step.value(QStringLiteral("duration_ms")).toInt() << "ms\n";
        else if (isDefault)
            stream << "encoder " << commandArguments(step).join(QLatin1Char(' '))
                   << "; no hold (Default)\n";
        else
            stream << "encoder " << commandArguments(step).join(QLatin1Char(' '))
                   << "; hold " << step.value(QStringLiteral("duration_ms")).toInt() << "ms\n";
    }
    if (!isDefault) {
        const QJsonObject restore = defaultStep(config);
        if (!restore.isEmpty())
            stream << "restore-default: encoder "
                   << commandArguments(restore).join(QLatin1Char(' ')) << "; no hold\n";
    }
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("kencoder-notify"));
    const QStringList args = app.arguments();
    if (args.size() == 2 && args.at(1) == QLatin1String("--help")) {
        QTextStream(stdout) << "kencoder-notify --daemon | --check | --session-policy | --play ID [--dry-run] | --simulate APP TITLE BODY\n";
        return 0;
    }
    if (args.size() == 2 && args.at(1) == QLatin1String("--session-policy")) {
        KEncoderSession::Policy state;
        QString error;
        if (!KEncoderSession::loadPolicy(&state, &error)) {
            qCritical().noquote() << error;
            return 2;
        }
        QTextStream(stdout) << "Session policy: lock=" << state.offOnLock
                            << "; screen-off=" << state.offOnScreenOff
                            << "; notify-when-inactive=" << state.notifyWhenInactive << '\n';
        return 0;
    }
    Configuration config;
    QString error;
    if (!KEncoderNotify::load(&config, &error)) {
        qCritical().noquote() << error;
        return 2;
    }
    if (args.size() == 2 && args.at(1) == QLatin1String("--check")) {
        bool muted = false;
        if (!KEncoderNotify::loadMute(&muted, &error)) {
            qCritical().noquote() << error;
            return 2;
        }
        QTextStream(stdout) << "Configuration: OK; muted=" << muted
                            << "; enabled=" << config.enabled
                            << "; backend=" << config.backend
                            << "; presets=" << config.presets.size()
                            << "; rules=" << config.rules.size() << '\n';
        return 0;
    }
    if (args.size() == 5 && args.at(1) == QLatin1String("--simulate")) {
        // Pure rule matching; never starts the daemon or the CLI.
        QTextStream stream(stdout);
        int hits = 0;
        for (const auto &entry : config.rules) {
            const QJsonObject rule = entry.toObject();
            if (!ruleMatches(rule, args.at(2), args.at(3), args.at(4))) continue;
            stream << "MATCH " << rule.value(QStringLiteral("id")).toString() << '\n';
            ++hits;
            break; // --simulate follows the same first-match priority as the listener.
        }
        stream << "MATCHES " << hits << '\n';
        return 0;
    }
    if (args.size() == 2 && args.at(1) == QLatin1String("--daemon")) {
        Daemon daemon;
        if (!daemon.start()) return 3;
        return app.exec();
    }
    if ((args.size() == 3 || args.size() == 4) && args.at(1) == QLatin1String("--play")) {
        const QString id = args.at(2);
        if (KEncoderNotify::presetById(config, id).isEmpty()) {
            qCritical() << "Unknown preset ID";
            return 2;
        }
        if (args.size() == 4 && args.at(3) == QLatin1String("--dry-run"))
            return dryRun(config, id) ? 0 : 2;
        if (args.size() != 3) return 2;
        auto bus = QDBusConnection::sessionBus();
        if (bus.isConnected() && bus.interface()) {
            const QDBusReply<bool> present = bus.interface()->isServiceRegistered(QString::fromLatin1(kNotifyService));
            if (present.isValid() && present.value()) {
                const auto versionRequest = QDBusMessage::createMethodCall(
                    QString::fromLatin1(kNotifyService), QString::fromLatin1(kNotifyObject),
                    QString::fromLatin1(kNotifyService), QStringLiteral("PlaybackProtocolVersion"));
                const auto versionReply = bus.call(versionRequest, QDBus::Block, 1500);
                if (versionReply.type() != QDBusMessage::ReplyMessage ||
                    versionReply.arguments().isEmpty() ||
                    versionReply.arguments().first().toUInt() < kPlaybackProtocolVersion) {
                    qCritical() << "kencoder-notify: old/unresponsive daemon; restart it before playback";
                    return 3; // Never forward to a player that may skip Default.
                }
                auto msg = QDBusMessage::createMethodCall(QString::fromLatin1(kNotifyService),
                                                           QString::fromLatin1(kNotifyObject),
                                                           QString::fromLatin1(kNotifyService),
                                                           QStringLiteral("PlayPreset"));
                msg.setArguments({id});
                const QDBusMessage reply = bus.call(msg, QDBus::Block, 1500);
                return reply.type() == QDBusMessage::ReplyMessage &&
                       !reply.arguments().isEmpty() && reply.arguments().first().toBool() ? 0 : 3;
            }
        }
        Player player;
        player.setRestoreStep(defaultStep(config));
        int result = 3;
        QObject::connect(&player, &Player::executionEnded, &app, [&](bool success) {
            result = success ? 0 : 3;
        });
        QObject::connect(&player, &Player::allIdle, &app, &QCoreApplication::quit);
        player.enqueue(KEncoderNotify::presetById(config, id));
        app.exec();
        return result;
    }
    qCritical() << "Invalid arguments; use --help";
    return 2;
}

#include "main.moc"
