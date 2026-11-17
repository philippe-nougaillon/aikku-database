#include "cli.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTextStream>

#include "backend.h"

namespace {
const char *kBundledSkill = R"SKILL(---
name: aikku
description: |
  Manage custom SQLite tables, attributes, and records with Aikku (a desktop
  tables manager inspired by Aikku Access). Use when creating, inspecting,
  importing, or exporting custom data tables or CSV collections.
---

# Aikku

Aikku stores custom collections (tables, typed attributes, and records) in a
single SQLite database (`~/.local/share/aikku/aikku.sqlite` by default) and can
import/export CSV files without needing a display.

## Commands

- `aikku tables [--db <path>] [--json]` — List all tables, field counts, and record counts.
- `aikku check [--db <path>] [--json]` — Validate tables, required fields, and formulas.
- `aikku import <file.csv> [--table <name>] [--db <path>] [--json]` — Import a CSV file into a new or existing table.
- `aikku export <table-name> <output.csv> [--db <path>] [--json]` — Export a table to CSV.
- `aikku open [database.sqlite|file.csv]` — Open the desktop GUI.
)SKILL";

void printError(const QString &msg, bool json) {
    if (json) {
        QJsonObject obj;
        obj.insert(QStringLiteral("ok"), false);
        obj.insert(QStringLiteral("error"), msg);
        QTextStream(stderr) << QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)) << "\n";
    } else {
        QTextStream(stderr) << "aikku: " << msg << "\n";
    }
}
} // namespace

namespace Cli {

bool isHeadlessCommand(int argc, char *argv[]) {
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == QLatin1String("--screenshot"))
            return true;
    }
    if (argc < 2)
        return false;
    const QString first = QString::fromLocal8Bit(argv[1]);
    return first == QLatin1String("tables") ||
           first == QLatin1String("check") ||
           first == QLatin1String("import") ||
           first == QLatin1String("export") ||
           first == QLatin1String("skill") ||
           first == QLatin1String("help");
}

