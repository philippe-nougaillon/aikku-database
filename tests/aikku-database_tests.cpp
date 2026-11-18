#include <QDir>
#include <QFile>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>

#include "backend.h"
#include "filepicker.h"
#include "theme.h"
#include "translator.h"

class FakeFilePicker : public FilePicker {
    Q_OBJECT

public:
    explicit FakeFilePicker(QObject *parent = nullptr) : FilePicker(parent) {}

    QUrl nextCsvOpenUrl;
    QUrl nextCsvExportUrl;
    QUrl nextDbOpenUrl;

    void openCsv() override {
        if (nextCsvOpenUrl.isValid())
            emit csvOpenSelected(nextCsvOpenUrl);
    }
    void exportCsv(const QUrl &suggestedUrl) override {
        emit csvExportSelected(nextCsvExportUrl.isValid() ? nextCsvExportUrl : suggestedUrl);
    }
    void openDatabase() override {
        if (nextDbOpenUrl.isValid())
            emit databaseOpenSelected(nextDbOpenUrl);
    }
};

class AikkuTests : public QObject {
    Q_OBJECT

private slots:
    void primaryActionCounts() {
        QTemporaryDir dir;
        Backend backend(dir.filePath("test.sqlite"));
        QCOMPARE(backend.count(), 0);
        backend.primaryAction();
        QCOMPARE(backend.count(), 1);
    }

    void tableAndFieldCrud() {
        QTemporaryDir dir;
        Backend backend(dir.filePath("test.sqlite"));

        const int tId = backend.createTable("Clients", true);
        QVERIFY(tId > 0);
        QCOMPARE(backend.tableCount(), 1);
        QCOMPARE(backend.currentTableName(), QString("Clients"));

        // Invalid Status field without colon is rejected.
        QCOMPARE(backend.addField("State", "Status", "Open, Closed"), 0);
        QVERIFY(!backend.errorMessage().isEmpty());

        const int fName = backend.addField("Name", "Text", "", true, false);
        const int fState = backend.addField("State", "Status", "Active:green, Prospect:blue", false, true);
        const int fRevenue = backend.addField("Revenue", "Euros", "", false, false, "Sum");
        QVERIFY(fName > 0 && fState > 0 && fRevenue > 0);
        QCOMPARE(backend.fieldCount(), 3);

        // Reorder field 3 up.
        QVERIFY(backend.moveField(fRevenue, -1));
        QCOMPARE(backend.fields().at(1).toMap().value("id").toInt(), fRevenue);

        // Rename table.
        QVERIFY(backend.updateTable(tId, "Partners", false));
        QCOMPARE(backend.currentTableName(), QString("Partners"));
        QCOMPARE(backend.currentTableLifo(), false);

        // Add records to Partners table.
        QVariantMap p1;
        p1.insert(QString::number(fName), "Acme Corp");
        QCOMPARE(backend.saveRecord(0, p1), 1);
        QVariantMap p2;
        p2.insert(QString::number(fName), "Beta Studio");
        QCOMPARE(backend.saveRecord(0, p2), 2);

        // Create an Orders table with a Relation attribute pointing to Partners:Name.
        const int ordersId = backend.createTable("Orders", true);
        QVERIFY(ordersId > 0);
        // Invalid relation to non-existent table fails.
        QCOMPARE(backend.addField("Client", "Relation", "MissingTable"), 0);
        QVERIFY(!backend.errorMessage().isEmpty());

        const int fClientRel = backend.addField("Client", "Relation", "Partners:Name", true, true);
        QVERIFY(fClientRel > 0);
        const QStringList relChoices = backend.fieldDetails(fClientRel).value("choices").toStringList();
        QCOMPARE(relChoices, (QStringList{"Acme Corp", "Beta Studio"}));

        QVariantMap order1;
        order1.insert(QString::number(fClientRel), "Acme Corp");
        QCOMPARE(backend.saveRecord(0, order1), 1);
        QCOMPARE(backend.recordValues(1).value(QString::number(fClientRel)).toString(), QString("Acme Corp"));
    }

