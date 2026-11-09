// SPDX-License-Identifier: BSD-3-Clause
// Copyright 2026 Pitriss
#include <QCoreApplication>
#include "../lighting_status.h"
#include "../translation_loader.h"
#include "../../notify/session_policy.h"
#include "../../notify/config.h"
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <KQuickConfigModule>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <KPluginFactory>
#include <QStandardPaths>

class KEncoderKcm final : public KQuickConfigModule
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString feedback READ feedback NOTIFY feedbackChanged)
    Q_PROPERTY(QString lightingStatus READ lightingStatus NOTIFY lightingStatusChanged)
    Q_PROPERTY(bool reportedAvailable READ reportedAvailable NOTIFY lightingStatusChanged)
    Q_PROPERTY(int reportedBrightness READ reportedBrightness NOTIFY lightingStatusChanged)
    Q_PROPERTY(QString reportedMode READ reportedMode NOTIFY lightingStatusChanged)
    Q_PROPERTY(QString encoderPath READ encoderPath CONSTANT)
    Q_PROPERTY(int keyboardReadiness READ keyboardReadiness NOTIFY readinessChanged)
    Q_PROPERTY(QString keyboardReadinessText READ keyboardReadinessText NOTIFY readinessChanged)
    Q_PROPERTY(int notifyReadiness READ notifyReadiness NOTIFY readinessChanged)
    Q_PROPERTY(QString notifyReadinessText READ notifyReadinessText NOTIFY readinessChanged)
    Q_PROPERTY(int listenerReadiness READ listenerReadiness NOTIFY readinessChanged)
    Q_PROPERTY(QString listenerReadinessText READ listenerReadinessText NOTIFY readinessChanged)
    Q_PROPERTY(bool offOnLock READ offOnLock NOTIFY sessionPolicyChanged)
    Q_PROPERTY(bool offOnScreenOff READ offOnScreenOff NOTIFY sessionPolicyChanged)
    Q_PROPERTY(bool notifyWhenInactive READ notifyWhenInactive NOTIFY sessionPolicyChanged)

public:
    KEncoderKcm(QObject *parent, const KPluginMetaData &metaData);

    bool busy() const { return busy_; }
    QString feedback() const { return feedback_; }
    QString lightingStatus() const { return lightingStatus_; }
    bool reportedAvailable() const { return reportedAvailable_; }
    int reportedBrightness() const { return reportedBrightness_; }
    QString reportedMode() const { return reportedMode_; }
    QString encoderPath() const { return QStandardPaths::findExecutable(QStringLiteral("encoder")); }
    int keyboardReadiness() const { return keyboardReadiness_; }
    QString keyboardReadinessText() const { return keyboardReadinessText_; }
    int notifyReadiness() const { return notifyReadiness_; }
    QString notifyReadinessText() const { return notifyReadinessText_; }
    int listenerReadiness() const { return listenerReadiness_; }
    QString listenerReadinessText() const { return listenerReadinessText_; }
    bool offOnLock() const { return sessionPolicy_.offOnLock; }
    bool offOnScreenOff() const { return sessionPolicy_.offOnScreenOff; }
    bool notifyWhenInactive() const { return sessionPolicy_.notifyWhenInactive; }

    Q_INVOKABLE void applyLighting(const QString &mode, int brightness, const QString &speed);
    Q_INVOKABLE void turnOff();
    Q_INVOKABLE void applyDefault();
    Q_INVOKABLE void refreshLightingStatus();
    Q_INVOKABLE bool saveSessionPolicy(bool offOnLock, bool offOnScreenOff,
                                       bool notifyWhenInactive);
    Q_INVOKABLE void launchGui();
    Q_INVOKABLE void refreshReadiness();

Q_SIGNALS:
    void busyChanged();
    void feedbackChanged();
    void lightingStatusChanged();
    void sessionPolicyChanged();
    void readinessChanged();

