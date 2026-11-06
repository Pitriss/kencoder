// SPDX-License-Identifier: BSD-3-Clause
// Copyright 2026 Pitriss
#pragma once

#include <QString>

namespace KEncoderSession {
struct Policy {
    bool offOnLock = false;
    bool offOnScreenOff = false;
    bool notifyWhenInactive = false;
};

QString policyFile();
bool loadPolicy(Policy *out, QString *error = nullptr);
bool savePolicy(const Policy &policy, QString *error = nullptr);
}