int run(const QStringList &args) {
    if (args.size() < 2)
        return 1;

    const QString cmd = args.at(1);
    bool json = args.contains(QStringLiteral("--json"));
    QString dbPath;
    for (int i = 2; i < args.size(); ++i) {
        if (args.at(i) == QLatin1String("--db") && i + 1 < args.size()) {
            dbPath = args.at(i + 1);
            break;
        }
    }

    if (cmd == QLatin1String("help")) {
        QTextStream out(stdout);
        out << "Usage: aikku [command] [options]\n\n"
            << "Editor:\n"
            << "  open [file.sqlite|file.csv]          Open the desktop application\n\n"
            << "Headless commands:\n"
            << "  tables [--db <path>] [--json]        List tables in the database\n"
            << "  check [--db <path>] [--json]         Check required fields and formulas\n"
            << "  import <csv> [--db <path>] [--json]  Import a CSV file as a table\n"
            << "  export <table> <csv> [--db <path>]   Export a table to CSV\n"
            << "  skill [install]                      Print or install the agent skill\n";
        return 0;
    }

    if (cmd == QLatin1String("skill")) {
        if (args.size() >= 3 && args.at(2) == QLatin1String("install")) {
            const QString targetDir = QDir::homePath() + QStringLiteral("/.config/omarchy/skills/aikku");
            QDir().mkpath(targetDir);
            const QString targetFile = targetDir + QStringLiteral("/SKILL.md");
            QSaveFile sf(targetFile);
            if (!sf.open(QIODevice::WriteOnly | QIODevice::Text) ||
                sf.write(kBundledSkill) < 0 || !sf.commit()) {
                printError(QStringLiteral("Could not write skill to %1").arg(targetFile), json);
                return 1;
            }
            if (json) {
                QJsonObject obj{{QStringLiteral("ok"), true}, {QStringLiteral("path"), targetFile}};
                QTextStream(stdout) << QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)) << "\n";
            } else {
                QTextStream(stdout) << "Installed skill to " << targetFile << "\n";
            }
            return 0;
        }
        QTextStream(stdout) << kBundledSkill;
        return 0;
    }

    Backend backend(dbPath);
    if (!backend.errorMessage().isEmpty()) {
        printError(backend.errorMessage(), json);
        return 1;
    }

    if (cmd == QLatin1String("tables")) {
        const QVariantList tableList = backend.tables();
        if (json) {
            QJsonArray arr;
            for (const QVariant &v : tableList)
                arr.append(QJsonObject::fromVariantMap(v.toMap()));
            QJsonObject root{{QStringLiteral("ok"), true},
                             {QStringLiteral("database"), backend.databasePath()},
                             {QStringLiteral("tables"), arr}};
            QTextStream(stdout) << QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)) << "\n";
        } else {
            QTextStream out(stdout);
            for (const QVariant &v : tableList) {
                const QVariantMap m = v.toMap();
                out << m.value(QStringLiteral("name")).toString()
                    << " (" << m.value(QStringLiteral("recordCount")).toInt() << " records, "
                    << m.value(QStringLiteral("fieldCount")).toInt() << " attributes)\n";
            }
        }
        return 0;
    }

    if (cmd == QLatin1String("check")) {
        QJsonArray issues;
        const QVariantList tableList = backend.tables();
        for (const QVariant &tv : tableList) {
            const QVariantMap tmap = tv.toMap();
            const int tId = tmap.value(QStringLiteral("id")).toInt();
            const QString tName = tmap.value(QStringLiteral("name")).toString();
            backend.selectTable(tId);
            const QVariantList fList = backend.fields();
            const QVariantList rList = backend.records();
            for (const QVariant &rv : rList) {
                const QVariantMap rmap = rv.toMap();
                const int recIdx = rmap.value(QStringLiteral("recordIndex")).toInt();
                const QVariantList cells = rmap.value(QStringLiteral("cells")).toList();
                for (int i = 0; i < cells.size() && i < fList.size(); ++i) {
                    const QVariantMap fmap = fList.at(i).toMap();
                    const QVariantMap cmap = cells.at(i).toMap();
                    const QString raw = cmap.value(QStringLiteral("rawValue")).toString();
                    if (fmap.value(QStringLiteral("obligatoire")).toBool() && raw.trimmed().isEmpty()) {
                        issues.append(QJsonObject{
                            {QStringLiteral("table"), tName},
                            {QStringLiteral("recordIndex"), recIdx},
                            {QStringLiteral("field"), fmap.value(QStringLiteral("name")).toString()},
                            {QStringLiteral("problem"), QStringLiteral("Missing required value")},
                        });
                    }
                    if (fmap.value(QStringLiteral("datatype")).toString() == QLatin1String("Formula") &&
                        raw == QLatin1String("err")) {
                        issues.append(QJsonObject{
                            {QStringLiteral("table"), tName},
                            {QStringLiteral("recordIndex"), recIdx},
                            {QStringLiteral("field"), fmap.value(QStringLiteral("name")).toString()},
                            {QStringLiteral("problem"), QStringLiteral("Invalid formula evaluation")},
                        });
                    }
                }
            }
        }
        const bool ok = issues.isEmpty();
        if (json) {
            QJsonObject root{{QStringLiteral("ok"), ok},
                             {QStringLiteral("issues"), issues}};
            QTextStream(stdout) << QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)) << "\n";
        } else if (!ok) {
            for (const QJsonValue &val : issues) {
                const QJsonObject o = val.toObject();
                QTextStream(stderr) << o.value(QStringLiteral("table")).toString()
                                    << " #" << o.value(QStringLiteral("recordIndex")).toInt()
                                    << " [" << o.value(QStringLiteral("field")).toString() << "]: "
                                    << o.value(QStringLiteral("problem")).toString() << "\n";
            }
        } else {
            QTextStream(stdout) << "OK (" << tableList.size() << " tables checked)\n";
        }
        return ok ? 0 : 1;
    }

    if (cmd == QLatin1String("import")) {
        QString csvPath;
        for (int i = 2; i < args.size(); ++i) {
            const QString a = args.at(i);
            if (a == QLatin1String("--db")) {
                ++i;
                continue;
            }
            if (a.startsWith(QLatin1String("--")))
                continue;
            csvPath = a;
            break;
        }
        if (csvPath.isEmpty()) {
            printError(QStringLiteral("Usage: aikku import <file.csv> [--db <path>] [--json]"), json);
            return 1;
        }
        if (!backend.importCsvFile(csvPath, 0)) {
            printError(backend.errorMessage(), json);
            return 1;
        }
        if (json) {
            QJsonObject root{{QStringLiteral("ok"), true},
                             {QStringLiteral("table"), backend.currentTableName()},
                             {QStringLiteral("records"), backend.recordCount()}};
            QTextStream(stdout) << QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)) << "\n";
        } else {
            QTextStream(stdout) << backend.noticeMessage() << "\n";
        }
        return 0;
    }

    if (cmd == QLatin1String("export")) {
        QStringList positional;
        for (int i = 2; i < args.size(); ++i) {
            const QString a = args.at(i);
            if (a == QLatin1String("--db")) {
                ++i;
                continue;
            }
            if (a.startsWith(QLatin1String("--")))
                continue;
            positional.append(a);
        }
        if (positional.size() < 2) {
            printError(QStringLiteral("Usage: aikku export <table-name> <output.csv> [--db <path>] [--json]"), json);
            return 1;
        }
        const QString tableName = positional.at(0);
        const QString outPath = positional.at(1);
        int foundId = 0;
        for (const QVariant &tv : backend.tables()) {
            const QVariantMap m = tv.toMap();
            if (m.value(QStringLiteral("name")).toString().compare(tableName, Qt::CaseInsensitive) == 0) {
                foundId = m.value(QStringLiteral("id")).toInt();
                break;
            }
        }
        if (foundId <= 0) {
            printError(QStringLiteral("Table \"%1\" not found.").arg(tableName), json);
            return 1;
        }
        backend.selectTable(foundId);
        if (!backend.exportCsvFile(outPath)) {
            printError(backend.errorMessage(), json);
            return 1;
        }
        if (json) {
            QJsonObject root{{QStringLiteral("ok"), true},
                             {QStringLiteral("table"), backend.currentTableName()},
                             {QStringLiteral("output"), QFileInfo(outPath).absoluteFilePath()},
                             {QStringLiteral("records"), backend.recordCount()}};
            QTextStream(stdout) << QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)) << "\n";
        } else {
            QTextStream(stdout) << backend.noticeMessage() << "\n";
        }
        return 0;
    }

    printError(QStringLiteral("Unknown command: %1").arg(cmd), json);
    return 1;
}

} // namespace Cli