    void recordsFormulasFiltersSortingAndAggregations() {
        QTemporaryDir dir;
        Backend backend(dir.filePath("test.sqlite"));

        QVERIFY(backend.createTable("Orders", true) > 0);
        const int fItem = backend.addField("Item", "Text", "", true, false);
        const int fStatus = backend.addField("Status", "Status", "Paid:green, Pending:yellow", false, true);
        const int fQty = backend.addField("Qty", "Number", "", true, false, "Sum");
        const int fPrice = backend.addField("Price", "Euros", "", true, false, "Average");
        const int fTotal = backend.addField("Total", "Formula", "[Qty] * [Price]", false, false, "Sum");
        const int fDate = backend.addField("Order Date", "Date", "", false, true);

        // Missing required field fails.
        QVariantMap badRow;
        badRow.insert(QString::number(fStatus), "Paid");
        QCOMPARE(backend.saveRecord(0, badRow), 0);
        QVERIFY(!backend.errorMessage().isEmpty());

        QVariantMap row1;
        row1.insert(QString::number(fItem), "Desk");
        row1.insert(QString::number(fStatus), "Paid");
        row1.insert(QString::number(fQty), "2");
        row1.insert(QString::number(fPrice), "250");
        row1.insert(QString::number(fDate), "2026-11-01");
        const int r1 = backend.saveRecord(0, row1);
        QCOMPARE(r1, 1);

        QVariantMap row2;
        row2.insert(QString::number(fItem), "Chair");
        row2.insert(QString::number(fStatus), "Pending");
        row2.insert(QString::number(fQty), "4");
        row2.insert(QString::number(fPrice), "125");
        row2.insert(QString::number(fDate), "2026-11-15");
        const int r2 = backend.saveRecord(0, row2);
        QCOMPARE(r2, 2);

        QCOMPARE(backend.recordCount(), 2);
        QVERIFY(backend.hasFooterRow());

        // Check formula evaluation (2 * 250 = 500, 4 * 125 = 500 -> Total Sum = 1000).
        const QVariantMap totalField = backend.fieldDetails(fTotal);
        QCOMPARE(totalField.value("footerValue").toDouble(), 1000.0);

        // Filter by Status = Paid.
        backend.setFieldFilter(fStatus, "Paid");
        QCOMPARE(backend.recordCount(), 1);
        QCOMPARE(backend.fieldDetails(fTotal).value("footerValue").toDouble(), 500.0);
        backend.clearFilters();
        QCOMPARE(backend.recordCount(), 2);

        // Date range filter.
        backend.setDateFilter(fDate, "2026-11-10", "2026-11-20");
        QCOMPARE(backend.recordCount(), 1);
        backend.clearFilters();

        // Search filter.
        backend.setSearchText("desk");
        QCOMPARE(backend.recordCount(), 1);
        backend.clearFilters();

        // Sort by Qty ascending / descending.
        backend.toggleSort(fQty);
        QCOMPARE(backend.records().first().toMap().value("recordIndex").toInt(), r1);
        backend.toggleSort(fQty);
        QCOMPARE(backend.records().first().toMap().value("recordIndex").toInt(), r2);
    }

    void csvImportAutoDetectionAndExport() {
        QTemporaryDir dir;
        const QString csvIn = dir.filePath("Contacts.csv");
        write(csvIn,
              "FullName,Visits,LastSeen,Subscribed\n"
              "Alice Martin,12,2026-10-05,true\n"
              "Bob Dupont,4,2026-11-12,non\n");

        FakeFilePicker picker;
        picker.nextCsvOpenUrl = QUrl::fromLocalFile(csvIn);
        const QString csvOut = dir.filePath("Exported.csv");
        picker.nextCsvExportUrl = QUrl::fromLocalFile(csvOut);

        Backend backend(dir.filePath("test.sqlite"), &picker);
        backend.importCsvDialog(0);

        QCOMPARE(backend.tableCount(), 1);
        QCOMPARE(backend.currentTableName(), QString("Contacts"));
        QCOMPARE(backend.fieldCount(), 4);
        QCOMPARE(backend.fields().at(0).toMap().value("datatype").toString(), QString("Text"));
        QCOMPARE(backend.fields().at(1).toMap().value("datatype").toString(), QString("Number"));
        QCOMPARE(backend.fields().at(2).toMap().value("datatype").toString(), QString("Date"));
        QCOMPARE(backend.fields().at(3).toMap().value("datatype").toString(), QString("YesNo"));
        QCOMPARE(backend.recordCount(), 2);

        backend.exportCsvDialog();
        QVERIFY(QFile::exists(csvOut));
        QFile exported(csvOut);
        QVERIFY(exported.open(QIODevice::ReadOnly));
        const QString outText = QString::fromUtf8(exported.readAll());
        QVERIFY(outText.contains("FullName,Visits,LastSeen,Subscribed"));
        QVERIFY(outText.contains("Alice Martin,12,2026-10-05,true"));
    }