private:
    void execute(const QStringList &arguments, const QString &successMessage);
    void setBusy(bool busy);
    void setFeedback(const QString &feedback);
    void setLightingStatus(const QString &status);
    void setKeyboardReadiness(int level, const QString &message);
    void refreshServices();

    KEncoderSession::Policy sessionPolicy_;
    QTimer readinessTimer_;
    int keyboardReadiness_ = 1; // 0=red, 1=amber, 2=green
    int notifyReadiness_ = 1;
    int listenerReadiness_ = 1;
    QString keyboardReadinessText_ = QCoreApplication::translate("KEncoderKcm", "Zjišťuji stav klávesnice…");
    QString notifyReadinessText_ = QCoreApplication::translate("KEncoderKcm", "Zjišťuji dostupnost služby…");
    QString listenerReadinessText_ = QCoreApplication::translate("KEncoderKcm", "Zjišťuji registraci listeneru…");
    QProcess process_;
    QTimer timeout_;
    bool busy_ = false;
    bool timedOut_ = false;
    bool reading_ = false;
    bool reportedAvailable_ = false;
    int reportedBrightness_ = -1;
    QString reportedMode_;
    QString lightingStatus_ = QCoreApplication::translate("KEncoderKcm", "Stav podsvícení: nezjištěn.");
    QString feedback_ = QCoreApplication::translate("KEncoderKcm", "Změny se odesílají pouze tlačítky Nastavit a Vypnout.");
    QString successMessage_;
};

K_PLUGIN_CLASS_WITH_JSON(KEncoderKcm, "kcm_kencoder.json")

