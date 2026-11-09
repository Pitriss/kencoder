// SPDX-License-Identifier: BSD-3-Clause
#include "translation_loader.h"
#include <QCoreApplication>
#include <QDir>
#include <QLocale>
#include <QStandardPaths>
#include <QStringList>
#include <QTranslator>
#include <QSet>

namespace KEncoderTranslation {
void install()
{
    auto *app = QCoreApplication::instance();
    if (!app) return;
    static QTranslator *installed = nullptr;
    if (installed) return;
    QStringList roots;
    const QString overrideRoot = qEnvironmentVariable("KENCODER_TRANSLATIONS_DIR");
    if (!overrideRoot.isEmpty()) roots.append(overrideRoot);
    for (const QString &data : QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation))
        roots.append(QDir(data).filePath(QStringLiteral("kencoder/translations")));
    // During a non-installed local build the translations are adjacent to the GUI.
    roots.append(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("translations")));

    const QString explicitLanguage = qEnvironmentVariable("KENCODER_LANG");
    QStringList choices = explicitLanguage.isEmpty()
        ? QLocale::system().uiLanguages() : QStringList{explicitLanguage};
    if (choices.isEmpty()) choices.append(QLocale::system().name());
    QSet<QString> seen;
    for (const QString &choice : choices) {
        // ISO 639-1 uses "uk" for Ukrainian; "ua" is the country code.
        // Accept "ua" as a compatibility alias for KENCODER_LANG=ua without
        // duplicating PO/QM catalogs or using nonstandard KDE locale keys.
        const QString requested = choice.left(2).toLower();
        const QString language = requested == QLatin1String("ua")
            ? QStringLiteral("uk") : requested;
        if (language == QLatin1String("cs")) return; // Source language; no QM necessary.
        if (seen.contains(language)) continue;
        seen.insert(language);
        for (const QString &root : roots) {
            auto *candidate = new QTranslator(app);
            // The build creates language-only names such as kencoder_de.qm.
            // QTranslator's QLocale overload may instead look for a territory
            // variant (for example kencoder_de_DE.qm), so load the exact file.
            const QString catalogPath = QDir(root).filePath(
                QStringLiteral("kencoder_%1.qm").arg(language));
            if (candidate->load(catalogPath)) {
                QCoreApplication::installTranslator(candidate);
                installed = candidate;
                return;
            }
            delete candidate;
        }
    }
}
} // namespace KEncoderTranslation