    void undoAndRedo() {
        QTemporaryDir dir;
        Backend backend(dir.filePath("test.sqlite"));

        backend.createTable("Tasks", true);
        const int fTitle = backend.addField("Title", "Text", "", true);
        QVariantMap vals;
        vals.insert(QString::number(fTitle), "Ship release");
        backend.saveRecord(0, vals);
        QCOMPARE(backend.recordCount(), 1);
        QVERIFY(backend.canUndo());

        backend.undo();
        QCOMPARE(backend.recordCount(), 0);
        QVERIFY(backend.canRedo());

        backend.redo();
        QCOMPARE(backend.recordCount(), 1);
    }

    void themeFallsBack() {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
    }

    void themeReadsAccent() {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        QFile colors(dir.path() + "/theme/colors.toml");
        QVERIFY(colors.open(QIODevice::WriteOnly));
        colors.write("accent = \"#112233\"\n");
        colors.close();

        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
    }

    void themeFollowsHyprlandRounding() {
        QTemporaryDir dir;
        const QString response = dir.filePath("response.json"), command = dir.filePath("hyprctl");
        auto writeExec = [](const QString &path, const QByteArray &data) {
            QFile file(path);
            return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
        };
        QVERIFY(writeExec(command, "#!/bin/sh\ncat '" + response.toUtf8() + "'\n"));
        QVERIFY(QFile::setPermissions(command, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        QVERIFY(writeExec(response, "{\"int\":0}"));

        Theme theme(dir.path(), nullptr, command);
        QCOMPARE(theme.radius(), 0);
        QVERIFY(writeExec(response, "{\"int\":6}"));
        QTRY_COMPARE(theme.radius(), 6);
    }

    void translatorFallsBackToEnglish() {
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("en"));
        QVERIFY(translator.translate("Main", "Save").isNull());
    }

    void translatorFollowsTheLanguageFile() {
        QTemporaryDir dir;
        write(dir.path() + "/language", "pt-BR\n");
        write(dir.path() + "/pt.json", R"({"Save": "Guardar", "Open": "Abrir"})");
        write(dir.path() + "/pt-BR.json", R"({"Save": "Salvar"})");

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("pt-BR"));
        QCOMPARE(translator.translate("Main", "Save"), QString("Salvar"));
        QCOMPARE(translator.translate("Main", "Open"), QString("Abrir"));
        QVERIFY(translator.translate("Main", "Quit").isNull());
    }

