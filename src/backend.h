#pragma once

#include <QHash>
#include <QImage>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QSqlDatabase>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

class FilePicker;

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString databasePath READ databasePath NOTIFY databaseChanged)
    Q_PROPERTY(QString databaseName READ databaseName NOTIFY databaseChanged)
    Q_PROPERTY(int imageGeneration READ imageGeneration NOTIFY databaseChanged)
    Q_PROPERTY(QVariantList tables READ tables NOTIFY tablesChanged)
    Q_PROPERTY(int tableCount READ tableCount NOTIFY tablesChanged)
    Q_PROPERTY(int count READ tableCount NOTIFY tablesChanged)
    Q_PROPERTY(int currentTableId READ currentTableId NOTIFY currentTableChanged)
    Q_PROPERTY(QString currentTableName READ currentTableName NOTIFY currentTableChanged)
    Q_PROPERTY(bool currentTableLifo READ currentTableLifo NOTIFY currentTableChanged)
    Q_PROPERTY(QVariantList fields READ fields NOTIFY fieldsChanged)
    Q_PROPERTY(int fieldCount READ fieldCount NOTIFY fieldsChanged)
    Q_PROPERTY(QVariantList filterableFields READ filterableFields NOTIFY fieldsChanged)
    Q_PROPERTY(bool hasFooterRow READ hasFooterRow NOTIFY recordsChanged)
    Q_PROPERTY(QVariantList records READ records NOTIFY recordsChanged)
    Q_PROPERTY(int recordCount READ recordCount NOTIFY recordsChanged)
    Q_PROPERTY(int totalRecordCount READ totalRecordCount NOTIFY recordsChanged)
    Q_PROPERTY(int selectedRowIndex READ selectedRowIndex WRITE setSelectedRowIndex NOTIFY selectedRowIndexChanged)
    Q_PROPERTY(int selectedRecordIndex READ selectedRecordIndex NOTIFY selectedRowIndexChanged)
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY filtersChanged)
    Q_PROPERTY(bool hasActiveFilters READ hasActiveFilters NOTIFY filtersChanged)
    Q_PROPERTY(int sortFieldId READ sortFieldId NOTIFY sortChanged)
    Q_PROPERTY(bool sortAscending READ sortAscending NOTIFY sortChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoStackChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoStackChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY statusChanged)
    Q_PROPERTY(QString noticeMessage READ noticeMessage NOTIFY statusChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString busyMessage READ busyMessage NOTIFY busyChanged)
    Q_PROPERTY(double busyProgress READ busyProgress NOTIFY busyChanged)

