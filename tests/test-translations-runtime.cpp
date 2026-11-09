// SPDX-License-Identifier: BSD-3-Clause
#include "../gui/translation_loader.h"

#include <QCoreApplication>
#include <QDebug>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QUrl>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    KEncoderTranslation::install();

    const QString requestedLanguage = qEnvironmentVariable("KENCODER_LANG").left(2).toLower();
    const bool ukrainian = requestedLanguage == QLatin1String("ua") ||
                           requestedLanguage == QLatin1String("uk");
    const QString expectedStatic = ukrainian ? QStringLiteral("Статичне")
                                             : QStringLiteral("Statisch");
    const QString expectedSaved = ukrainian ? QStringLiteral("Пресет збережено.")
                                            : QStringLiteral("Voreinstellung gespeichert.");
    const QString expectedNewPreset = ukrainian ? QStringLiteral("Новий пресет")
                                                : QStringLiteral("Neue Voreinstellung");

    const QString cxx = QCoreApplication::translate("KEncoder", "Statick\xC3\xA9");
    if (cxx != expectedStatic) {
        qCritical().noquote() << "FAIL: C++ translator not loaded; got:" << cxx;
        return 1;
    }

    // The notification status message belongs to a C++ KCM context, not QML.
    const QString notify = QCoreApplication::translate(
        "KEncoderNotifyKcm", "Preset ulo\u017Een.");
    if (notify != expectedSaved) {
        qCritical().noquote() << "FAIL: Notify KCM C++ translation missing; got:" << notify;
        return 2;
    }

    // In the actual Notify main.qml, the preset editor uses this qsTr key.
    // Checking the catalog separately distinguishes a missing context from
    // a QML engine integration error.
    const QString qmlCatalog = QCoreApplication::translate("main", "Nov\u00FD preset");
    if (qmlCatalog != expectedNewPreset) {
        qCritical().noquote() << "FAIL: QML main catalog context missing; got:" << qmlCatalog;
        return 3;
    }

    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(
        QByteArray("import QtQml\nQtObject { property string label: qsTr(\"Nov\\u00fd preset\") }\n"),
        QUrl(QStringLiteral("file:///main.qml")));
    QObject *object = component.create();
    if (!object) {
        qCritical().noquote() << "FAIL: QML translation test component:" << component.errorString();
        return 4;
    }
    const QString qml = object->property("label").toString();
    delete object;
    if (qml != expectedNewPreset) {
        qCritical().noquote() << "FAIL: QML translator not loaded; got:" << qml;
        return 5;
    }

    qInfo().noquote() << "PASS: C++ and QML translations loaded at runtime for"
                      << (ukrainian ? "Ukrainian (uk/ua)" : "German");
    return 0;
}
