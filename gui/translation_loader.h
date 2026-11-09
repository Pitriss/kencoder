// SPDX-License-Identifier: BSD-3-Clause
#pragma once

namespace KEncoderTranslation {
// Installs the best matching kencoder_<locale>.qm from user/system data paths.
// Call once before constructing any translatable Widgets or QML objects.
void install();
}
