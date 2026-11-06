// SPDX-License-Identifier: BSD-3-Clause
// Copyright 2026 Pitriss
#include "config.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>

namespace KEncoderNotify {
namespace {

bool fail(QString *error, const QString &text)
{
    if (error)
        *error = text;
    return false;
}

bool parseFile(const QString &path, const char *listKey, QJsonObject *root, QString *error)
{
    QFile file(path);
    if (!file.exists()) {
        *root = QJsonObject{};
        return true;
    }
    if (!file.open(QIODevice::ReadOnly))
        return fail(error, QStringLiteral("Nelze otevřít %1").arg(path));
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return fail(error, QStringLiteral("Neplatný JSON: %1").arg(path));
    *root = document.object();
    if (root->value(QStringLiteral("version")).toInt(-1) != 1 ||
        !root->value(QLatin1String(listKey)).isArray())
        return fail(error, QStringLiteral("Nepodporovaný formát konfigurace: %1").arg(path));
    return true;
}

bool saveFile(const QString &path, const QJsonObject &root, QString *error)
{
    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath()))
        return fail(error, QStringLiteral("Nelze vytvořit adresář konfigurace."));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return fail(error, QStringLiteral("Nelze uložit konfiguraci: %1").arg(path));
    const QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(json) != json.size() || !file.commit())
        return fail(error, QStringLiteral("Nelze dokončit zápis: %1").arg(path));
    return true;
}

bool validIdentifier(const QJsonObject &value)
{
    return value.value(QStringLiteral("id")).isString() &&
           !value.value(QStringLiteral("id")).toString().trimmed().isEmpty() &&
           value.value(QStringLiteral("name")).isString() &&
           !value.value(QStringLiteral("name")).toString().trimmed().isEmpty();
}

QJsonObject defaultPreset()
{
    // Virtual migration: do not write a user's configuration merely by reading it.
    return {{QStringLiteral("id"), QString::fromLatin1(kDefaultPresetId)},
            {QStringLiteral("name"), QStringLiteral("Default")},
            {QStringLiteral("steps"), QJsonArray{
                QJsonObject{{QStringLiteral("type"), QStringLiteral("static")},
                            {QStringLiteral("brightness"), 1},
                            {QStringLiteral("duration_ms"), 50}}
            }}};
}

void ensureDefaultFirst(Configuration *config)
{
    for (int i = 0; i < config->presets.size(); ++i) {
        QJsonObject entry = config->presets.at(i).toObject();
        if (entry.value(QStringLiteral("id")).toString() == QLatin1String(kDefaultPresetId)) {
            // The reserved entry is always shown first and always named Default.
            entry.insert(QStringLiteral("name"), QStringLiteral("Default"));
            config->presets.removeAt(i);
            config->presets.prepend(entry);
            return;
        }
    }
    config->presets.prepend(defaultPreset());
}

bool validStep(const QJsonObject &step)
{
    const QString type = step.value(QStringLiteral("type")).toString();
    const int duration = step.value(QStringLiteral("duration_ms")).toInt(-1);
    if (duration < 50 || duration > 60000)
        return false;
    if (type == QLatin1String("off") || type == QLatin1String("wait"))
        return true;
    const int brightness = step.value(QStringLiteral("brightness")).toInt(-1);
    if (brightness < 0 || brightness > 3)
        return false;
    if (type == QLatin1String("static"))
        return true;
    if (type != QLatin1String("breathe") && type != QLatin1String("ripple") &&
        type != QLatin1String("left_wave") && type != QLatin1String("right_wave"))
        return false;
    const int speed = step.value(QStringLiteral("speed")).toInt(-1);
    return speed >= 1 && speed <= 3;
}

} // namespace

QString configDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) +
           QStringLiteral("/kencoder");
}

QString presetsFile() { return configDirectory() + QStringLiteral("/notify-presets.json"); }
QString rulesFile() { return configDirectory() + QStringLiteral("/notify-rules.json"); }

bool load(Configuration *out, QString *error)
{
    if (!out)
        return fail(error, QStringLiteral("Chybí cílová konfigurace."));
    Configuration result;
    QJsonObject presets;
    QJsonObject rules;
    if (!parseFile(presetsFile(), "presets", &presets, error) ||
        !parseFile(rulesFile(), "rules", &rules, error))
        return false;
    result.presets = presets.value(QStringLiteral("presets")).toArray();
    ensureDefaultFirst(&result);
    result.rules = rules.value(QStringLiteral("rules")).toArray();
    if (!rules.isEmpty()) {
        if (!rules.value(QStringLiteral("enabled")).isBool() ||
            !rules.value(QStringLiteral("backend")).isString())
            return fail(error, QStringLiteral("Chybí enabled/backend v notify-rules.json."));
        result.enabled = rules.value(QStringLiteral("enabled")).toBool();
        result.backend = rules.value(QStringLiteral("backend")).toString();
    }
    if (!validate(result, error))
        return false;
    *out = result;
    return true;
}

