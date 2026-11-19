#include <QDir>
#include <QFile>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "backend.h"
#include "blobimageprovider.h"
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
    QUrl nextImageOpenUrl;
    QUrl nextAttachmentOpenUrl;
    QUrl nextAttachmentSaveUrl;

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
    void openImage() override {
        if (nextImageOpenUrl.isValid())
            emit imageOpenSelected(nextImageOpenUrl);
    }
    void openAttachment() override {
        if (nextAttachmentOpenUrl.isValid())
            emit attachmentOpenSelected(nextAttachmentOpenUrl);
    }
    void saveAttachment(const QUrl &suggestedUrl) override {
        emit attachmentSaveSelected(nextAttachmentSaveUrl.isValid() ? nextAttachmentSaveUrl : suggestedUrl);
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

    void imageFieldBlobsAndProvider() {
        QTemporaryDir dir;
        const QString smallPngPath = dir.filePath("badge.png");
        QImage smallImg(120, 80, QImage::Format_ARGB32);
        smallImg.fill(QColor(20, 140, 220, 200));
        QVERIFY(smallImg.save(smallPngPath, "PNG"));

        const QString bigJpgPath = dir.filePath("banner.jpg");
        QImage bigImg(2400, 1200, QImage::Format_RGB32);
        bigImg.fill(QColor(220, 120, 40));
        QVERIFY(bigImg.save(bigJpgPath, "JPEG", 90));

        FakeFilePicker picker;
        picker.nextImageOpenUrl = QUrl::fromLocalFile(smallPngPath);

        Backend backend(dir.filePath("images.sqlite"), &picker);
        QVERIFY(backend.createTable("Catalog", true) > 0);
        const int fTitle = backend.addField("Title", "Text", "", true, false);
        const int fPhoto = backend.addField("Photo", "Image", "", false, true);
        QVERIFY(fTitle > 0 && fPhoto > 0);

        // Pick image via portal picker interface.
        QSignalSpy spy(&backend, &Backend::imagePicked);
        backend.pickImageDialog(fPhoto);
        QCOMPARE(spy.count(), 1);
        const int pickedFieldId = spy.at(0).at(0).toInt();
        const QString blobRef = spy.at(0).at(1).toString();
        QCOMPARE(pickedFieldId, fPhoto);
        QVERIFY(blobRef.startsWith("blob:"));

        // Verify stored blob metadata & image provider loading.
        const QVariantMap info = backend.blobInfo(blobRef);
        QCOMPARE(info.value("filename").toString(), QString("badge.png"));
        QCOMPARE(info.value("mimeType").toString(), QString("image/png"));
        QCOMPARE(info.value("width").toInt(), 120);
        QCOMPARE(info.value("height").toInt(), 80);
        QVERIFY(info.value("url").toString().startsWith("image://aikku/"));

        // Save record and check cell metadata.
        QVariantMap row1;
        row1.insert(QString::number(fTitle), "Blue Widget");
        row1.insert(QString::number(fPhoto), blobRef);
        QCOMPARE(backend.saveRecord(0, row1), 1);
        QCOMPARE(backend.recordCount(), 1);

        const QVariantList cells = backend.records().first().toMap().value("cells").toList();
        QCOMPARE(cells.size(), 2);
        const QVariantMap photoCell = cells.at(1).toMap();
        QCOMPARE(photoCell.value("imageName").toString(), QString("badge.png"));
        QCOMPARE(photoCell.value("imageWidth").toInt(), 120);
        QCOMPARE(photoCell.value("imageHeight").toInt(), 80);
        QVERIFY(photoCell.value("imageUrl").toString().startsWith("image://aikku/"));

        // Search matches filename, NOT internal "blob:" prefix.
        backend.setSearchText("badge.png");
        QCOMPARE(backend.recordCount(), 1);
        backend.setSearchText("blob:");
        QCOMPARE(backend.recordCount(), 0);
        backend.clearFilters();
        QCOMPARE(backend.recordCount(), 1);

        // CSV export writes filename rather than "blob:<id>".
        const QString csvOut = dir.filePath("catalog.csv");
        QVERIFY(backend.exportCsvFile(csvOut));
        QFile csvFile(csvOut);
        QVERIFY(csvFile.open(QIODevice::ReadOnly));
        const QString csvText = QString::fromUtf8(csvFile.readAll());
        QVERIFY(csvText.contains("Blue Widget,badge.png"));
        QVERIFY(!csvText.contains("blob:"));

        // Undo & redo preserve the embedded blob.
        QVERIFY(backend.deleteRecord(1));
        QCOMPARE(backend.recordCount(), 0);
        backend.undo();
        QCOMPARE(backend.recordCount(), 1);
        QCOMPARE(backend.recordValues(1).value(QString::number(fPhoto)).toString(), blobRef);

        // Oversized image is automatically scaled down to <= 1600px.
        const QString bigRef = backend.importImageFile(bigJpgPath);
        QVERIFY(bigRef.startsWith("blob:"));
        const QVariantMap bigInfo = backend.blobInfo(bigRef);
        QCOMPARE(bigInfo.value("width").toInt(), 1600);
        QCOMPARE(bigInfo.value("height").toInt(), 800);
        QCOMPARE(bigInfo.value("mimeType").toString(), QString("image/jpeg"));

        // BlobImageProvider serves both full-resolution and scaled thumbnails, and read-only loader works.
        BlobImageProvider provider(&backend);
        QSize origSize;
        const int bigBlobId = Backend::parseBlobId(bigRef);
        const QImage fullLoaded = provider.requestImage(QString("1/%1").arg(bigBlobId), &origSize, QSize());
        QVERIFY(!fullLoaded.isNull());
        QCOMPARE(origSize, QSize(1600, 800));
        QCOMPARE(fullLoaded.size(), QSize(1600, 800));

        QSize thumbOrigSize;
        const QImage thumbLoaded = provider.requestImage(QString("1/%1").arg(bigBlobId), &thumbOrigSize, QSize(80, 80));
        QVERIFY(!thumbLoaded.isNull());
        QCOMPARE(thumbOrigSize, QSize(1600, 800));
        QCOMPARE(thumbLoaded.size(), QSize(160, 80));

        const QImage roLoaded = Backend::loadBlobImageFromFile(backend.threadSafeDatabasePath(), bigBlobId);
        QVERIFY(!roLoaded.isNull());
        QCOMPARE(roLoaded.size(), QSize(1600, 800));
    }

    void fileFieldAttachments() {
        QTemporaryDir dir;
        const QString pdfPath = dir.filePath("contract-2026.pdf");
        const QByteArray pdfBytes("%PDF-1.4\n1 0 obj\n<< /Type /Catalog >>\nendobj\n%%EOF\n");
        write(pdfPath, pdfBytes);

        const QString notePath = dir.filePath("notes.txt");
        const QByteArray noteBytes("Quarterly notes and checklist.\n");
        write(notePath, noteBytes);

        const QString savedOutPath = dir.filePath("saved-copy.pdf");

        FakeFilePicker picker;
        picker.nextAttachmentOpenUrl = QUrl::fromLocalFile(pdfPath);
        picker.nextAttachmentSaveUrl = QUrl::fromLocalFile(savedOutPath);

        Backend backend(dir.filePath("files.sqlite"), &picker);
        QVERIFY(backend.createTable("Docs", true) > 0);
        const int fTitle = backend.addField("Title", "Text", "", true, false);
        const int fDoc = backend.addField("Document", "File", "", false, true);
        QVERIFY(fTitle > 0 && fDoc > 0);

        // Pick attachment via portal picker.
        QSignalSpy spy(&backend, &Backend::filePicked);
        backend.pickFileDialog(fDoc);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toInt(), fDoc);
        const QString blobRef = spy.at(0).at(1).toString();
        QVERIFY(blobRef.startsWith("blob:"));

        // Inspect blob metadata.
        const QVariantMap info = backend.blobInfo(blobRef);
        QCOMPARE(info.value("filename").toString(), QString("contract-2026.pdf"));
        QCOMPARE(info.value("extension").toString(), QString("PDF"));
        QCOMPARE(info.value("sizeBytes").toLongLong(), qint64(pdfBytes.size()));
        QVERIFY(!info.value("sizeLabel").toString().isEmpty());

        // Save record and verify cell metadata.
        QVariantMap row1;
        row1.insert(QString::number(fTitle), "Vendor Agreement");
        row1.insert(QString::number(fDoc), blobRef);
        QCOMPARE(backend.saveRecord(0, row1), 1);

        const QVariantMap docCell = backend.records().first().toMap().value("cells").toList().at(1).toMap();
        QCOMPARE(docCell.value("fileName").toString(), QString("contract-2026.pdf"));
        QCOMPARE(docCell.value("fileExt").toString(), QString("PDF"));
        QCOMPARE(docCell.value("fileSizeBytes").toLongLong(), qint64(pdfBytes.size()));

        // Search by filename works, searching internal "blob:" does not.
        backend.setSearchText("contract-2026");
        QCOMPARE(backend.recordCount(), 1);
        backend.setSearchText("blob:");
        QCOMPARE(backend.recordCount(), 0);
        backend.clearFilters();

        // Quick filter by attached filename.
        backend.setFieldFilter(fDoc, "contract-2026.pdf");
        QCOMPARE(backend.recordCount(), 1);
        backend.clearFilters();

        // CSV export writes filename rather than "blob:<id>".
        const QString csvOut = dir.filePath("docs.csv");
        QVERIFY(backend.exportCsvFile(csvOut));
        QFile csvFile(csvOut);
        QVERIFY(csvFile.open(QIODevice::ReadOnly));
        const QString csvText = QString::fromUtf8(csvFile.readAll());
        QVERIFY(csvText.contains("Vendor Agreement,contract-2026.pdf"));
        QVERIFY(!csvText.contains("blob:"));

        // Extract to temp file & save via Save As dialog preserve exact bytes.
        const QString tempPath = backend.extractBlobToTempFile(blobRef);
        QVERIFY(!tempPath.isEmpty());
        QFile tempFile(tempPath);
        QVERIFY(tempFile.open(QIODevice::ReadOnly));
        QCOMPARE(tempFile.readAll(), pdfBytes);

        backend.saveBlobDialog(blobRef);
        QVERIFY(QFile::exists(savedOutPath));
        QFile savedFile(savedOutPath);
        QVERIFY(savedFile.open(QIODevice::ReadOnly));
        QCOMPARE(savedFile.readAll(), pdfBytes);

        // Undo/redo preserves embedded file attachment.
        QVERIFY(backend.deleteRecord(1));
        QCOMPARE(backend.recordCount(), 0);
        backend.undo();
        QCOMPARE(backend.recordCount(), 1);
        QCOMPARE(backend.recordValues(1).value(QString::number(fDoc)).toString(), blobRef);

        // Importing a text file via URL works as well.
        const QString noteRef = backend.importAttachmentUrl(QUrl::fromLocalFile(notePath));
        QVERIFY(noteRef.startsWith("blob:"));
        QCOMPARE(backend.blobInfo(noteRef).value("extension").toString(), QString("TXT"));
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
        engine.addImageProvider("aikku", new BlobImageProvider(&backend));
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

        // Verify the sample table's built-in product photos are embedded and rendered in column 0.
        QCOMPARE(backend.fieldCount(), 8);
        QCOMPARE(backend.recordCount(), 25);
        QCOMPARE(backend.fields().first().toMap().value("datatype").toString(), QString("Image"));
        auto *sampleThumb = findVisualItem(window->contentItem(), "gridImageThumb_0_0");
        QVERIFY(sampleThumb != nullptr);
        QVERIFY(sampleThumb->isVisible());

        // Open Add Record dialog via Space / openRecordDialog(0) and verify form fields are visible and interactive.
        QMetaObject::invokeMethod(window, "openRecordDialog", Q_ARG(QVariant, 0));
        QTest::qWait(80);
        QCOMPARE(window->property("recordDialogVisible").toBool(), true);

        auto *panel = findVisualItem(window->contentItem(), "recordDialogPanel");
        auto *fieldsCol = findVisualItem(window->contentItem(), "recordFieldsCol");
        QVERIFY(panel != nullptr && fieldsCol != nullptr);
        QVERIFY(panel->height() > 300);
        QVERIFY(fieldsCol->height() > 200);

        auto *input1 = findVisualItem(fieldsCol, "recordFieldInput_1");
        auto *input4 = findVisualItem(fieldsCol, "recordFieldInput_4");
        auto *input5 = findVisualItem(fieldsCol, "recordFieldInput_5");
        QVERIFY(input1 != nullptr && input4 != nullptr && input5 != nullptr);
        QVERIFY(input1->width() > 200);
        QVERIFY(input1->isVisible());

        // Enter values into the form inputs and submit.
        input1->setProperty("text", "Framework Laptop 13");
        input4->setProperty("text", "3");
        input5->setProperty("text", "1100");
        QTest::qWait(40);

        QMetaObject::invokeMethod(window, "submitRecordDialog", Q_ARG(QVariant, false));
        QTest::qWait(50);
        QCOMPARE(window->property("recordDialogVisible").toBool(), false);
        QCOMPARE(backend.recordCount(), 26);

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

        // Open Manage Table Attributes dialog and select "Relation" datatype.
        QMetaObject::invokeMethod(window, "openFieldsDialog");
        QTest::qWait(50);
        QCOMPARE(window->property("fieldsDialogVisible").toBool(), true);

        auto *fieldsDialogPanel = findVisualItem(window->contentItem(), "fieldsDialogPanel");
        auto *fieldFormBox = findVisualItem(window->contentItem(), "fieldFormBox");
        auto *requiredCheck = findVisualItem(window->contentItem(), "fieldFormRequiredCheck");
        QVERIFY(fieldsDialogPanel != nullptr && fieldFormBox != nullptr && requiredCheck != nullptr);

        window->setProperty("fieldFormDatatype", "Relation");
        QMetaObject::invokeMethod(window, "setRelationTarget", Q_ARG(QVariant, backend.currentTableName()), Q_ARG(QVariant, ""));
        QTest::qWait(50);

        const QPointF reqTopLeftInBox = requiredCheck->mapToItem(fieldFormBox, QPointF(0, 0));
        const QPointF reqBottomRightInBox = requiredCheck->mapToItem(fieldFormBox, QPointF(requiredCheck->width(), requiredCheck->height()));
        QVERIFY2(reqTopLeftInBox.x() >= 0 && reqTopLeftInBox.y() >= 0
                 && reqBottomRightInBox.x() <= fieldFormBox->width()
                 && reqBottomRightInBox.y() <= fieldFormBox->height(),
                 qPrintable(QString("Expected Required checkbox inside attribute form box (%1x%2), got bottom-right (%3,%4)")
                            .arg(fieldFormBox->width()).arg(fieldFormBox->height())
                            .arg(reqBottomRightInBox.x()).arg(reqBottomRightInBox.y())));

        const QPointF boxBottomRightInPanel = fieldFormBox->mapToItem(fieldsDialogPanel, QPointF(fieldFormBox->width(), fieldFormBox->height()));
        QVERIFY2(boxBottomRightInPanel.y() <= fieldsDialogPanel->height(),
                 qPrintable(QString("Expected attribute form box bottom (%1) <= dialog panel height (%2)")
                            .arg(boxBottomRightInPanel.y()).arg(fieldsDialogPanel->height())));

        // Close attributes dialog, add an Image attribute, attach an image, and verify grid thumbnail + click-to-zoom lightbox.
        window->setProperty("fieldsDialogVisible", false);
        const int fPhoto = backend.addField("Photo", "Image", "", false, false);
        QVERIFY(fPhoto > 0);

        const QString imgFile = dir.filePath("laptop.png");
        QImage sampleImg(320, 200, QImage::Format_ARGB32);
        sampleImg.fill(QColor(80, 180, 120));
        QVERIFY(sampleImg.save(imgFile, "PNG"));

        const QString blobRef = backend.importImageFile(imgFile);
        QVERIFY(blobRef.startsWith("blob:"));

        const int topRecIdx = backend.records().first().toMap().value("recordIndex").toInt();
        QVariantMap rowWithImg = backend.recordValues(topRecIdx);
        rowWithImg.insert(QString::number(fPhoto), blobRef);
        QVERIFY(backend.saveRecord(topRecIdx, rowWithImg) > 0);
        QTest::qWait(80);

        // Verify the image thumbnail is visible in the records grid.
        const int photoColIdx = backend.fieldCount() - 1;
        auto *gridThumb = findVisualItem(window->contentItem(), QString("gridImageThumb_0_%1").arg(photoColIdx));
        QVERIFY(gridThumb != nullptr);
        QVERIFY(gridThumb->isVisible());
        QCOMPARE(gridThumb->width(), 40.0);
        QCOMPARE(gridThumb->height(), 40.0);

        // Verify the record form renders the Image field picker box properly.
        QMetaObject::invokeMethod(window, "openRecordDialog", Q_ARG(QVariant, topRecIdx));
        QTest::qWait(60);
        auto *recordImgField = findVisualItem(window->contentItem(), QString("recordImageField_%1").arg(photoColIdx));
        QVERIFY(recordImgField != nullptr);
        QVERIFY(recordImgField->isVisible());
        QCOMPARE(recordImgField->height(), 84.0);
        QMetaObject::invokeMethod(window, "closeRecordDialog");
        QTest::qWait(40);

        // Verify the click-to-zoom lightbox opens, loads the embedded image, zooms, and closes cleanly.
        const QString imgUrl = backend.imageUrl(blobRef);
        QMetaObject::invokeMethod(window, "openImagePreview",
                                  Q_ARG(QVariant, imgUrl),
                                  Q_ARG(QVariant, QString("laptop.png")),
                                  Q_ARG(QVariant, 320),
                                  Q_ARG(QVariant, 200),
                                  Q_ARG(QVariant, QString("Photo")));
        QTest::qWait(80);
        QCOMPARE(window->property("imagePreviewVisible").toBool(), true);

        auto *previewOverlay = findVisualItem(window->contentItem(), "imagePreviewOverlay");
        auto *previewPanel = findVisualItem(window->contentItem(), "imagePreviewPanel");
        auto *previewImg = findVisualItem(window->contentItem(), "imagePreviewImage");
        auto *previewCloseBtn = findVisualItem(window->contentItem(), "imagePreviewCloseBtn");
        QVERIFY(previewOverlay != nullptr && previewPanel != nullptr && previewImg != nullptr && previewCloseBtn != nullptr);
        QVERIFY(previewOverlay->isVisible());
        QVERIFY(previewPanel->width() <= 760.0 && previewPanel->height() <= 540.0);
        QCOMPARE(previewImg->property("implicitWidth").toInt(), 320);
        QCOMPARE(previewImg->property("implicitHeight").toInt(), 200);

        QMetaObject::invokeMethod(window, "adjustImagePreviewZoom", Q_ARG(QVariant, 1.0));
        QCOMPARE(window->property("imagePreviewZoom").toDouble(), 2.0);

        // Clicking the Close button closes the preview without leaking clicks to Delete buttons underneath.
        const QPoint closeCenter = previewCloseBtn->mapToScene(
            QPointF(previewCloseBtn->width() / 2.0, previewCloseBtn->height() / 2.0)).toPoint();
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, closeCenter);
        QTest::qWait(60);
        QCOMPARE(window->property("imagePreviewVisible").toBool(), false);
        QCOMPARE(window->property("deleteTableConfirmVisible").toBool(), false);
        QCOMPARE(window->property("deleteRecordConfirmVisible").toBool(), false);

        // Add a File attribute, attach a PDF, and verify grid chip + record form attachment box.
        const int fManual = backend.addField("Manual", "File", "", false, false);
        QVERIFY(fManual > 0);
        const QString pdfFile = dir.filePath("manual.pdf");
        write(pdfFile, "%PDF-1.4\n%%EOF\n");
        const QString pdfRef = backend.importAttachmentFile(pdfFile);
        QVERIFY(pdfRef.startsWith("blob:"));

        QVariantMap rowWithFile = backend.recordValues(topRecIdx);
        rowWithFile.insert(QString::number(fManual), pdfRef);
        QVERIFY(backend.saveRecord(topRecIdx, rowWithFile) > 0);
        QTest::qWait(80);

        const int fileColIdx = backend.fieldCount() - 1;
        auto *gridFileChip = findVisualItem(window->contentItem(), QString("gridFileChip_0_%1").arg(fileColIdx));
        QVERIFY(gridFileChip != nullptr);
        QVERIFY(gridFileChip->isVisible());

        QMetaObject::invokeMethod(window, "openRecordDialog", Q_ARG(QVariant, topRecIdx));
        QTest::qWait(60);
        auto *recordFileField = findVisualItem(window->contentItem(), QString("recordFileField_%1").arg(fileColIdx));
        QVERIFY(recordFileField != nullptr);
        QVERIFY(recordFileField->isVisible());
        QCOMPARE(recordFileField->height(), 84.0);
        QMetaObject::invokeMethod(window, "closeRecordDialog");

        // Verify createSampleTableAsync displays the busy progress overlay and finishes cleanly.
        const int tablesBefore = backend.tableCount();
        auto *busyOverlay = findVisualItem(window->contentItem(), "busyOverlay");
        QVERIFY(busyOverlay != nullptr);
        QCOMPARE(busyOverlay->isVisible(), false);
        backend.createSampleTableAsync();
        QCOMPARE(backend.busy(), true);
        QCOMPARE(busyOverlay->isVisible(), true);
        QTRY_COMPARE(backend.busy(), false);
        QCOMPARE(busyOverlay->isVisible(), false);
        QCOMPARE(backend.tableCount(), tablesBefore + 1);
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
