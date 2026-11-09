// SPDX-License-Identifier: BSD-3-Clause
// Copyright 2026 Pitriss
#include <QCoreApplication>
#include "session_policy.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

namespace KEncoderSession {
namespace {
bool fail(QString *error, const QString &reason)
{
    if (error) *error = reason;
    return false;
}
}

QString policyFile()
{
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) +
           QStringLiteral("/kencoder/session-policy.json");
}

bool loadPolicy(Policy *out, QString *error)
{
    if (!out) return fail(error, QCoreApplication::translate("KEncoderSessionErrors", "Není kam načíst nastavení relace."));
    Policy fresh; // Missing configuration is deliberately inert.
    QFile file(policyFile());
    if (!file.exists()) {
        *out = fresh;
        return true;
    }
    if (!file.open(QIODevice::ReadOnly))
        return fail(error, QCoreApplication::translate("KEncoderSessionErrors", "Nelze číst nastavení relace."));
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) return fail(error, QCoreApplication::translate("KEncoderSessionErrors", "Neplatný JSON nastavení relace."));
    const QJsonObject data = doc.object();
    if (data.value(QStringLiteral("version")).toInt(-1) != 1 ||
        !data.value(QStringLiteral("off_on_lock")).isBool() ||
        !data.value(QStringLiteral("off_on_screen_off")).isBool() ||
        !data.value(QStringLiteral("notify_when_inactive")).isBool())
        return fail(error, QCoreApplication::translate("KEncoderSessionErrors", "Nepodporovaná konfigurace relace."));
    fresh.offOnLock = data.value(QStringLiteral("off_on_lock")).toBool();
    fresh.offOnScreenOff = data.value(QStringLiteral("off_on_screen_off")).toBool();
    fresh.notifyWhenInactive = data.value(QStringLiteral("notify_when_inactive")).toBool();
    *out = fresh;
    return true;
}

bool savePolicy(const Policy &policy, QString *error)
{
    const QFileInfo target(policyFile());
    if (!QDir().mkpath(target.absolutePath()))
        return fail(error, QCoreApplication::translate("KEncoderSessionErrors", "Nelze vytvořit adresář nastavení relace."));
    QSaveFile file(target.absoluteFilePath());
    if (!file.open(QIODevice::WriteOnly))
        return fail(error, QCoreApplication::translate("KEncoderSessionErrors", "Nelze otevřít nastavení relace pro zápis."));
    const QJsonObject data{
        {QStringLiteral("version"), 1},
        {QStringLiteral("off_on_lock"), policy.offOnLock},
        {QStringLiteral("off_on_screen_off"), policy.offOnScreenOff},
        {QStringLiteral("notify_when_inactive"), policy.notifyWhenInactive}
    };
    const QByteArray payload = QJsonDocument(data).toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size() || !file.commit())
        return fail(error, QCoreApplication::translate("KEncoderSessionErrors", "Nelze dokončit uložení nastavení relace."));
    return true;
}
} // namespace KEncoderSession