bool validate(const Configuration &config, QString *error)
{
    if (config.backend != QLatin1String(kWatcherBackend) &&
        config.backend != QLatin1String(kFutureBackend))
        return fail(error, QStringLiteral("Neznámý backend notifikací."));
    // Future backend can be selected in a file but must never activate silently.
    if (config.enabled && config.backend != QLatin1String(kWatcherBackend))
        return fail(error, QStringLiteral("Backend KNotify scanner zatím není implementován."));

    QSet<QString> ids;
    for (const auto &entry : config.presets) {
        if (!entry.isObject() || !validIdentifier(entry.toObject()))
            return fail(error, QStringLiteral("Neplatná položka presetu."));
        const QJsonObject preset = entry.toObject();
        const QString id = preset.value(QStringLiteral("id")).toString();
        if (ids.contains(id) || !preset.value(QStringLiteral("steps")).isArray())
            return fail(error, QStringLiteral("Duplicitní ID nebo chybějící kroky presetu: %1").arg(id));
        ids.insert(id);
        const QJsonArray steps = preset.value(QStringLiteral("steps")).toArray();
        if (steps.isEmpty())
            return fail(error, QStringLiteral("Preset musí obsahovat alespoň jeden krok: %1").arg(id));
        // The Default preset describes a lighting state, never a timed wait.
        if (id == QLatin1String(kDefaultPresetId) &&
            steps.first().toObject().value(QStringLiteral("type")).toString() == QLatin1String("wait"))
            return fail(error, QStringLiteral("Default nesmí začínat krokem Čekat."));
        for (const auto &step : steps) {
            if (!step.isObject() || !validStep(step.toObject()))
                return fail(error, QStringLiteral("Neplatný krok v presetu: %1").arg(id));
        }
    }

    QSet<QString> ruleIds;
    for (const auto &entry : config.rules) {
        if (!entry.isObject() || !validIdentifier(entry.toObject()))
            return fail(error, QStringLiteral("Neplatné pravidlo."));
        const QJsonObject rule = entry.toObject();
        const QString id = rule.value(QStringLiteral("id")).toString();
        if (ruleIds.contains(id) || !rule.value(QStringLiteral("enabled")).isBool() ||
            !rule.value(QStringLiteral("match")).isObject() ||
            !rule.value(QStringLiteral("preset_id")).isString())
            return fail(error, QStringLiteral("Neplatná struktura pravidla: %1").arg(id));
        ruleIds.insert(id);
        if (!ids.contains(rule.value(QStringLiteral("preset_id")).toString()))
            return fail(error, QStringLiteral("Pravidlo odkazuje na neexistující preset: %1").arg(id));
        const QJsonObject match = rule.value(QStringLiteral("match")).toObject();
        const auto app = match.value(QStringLiteral("app_contains"));
        const auto title = match.value(QStringLiteral("title_contains"));
        const auto body = match.value(QStringLiteral("text_contains"));
        if (!app.isString() || !title.isString() || !body.isString() ||
            (app.toString().isEmpty() && title.toString().isEmpty() && body.toString().isEmpty()))
            return fail(error, QStringLiteral("Pravidlo vyžaduje alespoň jednu podmínku: %1").arg(id));
        const int cooldown = rule.value(QStringLiteral("cooldown_ms")).toInt(-1);
        if (cooldown < 0 || cooldown > 3600000)
            return fail(error, QStringLiteral("Neplatný cooldown pravidla: %1").arg(id));
    }
    return true;
}

QJsonObject presetById(const Configuration &config, const QString &id)
{
    for (const auto &entry : config.presets) {
        const auto preset = entry.toObject();
        if (preset.value(QStringLiteral("id")).toString() == id)
            return preset;
    }
    return {};
}

bool savePresets(const Configuration &config, QString *error)
{
    if (!validate(config, error))
        return false;
    return saveFile(presetsFile(), {{QStringLiteral("version"), 1},
                                    {QStringLiteral("presets"), config.presets}}, error);
}

bool saveRules(const Configuration &config, QString *error)
{
    if (!validate(config, error))
        return false;
    return saveFile(rulesFile(), {{QStringLiteral("version"), 1},
                                  {QStringLiteral("enabled"), config.enabled},
                                  {QStringLiteral("backend"), config.backend},
                                  {QStringLiteral("rules"), config.rules}}, error);
}

} // namespace KEncoderNotify