KEncoderKcm::KEncoderKcm(QObject *parent, const KPluginMetaData &metaData)
    : KQuickConfigModule(parent, metaData)
{
    KEncoderTranslation::install();
    // Initial property strings are constructed before the constructor body.
    keyboardReadinessText_ = QCoreApplication::translate("KEncoderKcm", "Zjišťuji stav klávesnice…");
    notifyReadinessText_ = QCoreApplication::translate("KEncoderKcm", "Zjišťuji dostupnost služby…");
    listenerReadinessText_ = QCoreApplication::translate("KEncoderKcm", "Zjišťuji registraci listeneru…");
    lightingStatus_ = QCoreApplication::translate("KEncoderKcm", "Stav podsvícení: nezjištěn.");
    feedback_ = QCoreApplication::translate("KEncoderKcm", "Změny se odesílají pouze tlačítky Nastavit a Vypnout.");
    // The only way to change the lighting is clicking the explicit buttons.
    // KCM's implicit Apply/Defaults actions must never write to the keyboard.
    setButtons(NoAdditionalButton);
    setNeedsSave(false);
    QString policyError;
    if (!KEncoderSession::loadPolicy(&sessionPolicy_, &policyError))
        setFeedback(QCoreApplication::translate("KEncoderKcm", "Nastavení relace: %1").arg(policyError));
    readinessTimer_.setInterval(10000);
    connect(&readinessTimer_, &QTimer::timeout, this, [this] {
        refreshServices();
        if (!busy_) refreshLightingStatus(); // Known read-only 80 01 query.
    });
    readinessTimer_.start();
    QTimer::singleShot(0, this, [this] { refreshServices(); });
    timeout_.setSingleShot(true);
    timeout_.setInterval(8000);

    connect(&timeout_, &QTimer::timeout, this, [this] {
        timedOut_ = true;
        process_.kill();
    });
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (!busy_ || timedOut_)
            return;
        timeout_.stop();
        const QString reason = error == QProcess::FailedToStart
            ? QCoreApplication::translate("KEncoderKcm", "Příkaz encoder nelze spustit.")
            : QCoreApplication::translate("KEncoderKcm", "Chyba programu encoder: %1").arg(process_.errorString());
        if (reading_) {
            setLightingStatus(QCoreApplication::translate("KEncoderKcm", "Stav podsvícení: %1").arg(reason));
            setKeyboardReadiness(0, reason);
            reading_ = false;
        } else {
            setFeedback(reason);
        }
        setBusy(false);
    });
    connect(&process_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
        if (!busy_)
            return;
        timeout_.stop();
        if (reading_) {
            reading_ = false;
            const QByteArray output = process_.readAllStandardOutput();
            const QString standardError = QString::fromUtf8(process_.readAllStandardError());
            const QString diagnostic = (standardError.trimmed().isEmpty()
                ? QString::fromUtf8(output) : standardError).simplified().left(200);
            EncoderLightingStatus result;
            if (!timedOut_ && exitStatus == QProcess::NormalExit && exitCode == 0 &&
                parseEncoderLightingStatus(output, &result)) {
                reportedAvailable_ = true;
                reportedBrightness_ = result.brightness;
                reportedMode_ = result.mode;
                setLightingStatus(result.description());
                setKeyboardReadiness(2, QCoreApplication::translate("KEncoderKcm", "Ovládání a čtení podsvícení dostupné."));
            } else {
                reportedAvailable_ = false;
                QString reason;
                if (timedOut_)
                    reason = QCoreApplication::translate("KEncoderKcm", "časový limit čtení");
                else if (exitStatus != QProcess::NormalExit || exitCode != 0)
                    reason = QCoreApplication::translate("KEncoderKcm", "encoder skončil s kódem %1").arg(exitCode);
                else
                    reason = QCoreApplication::translate("KEncoderKcm", "neplatná odpověď encoder --status --json");
                if (!diagnostic.isEmpty())
                    reason += QStringLiteral(": %1").arg(diagnostic);
                reason += QStringLiteral(" [%1]").arg(process_.program());
                setLightingStatus(QCoreApplication::translate("KEncoderKcm", "Stav podsvícení: %1.").arg(reason));
                setKeyboardReadiness(0, reason);
            }
            setBusy(false);
            return;
        }
        bool commandSucceeded = false;
        if (timedOut_) {
            setFeedback(QCoreApplication::translate("KEncoderKcm", "Klávesnice neodpověděla do 8 sekund."));
        } else {
            const QString out = QString::fromUtf8(process_.readAllStandardOutput()).trimmed();
            const QString err = QString::fromUtf8(process_.readAllStandardError()).trimmed();
            if (exitStatus == QProcess::NormalExit && exitCode == 0 &&
                !out.contains(QStringLiteral("Command failed"))) {
                commandSucceeded = true;
                setFeedback(QCoreApplication::translate("KEncoderKcm", "Příkaz odeslán: %1. Aktualizuji stav podsvícení…")
                                .arg(successMessage_));
            } else {
                const QString detail = !err.isEmpty() ? err : out;
                setFeedback(QCoreApplication::translate("KEncoderKcm", "encoder selhal (kód %1): %2").arg(exitCode).arg(detail));
            }
        }
        setBusy(false);
        if (commandSucceeded)
            QTimer::singleShot(0, this, [this] { refreshLightingStatus(); });
    });
    // A read-only query is the only action allowed when the KCM opens.
    QTimer::singleShot(0, this, [this] { refreshLightingStatus(); });
}

