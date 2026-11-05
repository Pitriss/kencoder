// SPDX-License-Identifier: BSD-3-Clause
// Copyright 2026 Pitriss
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

public:
    KEncoderKcm(QObject *parent, const KPluginMetaData &metaData);

    bool busy() const { return busy_; }
    QString feedback() const { return feedback_; }

    Q_INVOKABLE void applyLighting(const QString &mode, int brightness, const QString &speed);
    Q_INVOKABLE void turnOff();

Q_SIGNALS:
    void busyChanged();
    void feedbackChanged();

private:
    void execute(const QStringList &arguments, const QString &successMessage);
    void setBusy(bool busy);
    void setFeedback(const QString &feedback);

    QProcess process_;
    QTimer timeout_;
    bool busy_ = false;
    bool timedOut_ = false;
    QString feedback_ = QStringLiteral("Aktuální stav zařízení není načten. Hodnoty jsou pouze předvolba.");
    QString successMessage_;
};

K_PLUGIN_CLASS_WITH_JSON(KEncoderKcm, "kcm_kencoder.json")

KEncoderKcm::KEncoderKcm(QObject *parent, const KPluginMetaData &metaData)
    : KQuickConfigModule(parent, metaData)
{
    // The only way to change the lighting is clicking the explicit buttons.
    // KCM's implicit Apply/Defaults actions must never write to the keyboard.
    setButtons(NoAdditionalButton);
    setNeedsSave(false);
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
        setFeedback(error == QProcess::FailedToStart
                        ? QStringLiteral("Nelze spustit encoder. Zkontrolujte instalaci.")
                        : QStringLiteral("Chyba spuštění encoder: %1").arg(process_.errorString()));
        setBusy(false);
    });
    connect(&process_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
        if (!busy_)
            return;
        timeout_.stop();
        if (timedOut_) {
            setFeedback(QStringLiteral("Klávesnice neodpověděla do 8 sekund."));
        } else {
            const QString out = QString::fromUtf8(process_.readAllStandardOutput()).trimmed();
            const QString err = QString::fromUtf8(process_.readAllStandardError()).trimmed();
            if (exitStatus == QProcess::NormalExit && exitCode == 0 &&
                !out.contains(QStringLiteral("Command failed"))) {
                setFeedback(QStringLiteral("Příkaz dokončen: %1. Skutečný stav HW nebyl nezávisle ověřen.")
                                .arg(successMessage_));
            } else {
                const QString detail = !err.isEmpty() ? err : out;
                setFeedback(QStringLiteral("encoder selhal (kód %1): %2").arg(exitCode).arg(detail));
            }
        }
        setBusy(false);
    });
}

void KEncoderKcm::applyLighting(const QString &mode, int brightness, const QString &speed)
{
    if (busy_)
        return;
    if (brightness < 0 || brightness > 3) {
        setFeedback(QStringLiteral("Neplatná úroveň jasu."));
        return;
    }
    if (mode == QLatin1String("static")) {
        execute({QStringLiteral("-b"), QString::number(brightness), QStringLiteral("-s")},
                QStringLiteral("statické podsvícení"));
        return;
    }
    if (mode != QLatin1String("breathe") && mode != QLatin1String("left_wave") &&
        mode != QLatin1String("right_wave") && mode != QLatin1String("ripple")) {
        setFeedback(QStringLiteral("Neznámá animace."));
        return;
    }
    if (speed != QLatin1String("slow") && speed != QLatin1String("medium") &&
        speed != QLatin1String("fast")) {
        setFeedback(QStringLiteral("Neplatná rychlost animace."));
        return;
    }
    execute({QStringLiteral("-b"), QString::number(brightness),
             QStringLiteral("-a"), mode, QStringLiteral("-v"), speed},
            QStringLiteral("animace"));
}

void KEncoderKcm::turnOff()
{
    if (!busy_)
        execute({QStringLiteral("-o")}, QStringLiteral("podsvícení vypnuto"));
}

void KEncoderKcm::execute(const QStringList &arguments, const QString &successMessage)
{
    if (busy_)
        return;
    const QString cli = QStandardPaths::findExecutable(QStringLiteral("encoder"));
    if (cli.isEmpty()) {
        setFeedback(QStringLiteral("Příkaz encoder není nainstalovaný nebo není v PATH."));
        return;
    }
    successMessage_ = successMessage;
    timedOut_ = false;
    process_.setProgram(cli);
    process_.setArguments(arguments); // No shell, no user-controlled command string.
    setBusy(true);
    setFeedback(QStringLiteral("Odesílání příkazu…"));
    timeout_.start();
    process_.start();
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