public:
    explicit Backend(const QString &databasePath = {}, FilePicker *filePicker = nullptr, QObject *parent = nullptr);
    ~Backend() override;

    QString databasePath() const { return m_databasePath; }
    QString databaseName() const;
    int imageGeneration() const { return m_imageGeneration; }
    QString threadSafeDatabasePath() const;
    QVariantList tables() const { return m_tables; }
    int tableCount() const { return m_tables.size(); }
    int count() const { return m_tables.size(); }
    int currentTableId() const { return m_currentTableId; }
    QString currentTableName() const { return m_currentTableName; }
    bool currentTableLifo() const { return m_currentTableLifo; }

    QVariantList fields() const { return m_fields; }
    int fieldCount() const { return m_fields.size(); }
    QVariantList filterableFields() const;
    bool hasFooterRow() const { return m_hasFooterRow; }

    QVariantList records() const { return m_records; }
    int recordCount() const { return m_records.size(); }
    int totalRecordCount() const { return m_totalRecordCount; }

    int selectedRowIndex() const { return m_selectedRowIndex; }
    void setSelectedRowIndex(int index);
    int selectedRecordIndex() const;

    QString searchText() const { return m_searchText; }
    void setSearchText(const QString &text);
    bool hasActiveFilters() const;

    int sortFieldId() const { return m_sortFieldId; }
    bool sortAscending() const { return m_sortAscending; }

    bool canUndo() const { return !m_undoStack.isEmpty(); }
    bool canRedo() const { return !m_redoStack.isEmpty(); }

    QString errorMessage() const { return m_errorMessage; }
    QString noticeMessage() const { return m_noticeMessage; }
    bool busy() const { return m_busy; }
    QString busyMessage() const { return m_busyMessage; }
    double busyProgress() const { return m_busyProgress; }

    Q_INVOKABLE void primaryAction();

    // Table management
    Q_INVOKABLE int createTable(const QString &name, bool lifo = true);
    Q_INVOKABLE bool updateTable(int tableId, const QString &name, bool lifo);
    Q_INVOKABLE bool deleteTable(int tableId);
    Q_INVOKABLE void selectTable(int tableId);
    Q_INVOKABLE void selectTableByIndex(int index);
    Q_INVOKABLE int createSampleTable(const QString &preset = QStringLiteral("inventory"));
    Q_INVOKABLE void createSampleTableAsync(const QString &preset = QStringLiteral("inventory"));

    // Field (attribute) management
    Q_INVOKABLE int addField(const QString &name, const QString &datatype,
                             const QString &items = {}, bool obligatoire = false,
                             bool filtre = false, const QString &operation = {},
                             const QString &description = {});
    Q_INVOKABLE bool updateField(int fieldId, const QString &name, const QString &datatype,
                                 const QString &items = {}, bool obligatoire = false,
                                 bool filtre = false, const QString &operation = {},
                                 const QString &description = {});
    Q_INVOKABLE bool deleteField(int fieldId);
    Q_INVOKABLE bool moveField(int fieldId, int delta);
    Q_INVOKABLE QVariantMap fieldDetails(int fieldId) const;

    // Record (row) management
    Q_INVOKABLE int saveRecord(int recordIndex, const QVariantMap &fieldValues);
    Q_INVOKABLE bool deleteRecord(int recordIndex);
    Q_INVOKABLE QVariantMap recordValues(int recordIndex) const;

    // Filtering and sorting
    Q_INVOKABLE void setFieldFilter(int fieldId, const QString &value);
    Q_INVOKABLE QString fieldFilter(int fieldId) const;
    Q_INVOKABLE void setDateFilter(int fieldId, const QString &startDate, const QString &endDate);
    Q_INVOKABLE QString dateFilterStart(int fieldId) const;
    Q_INVOKABLE QString dateFilterEnd(int fieldId) const;
    Q_INVOKABLE void clearFilters();
    Q_INVOKABLE void toggleSort(int fieldId);

    // CSV import/export and SQLite database switching
    Q_INVOKABLE bool openDatabaseFile(const QString &filePath);
    Q_INVOKABLE void openDatabaseDialog();
    Q_INVOKABLE bool importCsvFile(const QString &filePath, int targetTableId = 0, const QString &separator = ",");
    Q_INVOKABLE void importCsvDialog(int targetTableId = 0);
    Q_INVOKABLE bool exportCsvFile(const QString &filePath);
    Q_INVOKABLE void exportCsvDialog();
    Q_INVOKABLE bool handleDroppedUrls(const QList<QUrl> &urls);

    // Embedded image & file blobs
    Q_INVOKABLE void pickImageDialog(int fieldId);
    Q_INVOKABLE QString importImageFile(const QString &filePath);
    Q_INVOKABLE QString importImageUrl(const QUrl &url);
    Q_INVOKABLE void pickFileDialog(int fieldId);
    Q_INVOKABLE QString importAttachmentFile(const QString &filePath);
    Q_INVOKABLE QString importAttachmentUrl(const QUrl &url);
    Q_INVOKABLE QString extractBlobToTempFile(const QString &rawValue);
    Q_INVOKABLE bool openBlobAttachment(const QString &rawValue);
    Q_INVOKABLE bool saveBlobToFile(const QString &rawValue, const QString &destPath);
    Q_INVOKABLE void saveBlobDialog(const QString &rawValue);
    Q_INVOKABLE QString imageUrl(const QString &rawValue) const;
    Q_INVOKABLE QVariantMap blobInfo(const QString &rawValue) const;
    QImage loadBlobImage(int blobId) const;
    static QImage loadBlobImageFromFile(const QString &dbPath, int blobId);
    static int parseBlobId(const QString &rawValue);
    static QString formatByteSize(qint64 bytes);

    // Undo and redo
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void clearMessages();

    // Static helpers matching Aikku Access domain rules
    static QString detectDatatype(const QString &sample);
    static QString evaluateFormula(const QString &formulaExpr,
                                   const QHash<QString, QString> &fieldNameToValue,
                                   bool *okOut = nullptr);
    static QString statusBadgeColor(const QString &colorName);
    static QList<QStringList> parseCsv(const QString &content, QChar separator = ',');