void KEncoderKcm::applyLighting(const QString &mode, int brightness, const QString &speed)
{
    if (busy_)
        return;
    if (brightness < 0 || brightness > 3) {
        setFeedback(QCoreApplication::translate("KEncoderKcm", "Neplatná úroveň jasu."));
        return;
    }
    if (mode == QLatin1String("static")) {
        execute({QStringLiteral("-b"), QString::number(brightness), QStringLiteral("-s")},
                QCoreApplication::translate("KEncoderKcm", "statické podsvícení"));
        return;
    }
    if (mode == QLatin1String("wave")) {
        setFeedback(QCoreApplication::translate("KEncoderKcm", "Zvolte směr vlny před odesláním nastavení."));
        return;
    }
    if (mode != QLatin1String("breathe") && mode != QLatin1String("left_wave") &&
        mode != QLatin1String("right_wave") && mode != QLatin1String("ripple")) {
        setFeedback(QCoreApplication::translate("KEncoderKcm", "Neznámá animace."));
        return;
    }
    if (speed != QLatin1String("slow") && speed != QLatin1String("medium") &&
        speed != QLatin1String("fast")) {
        setFeedback(QCoreApplication::translate("KEncoderKcm", "Neplatná rychlost animace."));
        return;
    }
    execute({QStringLiteral("-b"), QString::number(brightness),
             QStringLiteral("-a"), mode, QStringLiteral("-v"), speed},
            QCoreApplication::translate("KEncoderKcm", "animace"));
}

void KEncoderKcm::turnOff()
{
    if (!busy_)
        execute({QStringLiteral("-o")}, QCoreApplication::translate("KEncoderKcm", "podsvícení vypnuto"));
}

void KEncoderKcm::applyDefault()
{
    if (busy_)
        return;
    KEncoderNotify::Configuration config;
    QString error;
    if (!KEncoderNotify::load(&config, &error)) {
        setFeedback(QCoreApplication::translate("KEncoderKcm", "Nelze načíst preset Default: %1").arg(error));
        return;
    }
    const QJsonObject preset = KEncoderNotify::presetById(
        config, QString::fromLatin1(KEncoderNotify::kDefaultPresetId));
    const QJsonArray steps = preset.value(QStringLiteral("steps")).toArray();
    if (steps.isEmpty()) {
        setFeedback(QCoreApplication::translate("KEncoderKcm", "Preset Default nemá žádný krok."));
        return;
    }
    const QJsonObject step = steps.first().toObject();
    const QString type = step.value(QStringLiteral("type")).toString();
    QStringList arguments;
    if (type == QLatin1String("off")) {
        arguments = {QStringLiteral("-o")};
    } else {
        const int level = step.value(QStringLiteral("brightness")).toInt(-1);
        if (level < 0 || level > 3) {
            setFeedback(QCoreApplication::translate("KEncoderKcm", "Neplatný jas v presetu Default."));
            return;
        }
        arguments = {QStringLiteral("-b"), QString::number(level)};
        if (type == QLatin1String("static")) {
            arguments << QStringLiteral("-s");
        } else if (type == QLatin1String("breathe") || type == QLatin1String("ripple") ||
                   type == QLatin1String("left_wave") || type == QLatin1String("right_wave")) {
            const int speed = step.value(QStringLiteral("speed")).toInt(-1);
            if (speed < 1 || speed > 3) {
                setFeedback(QCoreApplication::translate("KEncoderKcm", "Neplatná rychlost v presetu Default."));
                return;
            }
            const QString speedName = speed == 1 ? QStringLiteral("slow") :
                                      speed == 3 ? QStringLiteral("fast") : QStringLiteral("medium");
            arguments << QStringLiteral("-a") << type << QStringLiteral("-v") << speedName;
        } else {
            setFeedback(QCoreApplication::translate("KEncoderKcm", "Nepodporovaný krok v presetu Default."));
            return;
        }
    }
    // Execute directly, even with notifications disabled. No daemon or HID readback
    // is needed to choose the command; the normal KCM verification runs afterward.
    execute(arguments, QCoreApplication::translate("KEncoderKcm", "preset Default"));
}

