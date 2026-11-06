// SPDX-License-Identifier: BSD-3-Clause
// Copyright 2026 Pitriss
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace KEncoderNotify {

inline constexpr const char *kWatcherBackend = "kde-watcher";
inline constexpr const char *kFutureBackend = "knotify-scanner";
inline constexpr const char *kDefaultPresetId = "default";

struct Configuration {
    bool enabled = false; // Never listen by default.
    QString backend = QString::fromLatin1(kWatcherBackend);
    QJsonArray presets;
    QJsonArray rules;
};

QString configDirectory();
QString presetsFile();
QString rulesFile();

// Missing files mean default empty configuration. Malformed files fail closed.
bool load(Configuration *out, QString *error);
bool savePresets(const Configuration &config, QString *error);
bool saveRules(const Configuration &config, QString *error);

// Validate every record; no fixed count limits for presets, rules or steps.
bool validate(const Configuration &config, QString *error);
QJsonObject presetById(const Configuration &config, const QString &id);

} // namespace KEncoderNotify
