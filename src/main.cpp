// aikku — SQLite custom tables manager inspired by Aikku Access.

#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>

#include "backend.h"
#include "cli.h"
#include "portalfilepicker.h"
#include "theme.h"
#include "translator.h"

int main(int argc, char *argv[]) {
    const bool headless = Cli::isHeadlessCommand(argc, argv);
    if (headless)
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));

    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("aikku"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));

    const QStringList args = app.arguments();
    if (args.size() >= 2 &&
        (args.at(1) == QLatin1String("tables") ||
         args.at(1) == QLatin1String("check") ||
         args.at(1) == QLatin1String("import") ||
         args.at(1) == QLatin1String("export") ||
         args.at(1) == QLatin1String("skill") ||
         args.at(1) == QLatin1String("help"))) {
        return Cli::run(args);
    }

    // Matches the window to aikku.desktop, so the compositor (Wayland app_id)
    // and the launcher pick up the installed icon.
    app.setDesktopFileName(QStringLiteral("aikku"));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("aikku")));

    QQuickStyle::setStyle(QStringLiteral("Material"));

    // Parse optional GUI flags: [open] [file.sqlite|file.csv], --db <path>, --demo, --screenshot <file>.
    QString initialDbPath;
    QString initialCsvPath;
    QString screenshotPath;
    bool loadDemo = false;

    for (int i = 1; i < args.size(); ++i) {
        const QString a = args.at(i);
        if (a == QLatin1String("open")) {
            continue;
        } else if (a == QLatin1String("--db") && i + 1 < args.size()) {
            initialDbPath = args.at(++i);
        } else if (a == QLatin1String("--screenshot") && i + 1 < args.size()) {
            screenshotPath = args.at(++i);
        } else if (a == QLatin1String("--demo")) {
            loadDemo = true;
        } else if (!a.startsWith(QLatin1String("-"))) {
            if (a.endsWith(QLatin1String(".csv"), Qt::CaseInsensitive))
                initialCsvPath = a;
            else
                initialDbPath = a;
        }
    }

    // Installed before anything is translated, and every qsTr() binding is
    // re-evaluated when the Omarchy language changes.
    Translator translator;
    app.installTranslator(&translator);

    // When taking a README screenshot offscreen, use a non-existent theme path
    // if --demo is passed so the default #FFD60A accent is used reproducibly.
    Theme theme(loadDemo && !screenshotPath.isEmpty() ? QStringLiteral("/nonexistent-omarchy-theme") : QString());
    PortalFilePicker picker;
    Backend backend(initialDbPath, &picker);

    if (loadDemo && backend.tableCount() == 0) {
        backend.createSampleTable();
        backend.clearMessages();
    }
    if (!initialCsvPath.isEmpty())
        backend.importCsvFile(initialCsvPath, 0);

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty(QStringLiteral("translator"), &translator);
    engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;

    if (!screenshotPath.isEmpty()) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        if (!window)
            return 1;
        QTimer::singleShot(250, &app, [window, screenshotPath, &app]() {
            const QImage img = window->grabWindow();
            img.save(screenshotPath);
            app.quit();
        });
    }

    return app.exec();
}
