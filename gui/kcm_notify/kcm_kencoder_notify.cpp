// SPDX-License-Identifier: BSD-3-Clause
// Copyright 2026 Pitriss
#include "../../notify/config.h"

#include <KPluginFactory>
#include <KQuickConfigModule>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QUuid>
#include <QVariantList>

using KEncoderNotify::Configuration;

class KEncoderNotifyKcm final : public KQuickConfigModule
{
    Q_OBJECT
    Q_PROPERTY(bool listeningEnabled READ listeningEnabled NOTIFY essentialsChanged)
    Q_PROPERTY(QString backend READ backend NOTIFY essentialsChanged)
    Q_PROPERTY(QVariantList presets READ presets NOTIFY presetsChanged)
    Q_PROPERTY(QVariantList rules READ rules NOTIFY rulesChanged)
    Q_PROPERTY(QString feedback READ feedback NOTIFY feedbackChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
public:
    KEncoderNotifyKcm(QObject *parent, const KPluginMetaData &metaData)
        : KQuickConfigModule(parent, metaData)
    {
        setButtons(NoAdditionalButton); // All writes require explicit UI buttons.
        setNeedsSave(false);
        QString error;
        if (!KEncoderNotify::load(&config_, &error))
            setFeedback(QStringLiteral("Konfigurace je neplatná: %1").arg(error));
        else
            setFeedback(QStringLiteral("Sekvence se spouštějí pouze příkazem Přehrát nebo povoleným listenerem. ") +
                        QStringLiteral("Po provedení se neověřuje readback."));

        connect(&playback_, &QProcess::errorOccurred, this,
                [this](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) {
                setFeedback(QStringLiteral("Nelze spustit kencoder-notify."));
                setBusy(false);
            }
        });
        connect(&playback_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this](int code, QProcess::ExitStatus status) {
            setBusy(false);
            if (status == QProcess::NormalExit && code == 0) {
                setFeedback(QStringLiteral("Sekvence dokončena nebo předána listeneru. Stav zařízení není ověřen readbackem."));
            } else {
                const QString details = QString::fromUtf8(playback_.readAllStandardError()).trimmed();
                setFeedback(details.isEmpty()
                    ? QStringLiteral("Přehrávání nebo návrat do Default selhal (kód %1).").arg(code)
                    : QStringLiteral("Přehrávání nebo návrat do Default selhal: %1").arg(details.left(320)));
            }
        });
    }

    bool listeningEnabled() const { return config_.enabled; }
    QString backend() const { return config_.backend; }
    QVariantList presets() const { return config_.presets.toVariantList(); }
    QVariantList rules() const { return config_.rules.toVariantList(); }
    QString feedback() const { return feedback_; }
    bool busy() const { return busy_; }

    Q_INVOKABLE bool applyEssentials(bool enabled, const QString &backend)
    {
        if (enabled && backend != QLatin1String(KEncoderNotify::kWatcherBackend)) {
            setFeedback(QStringLiteral("Listener KNotify scanner bude dostupný až ve fázi 2."));
            return false;
        }
        const QString exe = QStandardPaths::findExecutable(QStringLiteral("kencoder-notify"));
        if (enabled && exe.isEmpty()) {
            setFeedback(QStringLiteral("Listener není dostupný v PATH. Nejdřív jej sestavte/nainstalujte."));
            return false;
        }
        Configuration updated = config_;
        updated.enabled = enabled;
        updated.backend = backend;
        QString error;
        if (!KEncoderNotify::saveRules(updated, &error)) {
            setFeedback(error);
            return false;
        }
        config_ = updated;
        Q_EMIT essentialsChanged();
        if (enabled && !QProcess::startDetached(exe, {QStringLiteral("--daemon")})) {
            // Persisted state remains enabled; show failure explicitly.
            setFeedback(QStringLiteral("Nastavení uloženo, ale listener se nepodařilo spustit."));
            return false;
        }
        setFeedback(enabled
            ? QStringLiteral("Povoleno v konfiguraci. Spuštění procesu neprokazuje registraci v KDE D-Bus.")
            : QStringLiteral("Naslouchání vypnuto. Běžící listener změnu konfigurace načte."));
        return true;
    }

    Q_INVOKABLE bool savePreset(const QString &id, const QString &name,
                                const QVariantList &steps)
    {
        const QString actualId = id.isEmpty()
            ? QUuid::createUuid().toString(QUuid::WithoutBraces) : id;
        const bool isDefault = actualId == QLatin1String(KEncoderNotify::kDefaultPresetId);
        Configuration updated = config_;
        const QJsonArray savedSteps = isDefault && !steps.isEmpty()
            ? QJsonArray{QJsonValue::fromVariant(steps.first())}
            : QJsonArray::fromVariantList(steps);
        QJsonObject preset{{QStringLiteral("id"), actualId},
                           {QStringLiteral("name"), isDefault ? QStringLiteral("Default") : name.trimmed()},
                           {QStringLiteral("steps"), savedSteps}};
        bool replaced = false;
        for (int i = 0; i < updated.presets.size(); ++i) {
            if (updated.presets.at(i).toObject().value(QStringLiteral("id")).toString() == actualId) {
                updated.presets.replace(i, preset);
                replaced = true;
                break;
            }
        }
        if (!replaced) updated.presets.append(preset);
        QString error;
        if (!KEncoderNotify::savePresets(updated, &error)) {
            setFeedback(error);
            return false;
        }
        config_ = updated;
        Q_EMIT presetsChanged();
        setFeedback(QStringLiteral("Preset uložen."));
        return true;
    }

    Q_INVOKABLE bool deletePreset(const QString &id)
    {
        if (id == QLatin1String(KEncoderNotify::kDefaultPresetId)) {
            setFeedback(QStringLiteral("Preset Default nelze odstranit."));
            return false;
        }
        for (const auto &entry : config_.rules) {
            if (entry.toObject().value(QStringLiteral("preset_id")).toString() == id) {
                setFeedback(QStringLiteral("Preset používá pravidlo. Nejdříve upravte nebo odstraňte pravidlo."));
                return false;
            }
        }
        Configuration updated = config_;
        for (int i = 0; i < updated.presets.size(); ++i) {
            if (updated.presets.at(i).toObject().value(QStringLiteral("id")).toString() == id) {
                updated.presets.removeAt(i);
                QString error;
                if (!KEncoderNotify::savePresets(updated, &error)) {
                    setFeedback(error);
                    return false;
                }
                config_ = updated;
                Q_EMIT presetsChanged();
                setFeedback(QStringLiteral("Preset odstraněn."));
                return true;
            }
        }
        return false;
    }

    Q_INVOKABLE bool saveRule(const QString &id, const QString &name, bool enabled,
                              const QString &app, const QString &title,
                              const QString &text, const QString &presetId, int cooldownMs)
    {
        const QString actualId = id.isEmpty()
            ? QUuid::createUuid().toString(QUuid::WithoutBraces) : id;
        Configuration updated = config_;
        QJsonObject match{{QStringLiteral("app_contains"), app.trimmed()},
                          {QStringLiteral("title_contains"), title.trimmed()},
                          {QStringLiteral("text_contains"), text.trimmed()}};
        QJsonObject rule{{QStringLiteral("id"), actualId},
                         {QStringLiteral("name"), name.trimmed()},
                         {QStringLiteral("enabled"), enabled},
                         {QStringLiteral("match"), match},
                         {QStringLiteral("preset_id"), presetId},
                         {QStringLiteral("cooldown_ms"), cooldownMs}};
        bool replaced = false;
        for (int i = 0; i < updated.rules.size(); ++i) {
            if (updated.rules.at(i).toObject().value(QStringLiteral("id")).toString() == actualId) {
                updated.rules.replace(i, rule);
                replaced = true;
                break;
            }
        }
        if (!replaced) updated.rules.append(rule);
        QString error;
        if (!KEncoderNotify::saveRules(updated, &error)) {
            setFeedback(error);
            return false;
        }
        config_ = updated;
        Q_EMIT rulesChanged();
        setFeedback(QStringLiteral("Pravidlo uloženo."));
        return true;
    }

    Q_INVOKABLE bool moveRuleTo(const QString &id, int targetIndex)
    {
        Configuration updated = config_;
        if (targetIndex < 0 || targetIndex >= updated.rules.size())
            return false;
        for (int i = 0; i < updated.rules.size(); ++i) {
            if (updated.rules.at(i).toObject().value(QStringLiteral("id")).toString() != id)
                continue;
            if (i == targetIndex) return true;
            const QJsonValue moved = updated.rules.at(i);
            updated.rules.removeAt(i);
            updated.rules.insert(targetIndex, moved);
            QString error;
            if (!KEncoderNotify::saveRules(updated, &error)) {
                setFeedback(error);
                return false;
            }
            config_ = updated;
            Q_EMIT rulesChanged();
            setFeedback(QStringLiteral("Pravidlo přesunuto na pozici %1.").arg(targetIndex + 1));
            return true;
        }
        setFeedback(QStringLiteral("Pravidlo neexistuje."));
        return false;
    }

    Q_INVOKABLE bool moveRule(const QString &id, int offset)
    {
        if (offset != -1 && offset != 1) {
            setFeedback(QStringLiteral("Pravidlo lze přesunout jen o jednu pozici."));
            return false;
        }
        Configuration updated = config_;
        for (int i = 0; i < updated.rules.size(); ++i) {
            if (updated.rules.at(i).toObject().value(QStringLiteral("id")).toString() != id)
                continue;
            const int target = i + offset;
            if (target < 0 || target >= updated.rules.size())
                return false;
            const QJsonValue moved = updated.rules.at(i);
            updated.rules.replace(i, updated.rules.at(target));
            updated.rules.replace(target, moved);
            QString error;
            if (!KEncoderNotify::saveRules(updated, &error)) {
                setFeedback(error);
                return false;
            }
            config_ = updated;
            Q_EMIT rulesChanged();
            setFeedback(offset < 0 ? QStringLiteral("Pravidlo posunuto nahoru.")
                                   : QStringLiteral("Pravidlo posunuto dolů."));
            return true;
        }
        setFeedback(QStringLiteral("Pravidlo neexistuje."));
        return false;
    }

    Q_INVOKABLE bool setRuleEnabled(const QString &id, bool enabled)
    {
        Configuration updated = config_;
        for (int i = 0; i < updated.rules.size(); ++i) {
            QJsonObject rule = updated.rules.at(i).toObject();
            if (rule.value(QStringLiteral("id")).toString() != id)
                continue;
            if (rule.value(QStringLiteral("enabled")).toBool() == enabled)
                return true;
            rule.insert(QStringLiteral("enabled"), enabled);
            updated.rules.replace(i, rule);
            QString error;
            if (!KEncoderNotify::saveRules(updated, &error)) {
                setFeedback(error);
                return false;
            }
            config_ = updated;
            Q_EMIT rulesChanged();
            setFeedback(enabled ? QStringLiteral("Pravidlo zapnuto.")
                                : QStringLiteral("Pravidlo vypnuto."));
            return true;
        }
        setFeedback(QStringLiteral("Pravidlo neexistuje."));
        return false;
    }

    Q_INVOKABLE bool deleteRule(const QString &id)
    {
        Configuration updated = config_;
        for (int i = 0; i < updated.rules.size(); ++i) {
            if (updated.rules.at(i).toObject().value(QStringLiteral("id")).toString() == id) {
                updated.rules.removeAt(i);
                QString error;
                if (!KEncoderNotify::saveRules(updated, &error)) {
                    setFeedback(error);
                    return false;
                }
                config_ = updated;
                Q_EMIT rulesChanged();
                setFeedback(QStringLiteral("Pravidlo odstraněno."));
                return true;
            }
        }
        return false;
    }

    Q_INVOKABLE void playPreset(const QString &id)
    {
        if (busy_ || KEncoderNotify::presetById(config_, id).isEmpty()) return;
        const QString exe = QStandardPaths::findExecutable(QStringLiteral("kencoder-notify"));
        if (exe.isEmpty()) {
            setFeedback(QStringLiteral("Není dostupný kencoder-notify v PATH."));
            return;
        }
        setBusy(true);
        setFeedback(QStringLiteral("Spouštím sekvenci, bez hardwarového readbacku…"));
        playback_.start(exe, {QStringLiteral("--play"), id});
    }

Q_SIGNALS:
    void essentialsChanged();
    void presetsChanged();
    void rulesChanged();
    void feedbackChanged();
    void busyChanged();

private:
    void setFeedback(const QString &text)
    {
        feedback_ = text;
        Q_EMIT feedbackChanged();
    }
    void setBusy(bool on)
    {
        if (busy_ == on) return;
        busy_ = on;
        Q_EMIT busyChanged();
    }
    Configuration config_;
    QProcess playback_;
    QString feedback_;
    bool busy_ = false;
};

K_PLUGIN_CLASS_WITH_JSON(KEncoderNotifyKcm, "kcm_kencoder_notify.json")

#include "kcm_kencoder_notify.moc"