bool KEncoderKcm::saveSessionPolicy(bool offOnLock, bool offOnScreenOff,
                                         bool notifyWhenInactive)
{
    const KEncoderSession::Policy desired{offOnLock, offOnScreenOff, notifyWhenInactive};
    QString error;
    if (!KEncoderSession::savePolicy(desired, &error)) {
        setFeedback(error);
        Q_EMIT sessionPolicyChanged(); // Re-sync checkboxes after failed write.
        return false;
    }
    sessionPolicy_ = desired;
    Q_EMIT sessionPolicyChanged();

    auto bus = QDBusConnection::sessionBus();
    if (bus.isConnected() && bus.interface()) {
        const QDBusReply<bool> present = bus.interface()->isServiceRegistered(
            QStringLiteral("org.kencoder.Notify1"));
        if (present.isValid() && present.value()) {
            // Do not silently pass new policy to an old daemon that ignores it.
            const QDBusMessage protocolCall = QDBusMessage::createMethodCall(
                QStringLiteral("org.kencoder.Notify1"), QStringLiteral("/org/kencoder/Notify1"),
                QStringLiteral("org.kencoder.Notify1"),
                QStringLiteral("SessionPolicyProtocolVersion"));
            const QDBusMessage protocolReply = bus.call(protocolCall, QDBus::Block, 1000);
            if (protocolReply.type() != QDBusMessage::ReplyMessage ||
                protocolReply.arguments().isEmpty() ||
                protocolReply.arguments().first().toUInt() != 1) {
                setFeedback(QCoreApplication::translate("KEncoderKcm", "Nastavení uloženo. Běží starší kencoder-notify; "
                                          "restartujte službu relace."));
                return false;
            }
            QDBusMessage call = QDBusMessage::createMethodCall(
                QStringLiteral("org.kencoder.Notify1"), QStringLiteral("/org/kencoder/Notify1"),
                QStringLiteral("org.kencoder.Notify1"), QStringLiteral("Reload"));
            const QDBusMessage reply = bus.call(call, QDBus::Block, 1000);
            if (reply.type() == QDBusMessage::ReplyMessage &&
                !reply.arguments().isEmpty() && reply.arguments().first().toBool()) {
                setFeedback(QCoreApplication::translate("KEncoderKcm", "Nastavení relace uloženo a aktivováno."));
                return true;
            }
            setFeedback(QCoreApplication::translate("KEncoderKcm", "Nastavení uloženo, ale běžící služba jej nepřijala."));
            return false;
        }
    }
    const QString daemon = QStandardPaths::findExecutable(QStringLiteral("kencoder-notify"));
    if (daemon.isEmpty()) {
        setFeedback(QCoreApplication::translate("KEncoderKcm", "Nastavení uloženo, ale chybí kencoder-notify v PATH."));
        return true; // Persistence succeeded; startup will work after installation.
    }
    if (!QProcess::startDetached(daemon, {QStringLiteral("--daemon")})) {
        setFeedback(QCoreApplication::translate("KEncoderKcm", "Nastavení uloženo, ale službu nelze spustit."));
        return true;
    }
    setFeedback(QCoreApplication::translate("KEncoderKcm", "Nastavení uloženo, spouštím službu relace."));
    return true;
}

void KEncoderKcm::launchGui()
{
    const QString application = QStandardPaths::findExecutable(QStringLiteral("kencoder"));
    if (application.isEmpty()) {
        setFeedback(QCoreApplication::translate("KEncoderKcm", "Aplikace kencoder není dostupná v PATH."));
        return;
    }
    if (QProcess::startDetached(application, {}))
        setFeedback(QCoreApplication::translate("KEncoderKcm", "Spouštím KEncoder."));
    else
        setFeedback(QCoreApplication::translate("KEncoderKcm", "Aplikaci KEncoder nelze spustit."));
}

void KEncoderKcm::execute(const QStringList &arguments, const QString &successMessage)
{
    if (busy_)
        return;
    const QString cli = QStandardPaths::findExecutable(QStringLiteral("encoder"));
    if (cli.isEmpty()) {
        setFeedback(QCoreApplication::translate("KEncoderKcm", "Příkaz encoder není nainstalovaný nebo není v PATH."));
        return;
    }
    successMessage_ = successMessage;
    reading_ = false;
    timedOut_ = false;
    timeout_.setInterval(8000);
    process_.setProgram(cli);
    process_.setArguments(arguments); // No shell, no user-controlled command string.
    setBusy(true);
    setFeedback(QCoreApplication::translate("KEncoderKcm", "Odesílání příkazu…"));
    timeout_.start();
    process_.start();
}