    void translatorPicksPluralForms() {
        QTemporaryDir dir;
        write(dir.path() + "/language", "ru\n");
        write(dir.path() + "/en.json", R"({"%n records": {"one": "%n record", "other": "%n records"}})");
        write(dir.path() + "/ru.json", R"({"%n records": {"one": "%n запись", "few": "%n записи", "many": "%n записей", "other": "%n записи"}})");

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.translate("Main", "%n records", nullptr, 21), QString("%n запись"));
        QCOMPARE(translator.translate("Main", "%n records", nullptr, 22), QString("%n записи"));
        QCOMPARE(translator.translate("Main", "%n records", nullptr, 25), QString("%n записей"));
        QCOMPARE(Translator::pluralCategory("ar", 11), QString("many"));
        QCOMPARE(Translator::pluralCategory("ja", 1), QString("other"));
    }

    void qmlWindowLoadsAndRendersSampleTable() {
        QTemporaryDir dir;
        QQuickStyle::setStyle("Material");
        Translator translator(dir.filePath("language"), ":/i18n");
        Theme theme(dir.path());
        Backend backend(dir.filePath("ui.sqlite"));
        backend.createSampleTable();

        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("translator", &translator);
        engine.rootContext()->setContextProperty("theme", &theme);
        engine.rootContext()->setContextProperty("backend", &backend);
        engine.load(QUrl("qrc:/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());

        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window != nullptr);
        QTest::qWait(100);
        const QImage grab = window->grabWindow();
        QVERIFY(!grab.isNull());
        QCOMPARE(grab.width(), 1120);
        QCOMPARE(grab.height(), 720);

        // Open Add Record dialog via Space / openRecordDialog(0) and verify form fields are visible and interactive.
        QMetaObject::invokeMethod(window, "openRecordDialog", Q_ARG(QVariant, 0));
        QTest::qWait(80);
        QCOMPARE(window->property("recordDialogVisible").toBool(), true);

        auto *panel = findVisualItem(window->contentItem(), "recordDialogPanel");
        auto *fieldsCol = findVisualItem(window->contentItem(), "recordFieldsCol");
        QVERIFY(panel != nullptr && fieldsCol != nullptr);
        QVERIFY(panel->height() > 300);
        QVERIFY(fieldsCol->height() > 200);

        auto *input0 = findVisualItem(fieldsCol, "recordFieldInput_0");
        auto *input3 = findVisualItem(fieldsCol, "recordFieldInput_3");
        auto *input4 = findVisualItem(fieldsCol, "recordFieldInput_4");
        QVERIFY(input0 != nullptr && input3 != nullptr && input4 != nullptr);
        QVERIFY(input0->width() > 200);
        QVERIFY(input0->isVisible());

        // Enter values into the form inputs and submit.
        input0->setProperty("text", "Framework Laptop 13");
        input3->setProperty("text", "3");
        input4->setProperty("text", "1100");
        QTest::qWait(40);

        QMetaObject::invokeMethod(window, "submitRecordDialog", Q_ARG(QVariant, false));
        QTest::qWait(50);
        QCOMPARE(window->property("recordDialogVisible").toBool(), false);
        QCOMPARE(backend.recordCount(), 6);

        // Verify that columns automatically expand to fit long field values and enable horizontal scrolling.
        const int fEmail = backend.addField("Email", "Email", "", false, false);
        const int fUrl = backend.addField("Website", "URL", "", false, false);
        QVERIFY(fEmail > 0 && fUrl > 0);

        QVariantMap longRow = backend.recordValues(1);
        longRow.insert(QString::number(fEmail), "christopher.alexander.longname@enterprise-domain.example.org");
        longRow.insert(QString::number(fUrl), "https://inventory.enterprise-domain.example.org/assets/hardware/thinkpad-x1");
        QVERIFY(backend.saveRecord(1, longRow) > 0);
        QTest::qWait(50);

        auto *horizFlick = findVisualItem(window->contentItem(), "horizFlick");
        QVERIFY(horizFlick != nullptr);
        const double contentW = horizFlick->property("contentWidth").toDouble();
        const double viewportW = horizFlick->width();
        QVERIFY2(contentW > viewportW, qPrintable(QString("Expected contentWidth (%1) > viewportWidth (%2)").arg(contentW).arg(viewportW)));

        const int emailColW = backend.fieldDetails(fEmail).value("contentWidth").toInt();
        QVERIFY2(emailColW >= 300, qPrintable(QString("Expected email column width >= 300, got %1").arg(emailColW)));
    }

private:
    static QQuickItem *findVisualItem(QQuickItem *item, const QString &name) {
        if (!item)
            return nullptr;
        if (item->objectName() == name)
            return item;
        for (QQuickItem *child : item->childItems()) {
            if (QQuickItem *found = findVisualItem(child, name))
                return found;
        }
        return nullptr;
    }
    static void write(const QString &path, const QByteArray &contents) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(contents);
    }
};

QTEST_MAIN(AikkuTests)
#include "aikku-database_tests.moc"
