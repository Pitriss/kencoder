// SPDX-License-Identifier: BSD-3-Clause
// Copyright 2026 Pitriss
#pragma once

// Common, side-effect-free parser for the encoder --status --json contract.
// This header is QtCore-only: neither UI requires KF6 to parse status.
#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

struct EncoderLightingStatus
{
    int brightness = -1;
    int effectCode = -1;
    bool enabled = false;
    QString mode;

    QString description() const
    {
        QString effect;
        if (mode == QLatin1String("off"))
            effect = QStringLiteral("Vypnuto");
        else if (mode == QLatin1String("static"))
            effect = QStringLiteral("Statické");
        else if (mode == QLatin1String("breathe"))
            effect = QStringLiteral("Dýchání");
        else if (mode == QLatin1String("ripple"))
            effect = QStringLiteral("Reakce na stisk");
        else if (mode == QLatin1String("wave"))
            effect = QStringLiteral("Vlna (směr nezjištěn)");
        else
            effect = QStringLiteral("Neznámý režim");

        return QStringLiteral("Z klávesnice: %1, jas %2. Rychlost nezjištěna.")
            .arg(effect).arg(brightness);
    }
};

inline bool parseEncoderLightingStatus(const QByteArray &payload, EncoderLightingStatus *out)
{
    if (!out)
        return false;
    QJsonParseError jsonError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &jsonError);
    if (jsonError.error != QJsonParseError::NoError || !document.isObject())
        return false;
    const QJsonObject value = document.object();
    if (value.value(QStringLiteral("source")).toString() != QLatin1String("hardware") ||
        !value.value(QStringLiteral("brightness")).isDouble() ||
        !value.value(QStringLiteral("effect_code")).isDouble() ||
        !value.value(QStringLiteral("mode")).isString())
        return false;

    const int brightness = value.value(QStringLiteral("brightness")).toInt(-1);
    const int effect = value.value(QStringLiteral("effect_code")).toInt(-1);
    if (brightness < 0 || brightness > 3)
        return false;

    const QString mode = value.value(QStringLiteral("mode")).toString();
    const bool known = (effect == 0 && mode == QLatin1String("off")) ||
                       (effect == 1 && mode == QLatin1String("static")) ||
                       (effect == 2 && mode == QLatin1String("breathe")) ||
                       (effect == 9 && mode == QLatin1String("ripple")) ||
                       (effect == 10 && mode == QLatin1String("wave"));
    const auto enabled = value.value(QStringLiteral("enabled"));
    if (!known || !enabled.isBool() || enabled.toBool() != (effect != 0))
        return false; // Do not turn an unknown effect into an invented UI state.

    out->brightness = brightness;
    out->effectCode = effect;
    out->enabled = enabled.toBool();
    out->mode = mode;
    return true;
}