void KEncoderKcm::refreshLightingStatus()
{
    if (busy_)
        return;
    const QString cli = QStandardPaths::findExecutable(QStringLiteral("encoder"));
    if (cli.isEmpty()) {
        reportedAvailable_ = false;
        setLightingStatus(QCoreApplication::translate("KEncoderKcm", "Stav podsvícení: chybí encoder v PATH."));
        setKeyboardReadiness(0, QCoreApplication::translate("KEncoderKcm", "Chybí program encoder v PATH."));
        return;
    }
    reading_ = true;
    if (keyboardReadiness_ != 2)
        setKeyboardReadiness(1, QCoreApplication::translate("KEncoderKcm", "Čtu stav klávesnice…"));
    timedOut_ = false;
    reportedAvailable_ = false;
    setLightingStatus(QCoreApplication::translate("KEncoderKcm", "Zjišťuji skutečný stav klávesnice…"));
    process_.setProgram(cli);
    process_.setArguments({QStringLiteral("--status"), QStringLiteral("--json")});
    setBusy(true);
    timeout_.start(4000);
    process_.start();
}

void KEncoderKcm::setKeyboardReadiness(int level, const QString &message)
{
    if (keyboardReadiness_ == level && keyboardReadinessText_ == message) return;
    keyboardReadiness_ = level;
    keyboardReadinessText_ = message;
    Q_EMIT readinessChanged();
}

void KEncoderKcm::refreshReadiness()
{
    refreshServices();
    if (!busy_) refreshLightingStatus();
}