signals:
    void databaseChanged();
    void tablesChanged();
    void currentTableChanged();
    void fieldsChanged();
    void recordsChanged();
    void selectedRowIndexChanged();
    void filtersChanged();
    void sortChanged();
    void undoStackChanged();
    void statusChanged();
    void busyChanged();
    void imagePicked(int fieldId, const QString &blobRef);
    void filePicked(int fieldId, const QString &blobRef);

private:
    struct TableRow {
        int id = 0;
        QString name;
        bool lifo = true;
        int recordIndex = 0;
        QString createdAt;
        QString updatedAt;
    };

    struct FieldRow {
        int id = 0;
        int tableId = 0;
        QString name;
        QString datatype;
        QString items;
        bool filtre = false;
        bool obligatoire = false;
        int rowOrder = 0;
        QString operation;
        QString description;
    };

    struct ValueRow {
        int id = 0;
        int fieldId = 0;
        int recordIndex = 0;
        QString data;
        QString oldValue;
        QString createdAt;
        QString updatedAt;
    };

    struct Snapshot {
        QList<TableRow> tables;
        QList<FieldRow> fields;
        QList<ValueRow> values;
        int currentTableId = 0;
    };

    bool initDatabase(const QString &path);
    void reloadAll();
    void reloadTables();
    void reloadFieldsAndRecords();
    Snapshot captureSnapshot() const;
    bool restoreSnapshot(const Snapshot &snapshot);
    void pushUndoSnapshot();
    void setError(const QString &message);
    void setNotice(const QString &message);
    void setBusy(bool busy, const QString &message = {}, double progress = -1.0);
    bool validateFieldOptions(const QString &datatype, const QString &items, QString *errorOut) const;

    QString m_connectionName;
    mutable QMutex m_dbPathMutex;
    QString m_databasePath;
    int m_imageGeneration = 0;
    FilePicker *m_filePicker = nullptr;
    int m_pendingImportTableId = 0;
    int m_pendingImageFieldId = 0;
    int m_pendingFileFieldId = 0;
    QString m_pendingSaveBlobRef;

    QVariantList m_tables;
    int m_currentTableId = 0;
    QString m_currentTableName;
    bool m_currentTableLifo = true;

    QVariantList m_fields;
    bool m_hasFooterRow = false;
    QVariantList m_records;
    int m_totalRecordCount = 0;
    int m_selectedRowIndex = -1;

    QString m_searchText;
    QHash<int, QString> m_fieldFilters;
    QHash<int, QPair<QString, QString>> m_dateFilters;

    int m_sortFieldId = 0;
    bool m_sortAscending = false;

    QList<Snapshot> m_undoStack;
    QList<Snapshot> m_redoStack;

    QString m_errorMessage;
    QString m_noticeMessage;
    bool m_busy = false;
    QString m_busyMessage;
    double m_busyProgress = -1.0;
};
