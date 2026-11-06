// SPDX-License-Identifier: BSD-3-Clause
// Copyright 2026 Pitriss
#include "../lighting_status.h"
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

public:
    KEncoderKcm(QObject *parent, const KPluginMetaData &metaData);

    bool busy() const { return busy_; }
    QString feedback() const { return feedback_; }
    QString lightingStatus() const { return lightingStatus_; }
    bool reportedAvailable() const { return reportedAvailable_; }
    int reportedBrightness() const { return reportedBrightness_; }
    QString reportedMode() const { return reportedMode_; }

    Q_INVOKABLE void applyLighting(const QString &mode, int brightness, const QString &speed);
    Q_INVOKABLE void turnOff();
    Q_INVOKABLE void refreshLightingStatus();

Q_SIGNALS:
    void busyChanged();
    void feedbackChanged();
    void lightingStatusChanged();

private:
    void execute(const QStringList &arguments, const QString &successMessage);
    void setBusy(bool busy);
    void setFeedback(const QString &feedback);
    void setLightingStatus(const QString &status);

    QProcess process_;
    QTimer timeout_;
    bool busy_ = false;
    bool timedOut_ = false;
    bool reading_ = false;
    bool reportedAvailable_ = false;
    int reportedBrightness_ = -1;
    QString reportedMode_;
    QString lightingStatus_ = QStringLiteral("Stav podsvícení: nezjištěn.");
    QString feedback_ = QStringLiteral("Změny se odesílají pouze tlačítky Nastavit a Vypnout.");
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
        const QString reason = error == QProcess::FailedToStart
            ? QStringLiteral("Příkaz encoder nelze spustit.")
            : QStringLiteral("Chyba programu encoder: %1").arg(process_.errorString());
        if (reading_) {
            setLightingStatus(QStringLiteral("Stav podsvícení: %1").arg(reason));
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
            EncoderLightingStatus result;
            if (!timedOut_ && exitStatus == QProcess::NormalExit && exitCode == 0 &&
                parseEncoderLightingStatus(process_.readAllStandardOutput(), &result)) {
                reportedAvailable_ = true;
                reportedBrightness_ = result.brightness;
                reportedMode_ = result.mode;
                setLightingStatus(result.description());
            } else {
                reportedAvailable_ = false;
                const QString reason = timedOut_
                    ? QStringLiteral("časový limit čtení")
                    : QStringLiteral("nelze načíst z klávesnice (encoder --status --json)");
                setLightingStatus(QStringLiteral("Stav podsvícení: %1.").arg(reason));
            }
            setBusy(false);
            return;
        }
        bool commandSucceeded = false;
        if (timedOut_) {
            setFeedback(QStringLiteral("Klávesnice neodpověděla do 8 sekund."));
        } else {
            const QString out = QString::fromUtf8(process_.readAllStandardOutput()).trimmed();
            const QString err = QString::fromUtf8(process_.readAllStandardError()).trimmed();
            if (exitStatus == QProcess::NormalExit && exitCode == 0 &&
                !out.contains(QStringLiteral("Command failed"))) {
                commandSucceeded = true;
                setFeedback(QStringLiteral("Příkaz odeslán: %1. Výsledek čtení je uveden výše.")
                                .arg(successMessage_));
            } else {
                const QString detail = !err.isEmpty() ? err : out;
                setFeedback(QStringLiteral("encoder selhal (kód %1): %2").arg(exitCode).arg(detail));
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
        setFeedback(QStringLiteral("Neplatná úroveň jasu."));
        return;
    }
    if (mode == QLatin1String("static")) {
        execute({QStringLiteral("-b"), QString::number(brightness), QStringLiteral("-s")},
                QStringLiteral("statické podsvícení"));
        return;
    }
    if (mode == QLatin1String("wave")) {
        setFeedback(QStringLiteral("Zvolte směr vlny před odesláním nastavení."));
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
    reading_ = false;
    timedOut_ = false;
    timeout_.setInterval(8000);
    process_.setProgram(cli);
    process_.setArguments(arguments); // No shell, no user-controlled command string.
    setBusy(true);
    setFeedback(QStringLiteral("Odesílání příkazu…"));
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
        setLightingStatus(QStringLiteral("Stav podsvícení: chybí encoder v PATH."));
        return;
    }
    reading_ = true;
    timedOut_ = false;
    reportedAvailable_ = false;
    setLightingStatus(QStringLiteral("Zjišťuji skutečný stav klávesnice…"));
    process_.setProgram(cli);
    process_.setArguments({QStringLiteral("--status"), QStringLiteral("--json")});
    setBusy(true);
    timeout_.start(4000);
    process_.start();
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