void KEncoderKcm::refreshServices()
{
    const QString executable = QStandardPaths::findExecutable(QStringLiteral("kencoder-notify"));
    KEncoderNotify::Configuration config;
    QString error;
    const bool validConfig = KEncoderNotify::load(&config, &error);
    bool muted = false;
    const bool validMute = KEncoderNotify::loadMute(&muted, &error);
    const auto bus = QDBusConnection::sessionBus();
    bool running = false;
    if (bus.isConnected() && bus.interface()) {
        const QDBusReply<bool> present = bus.interface()->isServiceRegistered(
            QStringLiteral("org.kencoder.Notify1"));
        running = present.isValid() && present.value();
    }
    int notifyLevel = 1;
    int listenerLevel = 1;
    QString notifyText;
    QString listenerText;
    if (!validConfig || !validMute) {
        notifyLevel = 0;
        notifyText = QCoreApplication::translate("KEncoderKcm", "Chybná konfigurace KEncoder Notify: %1").arg(error);
    } else if (executable.isEmpty()) {
        notifyLevel = 0;
        notifyText = QCoreApplication::translate("KEncoderKcm", "Chybí spustitelný kencoder-notify v PATH.");
    } else if (!running) {
        notifyText = QCoreApplication::translate("KEncoderKcm", "Program dostupný, služba aktuálně neběží.");
    } else {
        const QDBusMessage protocolCall = QDBusMessage::createMethodCall(
            QStringLiteral("org.kencoder.Notify1"), QStringLiteral("/org/kencoder/Notify1"),
            QStringLiteral("org.kencoder.Notify1"), QStringLiteral("SessionPolicyProtocolVersion"));
        const QDBusMessage protocolReply = bus.call(protocolCall, QDBus::Block, 300);
        if (protocolReply.type() != QDBusMessage::ReplyMessage ||
            protocolReply.arguments().isEmpty() || protocolReply.arguments().first().toUInt() != 1) {
            notifyLevel = 0;
            notifyText = QCoreApplication::translate("KEncoderKcm", "Běží nekompatibilní nebo neodpovídající služba.");
        } else {
            const QDBusMessage muteCall = QDBusMessage::createMethodCall(
                QStringLiteral("org.kencoder.Notify1"), QStringLiteral("/org/kencoder/Notify1"),
                QStringLiteral("org.kencoder.Notify1"), QStringLiteral("MuteProtocolVersion"));
            const QDBusMessage muteReply = bus.call(muteCall, QDBus::Block, 300);
            if (muteReply.type() != QDBusMessage::ReplyMessage ||
                muteReply.arguments().isEmpty() || muteReply.arguments().first().toUInt() != 1) {
                notifyLevel = 0;
                notifyText = QCoreApplication::translate("KEncoderKcm", "Běží stará služba bez ztlumení. Restartujte ji.");
            } else {
                notifyLevel = 2;
                notifyText = muted ? QCoreApplication::translate("KEncoderKcm", "Služba běží, světelné reakce jsou ztlumené.")
                                   : QCoreApplication::translate("KEncoderKcm", "Služba KEncoder Notify je připravená.");
            }
        }
    }
    if (!running) {
        listenerLevel = config.enabled ? 0 : 1;
        listenerText = config.enabled
            ? QCoreApplication::translate("KEncoderKcm", "Listener je povolen, ale služba neběží.")
            : QCoreApplication::translate("KEncoderKcm", "Listener je vypnutý.");
    } else {
        const QDBusMessage stateCall = QDBusMessage::createMethodCall(
            QStringLiteral("org.kencoder.Notify1"), QStringLiteral("/org/kencoder/Notify1"),
            QStringLiteral("org.kencoder.Notify1"), QStringLiteral("ListenerStatus"));
        const QDBusMessage stateReply = bus.call(stateCall, QDBus::Block, 300);
        const QString state = (stateReply.type() == QDBusMessage::ReplyMessage &&
                               !stateReply.arguments().isEmpty())
            ? stateReply.arguments().first().toString() : QString();
        if (state == QLatin1String("active")) {
            listenerLevel = 2;
            listenerText = muted ? QCoreApplication::translate("KEncoderKcm", "Listener aktivní; reakce ztlumené.")
                                  : QCoreApplication::translate("KEncoderKcm", "Listener zaregistrován u Plasma 6.");
        } else if (state == QLatin1String("disabled")) {
            listenerText = QCoreApplication::translate("KEncoderKcm", "Listener je vypnutý v nastavení KEncoder Notify.");
        } else if (state == QLatin1String("unavailable")) {
            listenerLevel = 0;
            listenerText = QCoreApplication::translate("KEncoderKcm", "Listener povolen, ale registrace u Plasma selhala.");
        } else {
            listenerLevel = 0;
            listenerText = QCoreApplication::translate("KEncoderKcm", "Stav listeneru nelze ověřit (stará služba?).");
        }
    }
    if (notifyReadiness_ != notifyLevel || notifyReadinessText_ != notifyText ||
        listenerReadiness_ != listenerLevel || listenerReadinessText_ != listenerText) {
        notifyReadiness_ = notifyLevel;
        notifyReadinessText_ = notifyText;
        listenerReadiness_ = listenerLevel;
        listenerReadinessText_ = listenerText;
        Q_EMIT readinessChanged();
    }
}

void KEncoderKcm::setLightingStatus(const QString &status)
{
    lightingStatus_ = status;
    Q_EMIT lightingStatusChanged();
}

void KEncoderKcm::setBusy(bool busy)
{
    if (busy_ == busy)
        return;
    busy_ = busy;
    Q_EMIT busyChanged();
}

void KEncoderKcm::setFeedback(const QString &feedback)
{
    if (feedback_ == feedback)
        return;
    feedback_ = feedback;
    Q_EMIT feedbackChanged();
}


#include "kcm_kencoder.moc"
