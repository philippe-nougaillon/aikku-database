#include "backend.h"

#include <QColor>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QLocale>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>

#include "filepicker.h"

namespace {
int s_connectionCounter = 0;

QString nowIso() {
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

QString formatTimestamp(const QString &iso) {
    const QDateTime dt = QDateTime::fromString(iso, Qt::ISODate).toLocalTime();
    if (!dt.isValid())
        return iso;
    return dt.toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}

// Recursive-descent arithmetic parser for formulas after [Field] substitution.
class ArithmeticParser {
public:
    explicit ArithmeticParser(const QString &expr) : m_expr(expr) {}

    double parse(bool *ok) {
        m_pos = 0;
        m_ok = true;
        const double result = parseExpression();
        skipSpaces();
        if (m_pos < m_expr.size() || !std::isfinite(result))
            m_ok = false;
        if (ok)
            *ok = m_ok;
        return m_ok ? result : 0.0;
    }

private:
    void skipSpaces() {
        while (m_pos < m_expr.size() && m_expr.at(m_pos).isSpace())
            ++m_pos;
    }

    double parseExpression() {
        double left = parseTerm();
        while (m_ok) {
            skipSpaces();
            if (m_pos < m_expr.size() && (m_expr.at(m_pos) == QLatin1Char('+') || m_expr.at(m_pos) == QLatin1Char('-'))) {
                const QChar op = m_expr.at(m_pos++);
                const double right = parseTerm();
                left = (op == QLatin1Char('+')) ? (left + right) : (left - right);
            } else {
                break;
            }
        }
        return left;
    }

    double parseTerm() {
        double left = parseFactor();
        while (m_ok) {
            skipSpaces();
            if (m_pos < m_expr.size() && (m_expr.at(m_pos) == QLatin1Char('*') || m_expr.at(m_pos) == QLatin1Char('/'))) {
                const QChar op = m_expr.at(m_pos++);
                const double right = parseFactor();
                if (op == QLatin1Char('/')) {
                    if (std::abs(right) < 1e-12) {
                        m_ok = false;
                        return 0.0;
                    }
                    left /= right;
                } else {
                    left *= right;
                }
            } else {
                break;
            }
        }
        return left;
    }

    double parseFactor() {
        skipSpaces();
        if (m_pos >= m_expr.size()) {
            m_ok = false;
            return 0.0;
        }
        if (m_expr.at(m_pos) == QLatin1Char('+')) {
            ++m_pos;
            return parseFactor();
        }
        if (m_expr.at(m_pos) == QLatin1Char('-')) {
            ++m_pos;
            return -parseFactor();
        }
        if (m_expr.at(m_pos) == QLatin1Char('(')) {
            ++m_pos;
            const double val = parseExpression();
            skipSpaces();
            if (m_pos >= m_expr.size() || m_expr.at(m_pos) != QLatin1Char(')')) {
                m_ok = false;
                return 0.0;
            }
            ++m_pos;
            return val;
        }
        const int start = m_pos;
        bool seenDot = false;
        while (m_pos < m_expr.size()) {
            const QChar ch = m_expr.at(m_pos);
            if (ch.isDigit()) {
                ++m_pos;
            } else if ((ch == QLatin1Char('.') || ch == QLatin1Char(',')) && !seenDot) {
                seenDot = true;
                ++m_pos;
            } else {
                break;
            }
        }
        if (start == m_pos) {
            m_ok = false;
            return 0.0;
        }
        QString token = m_expr.mid(start, m_pos - start);
        token.replace(QLatin1Char(','), QLatin1Char('.'));
        bool numOk = false;
        const double val = token.toDouble(&numOk);
        if (!numOk)
            m_ok = false;
        return val;
    }

    QString m_expr;
    int m_pos = 0;
    bool m_ok = true;
};

QString escapeCsvCell(const QString &value) {
    if (value.contains(QLatin1Char(',')) || value.contains(QLatin1Char('"')) ||
        value.contains(QLatin1Char('\n')) || value.contains(QLatin1Char('\r'))) {
        QString escaped = value;
        escaped.replace(QLatin1String("\""), QLatin1String("\"\""));
        return QStringLiteral("\"%1\"").arg(escaped);
    }
    return value;
}

bool parseDoubleFlexible(const QString &text, double *out = nullptr) {
    QString cleaned = text.trimmed();
    cleaned.replace(QLatin1Char(','), QLatin1Char('.'));
    cleaned.remove(QStringLiteral("€")).remove(QLatin1Char(' '));
    bool ok = false;
    const double val = cleaned.toDouble(&ok);
    if (ok && out)
        *out = val;
    return ok;
}
} // namespace

Backend::Backend(const QString &databasePath, FilePicker *filePicker, QObject *parent)
    : QObject(parent),
      m_connectionName(QStringLiteral("aikku_sql_%1").arg(++s_connectionCounter)),
      m_filePicker(filePicker) {
    if (m_filePicker) {
        connect(m_filePicker, &FilePicker::csvOpenSelected, this, [this](const QUrl &url) {
            importCsvFile(url.toLocalFile(), m_pendingImportTableId);
        });
        connect(m_filePicker, &FilePicker::csvExportSelected, this, [this](const QUrl &url) {
            exportCsvFile(url.toLocalFile());
        });
        connect(m_filePicker, &FilePicker::databaseOpenSelected, this, [this](const QUrl &url) {
            openDatabaseFile(url.toLocalFile());
        });
        connect(m_filePicker, &FilePicker::failed, this, &Backend::setError);
    }

    QString path = databasePath;
    if (path.isEmpty()) {
        const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(dataDir);
        path = dataDir + QStringLiteral("/aikku-database.sqlite");
    }
    initDatabase(path);
}

Backend::~Backend() {
    {
        QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
        if (db.isOpen())
            db.close();
    }
    QSqlDatabase::removeDatabase(m_connectionName);
}

QString Backend::databaseName() const {
    return QFileInfo(m_databasePath).fileName();
}

bool Backend::initDatabase(const QString &path) {
    const QFileInfo info(path);
    if (!info.absolutePath().isEmpty())
        QDir().mkpath(info.absolutePath());

    if (QSqlDatabase::contains(m_connectionName)) {
        {
            QSqlDatabase existing = QSqlDatabase::database(m_connectionName, false);
            if (existing.isOpen())
                existing.close();
        }
        QSqlDatabase::removeDatabase(m_connectionName);
    }

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    db.setDatabaseName(path);
    if (!db.open()) {
        setError(tr("Could not open SQLite database: %1").arg(db.lastError().text()));
        return false;
    }

    QSqlQuery query(db);
    query.exec(QStringLiteral("PRAGMA foreign_keys = ON"));

    if (!query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS tables ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  name TEXT NOT NULL,"
            "  lifo INTEGER NOT NULL DEFAULT 1,"
            "  record_index INTEGER NOT NULL DEFAULT 0,"
            "  created_at TEXT NOT NULL,"
            "  updated_at TEXT NOT NULL"
            ")"))) {
        setError(tr("Failed to initialize tables schema: %1").arg(query.lastError().text()));
        return false;
    }

    if (!query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS fields ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  table_id INTEGER NOT NULL,"
            "  name TEXT NOT NULL,"
            "  datatype TEXT NOT NULL DEFAULT 'Text',"
            "  items TEXT NOT NULL DEFAULT '',"
            "  filtre INTEGER NOT NULL DEFAULT 0,"
            "  obligatoire INTEGER NOT NULL DEFAULT 0,"
            "  row_order INTEGER NOT NULL DEFAULT 0,"
            "  operation TEXT NOT NULL DEFAULT '',"
            "  description TEXT NOT NULL DEFAULT '',"
            "  FOREIGN KEY(table_id) REFERENCES tables(id) ON DELETE CASCADE"
            ")"))) {
        setError(tr("Failed to initialize fields schema: %1").arg(query.lastError().text()));
        return false;
    }

    if (!query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS \"values\" ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  field_id INTEGER NOT NULL,"
            "  record_index INTEGER NOT NULL,"
            "  data TEXT NOT NULL DEFAULT '',"
            "  old_value TEXT NOT NULL DEFAULT '',"
            "  created_at TEXT NOT NULL,"
            "  updated_at TEXT NOT NULL,"
            "  FOREIGN KEY(field_id) REFERENCES fields(id) ON DELETE CASCADE"
            ")"))) {
        setError(tr("Failed to initialize values schema: %1").arg(query.lastError().text()));
        return false;
    }

    query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_fields_table ON fields(table_id, row_order)"));
    query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_values_field_record ON \"values\"(field_id, record_index)"));

    m_databasePath = QFileInfo(path).absoluteFilePath();
    m_undoStack.clear();
    m_redoStack.clear();
    m_currentTableId = 0;
    m_searchText.clear();
    m_fieldFilters.clear();
    m_dateFilters.clear();
    m_sortFieldId = 0;
    m_sortAscending = false;

    emit databaseChanged();
    emit undoStackChanged();
    reloadAll();
    return true;
}

void Backend::primaryAction() {
    if (m_tables.isEmpty()) {
        createTable(tr("Objects"), true);
        return;
    }
    if (m_fields.isEmpty()) {
        addField(tr("Name"), QStringLiteral("Text"), {}, true, false, {}, {});
    }
}

void Backend::reloadAll() {
    reloadTables();
    reloadFieldsAndRecords();
}

void Backend::reloadTables() {
    QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
    if (!db.isOpen())
        return;

    QVariantList nextTables;
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT id, name, lifo, record_index, created_at, updated_at FROM tables ORDER BY name COLLATE NOCASE ASC, id ASC"));
    if (q.exec()) {
        while (q.next()) {
            const int tableId = q.value(0).toInt();
            const QString name = q.value(1).toString();
            const bool lifo = q.value(2).toInt() != 0;

            // Count distinct active record_index rows in values for this table.
            int activeCount = 0;
            QSqlQuery countQuery(db);
            countQuery.prepare(QStringLiteral(
                "SELECT COUNT(DISTINCT v.record_index) FROM \"values\" v "
                "JOIN fields f ON f.id = v.field_id "
                "WHERE f.table_id = ? AND v.data != ''"));
            countQuery.addBindValue(tableId);
            if (countQuery.exec() && countQuery.next())
                activeCount = countQuery.value(0).toInt();

            // Gather ordered field names for subtitle summary.
            QStringList fieldNames;
            QSqlQuery fieldQuery(db);
            fieldQuery.prepare(QStringLiteral("SELECT name FROM fields WHERE table_id = ? ORDER BY row_order ASC, id ASC"));
            fieldQuery.addBindValue(tableId);
            if (fieldQuery.exec()) {
                while (fieldQuery.next())
                    fieldNames.append(fieldQuery.value(0).toString());
            }

            QString lastUpd = q.value(5).toString();
            QSqlQuery maxUpdQuery(db);
            maxUpdQuery.prepare(QStringLiteral(
                "SELECT MAX(v.updated_at) FROM \"values\" v "
                "JOIN fields f ON f.id = v.field_id WHERE f.table_id = ?"));
            maxUpdQuery.addBindValue(tableId);
            if (maxUpdQuery.exec() && maxUpdQuery.next() && !maxUpdQuery.value(0).toString().isEmpty())
                lastUpd = maxUpdQuery.value(0).toString();

            QVariantMap item;
            item.insert(QStringLiteral("id"), tableId);
            item.insert(QStringLiteral("name"), name);
            item.insert(QStringLiteral("lifo"), lifo);
            item.insert(QStringLiteral("recordCount"), activeCount);
            item.insert(QStringLiteral("fieldCount"), fieldNames.size());
            item.insert(QStringLiteral("fieldSummary"), fieldNames.join(QStringLiteral(", ")));
            item.insert(QStringLiteral("updatedAt"), formatTimestamp(lastUpd));
            nextTables.append(item);
        }
    }

    m_tables = nextTables;
    emit tablesChanged();

    // Ensure currentTableId points to an existing table or 0 if none.
    bool foundCurrent = false;
    for (const QVariant &v : std::as_const(m_tables)) {
        const QVariantMap map = v.toMap();
        if (map.value(QStringLiteral("id")).toInt() == m_currentTableId) {
            foundCurrent = true;
            m_currentTableName = map.value(QStringLiteral("name")).toString();
            m_currentTableLifo = map.value(QStringLiteral("lifo")).toBool();
            break;
        }
    }

    if (!foundCurrent) {
        if (!m_tables.isEmpty()) {
            const QVariantMap first = m_tables.first().toMap();
            m_currentTableId = first.value(QStringLiteral("id")).toInt();
            m_currentTableName = first.value(QStringLiteral("name")).toString();
            m_currentTableLifo = first.value(QStringLiteral("lifo")).toBool();
        } else {
            m_currentTableId = 0;
            m_currentTableName.clear();
            m_currentTableLifo = true;
        }
        m_searchText.clear();
        m_fieldFilters.clear();
        m_dateFilters.clear();
        m_sortFieldId = 0;
        m_sortAscending = !m_currentTableLifo;
    }
    emit currentTableChanged();
}

void Backend::reloadFieldsAndRecords() {
    QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
    if (!db.isOpen() || m_currentTableId <= 0) {
        m_fields.clear();
        m_records.clear();
        m_totalRecordCount = 0;
        m_hasFooterRow = false;
        m_selectedRowIndex = -1;
        emit fieldsChanged();
        emit recordsChanged();
        emit selectedRowIndexChanged();
        return;
    }

    // 1. Load fields ordered by row_order.
    QList<FieldRow> fieldRows;
    {
        QSqlQuery fq(db);
        fq.prepare(QStringLiteral(
            "SELECT id, table_id, name, datatype, items, filtre, obligatoire, row_order, operation, description "
            "FROM fields WHERE table_id = ? ORDER BY row_order ASC, id ASC"));
        fq.addBindValue(m_currentTableId);
        if (fq.exec()) {
            while (fq.next()) {
                FieldRow fr;
                fr.id = fq.value(0).toInt();
                fr.tableId = fq.value(1).toInt();
                fr.name = fq.value(2).toString();
                fr.datatype = fq.value(3).toString();
                fr.items = fq.value(4).toString();
                fr.filtre = fq.value(5).toInt() != 0;
                fr.obligatoire = fq.value(6).toInt() != 0;
                fr.rowOrder = fq.value(7).toInt();
                fr.operation = fq.value(8).toString();
                fr.description = fq.value(9).toString();
                fieldRows.append(fr);
            }
        }
    }

    // 2. Load all values for this table grouped by record_index.
    struct RecordBucket {
        QHash<int, QString> valuesByField;
        QString maxUpdatedAt;
    };
    QHash<int, RecordBucket> buckets;
    {
        QSqlQuery vq(db);
        vq.prepare(QStringLiteral(
            "SELECT v.field_id, v.record_index, v.data, v.updated_at "
            "FROM \"values\" v JOIN fields f ON f.id = v.field_id "
            "WHERE f.table_id = ?"));
        vq.addBindValue(m_currentTableId);
        if (vq.exec()) {
            while (vq.next()) {
                const int fieldId = vq.value(0).toInt();
                const int recIdx = vq.value(1).toInt();
                const QString data = vq.value(2).toString();
                const QString updatedAt = vq.value(3).toString();
                auto &bucket = buckets[recIdx];
                bucket.valuesByField.insert(fieldId, data);
                if (updatedAt > bucket.maxUpdatedAt)
                    bucket.maxUpdatedAt = updatedAt;
            }
        }
    }

    // Keep only non-empty records (matching Table#size in aikku-access).
    QList<int> allRecordIndices;
    for (auto it = buckets.constBegin(); it != buckets.constEnd(); ++it) {
        bool anyNonEmpty = false;
        for (const QString &val : it.value().valuesByField) {
            if (!val.trimmed().isEmpty()) {
                anyNonEmpty = true;
                break;
            }
        }
        if (anyNonEmpty)
            allRecordIndices.append(it.key());
    }
    m_totalRecordCount = allRecordIndices.size();

    // Evaluate formulas dynamically for every record so searches, filters, sorts, and aggregations see fresh values.
    for (int recIdx : std::as_const(allRecordIndices)) {
        auto &bucket = buckets[recIdx];
        QHash<QString, QString> byName;
        for (const FieldRow &fr : std::as_const(fieldRows))
            byName.insert(fr.name, bucket.valuesByField.value(fr.id));
        for (const FieldRow &fr : std::as_const(fieldRows)) {
            if (fr.datatype == QLatin1String("Formula")) {
                const QString computed = evaluateFormula(fr.items, byName);
                bucket.valuesByField.insert(fr.id, computed);
                byName.insert(fr.name, computed);
            }
        }
    }

    // 3. Apply search and field/date filters.
    const QString needle = m_searchText.trimmed();
    QList<int> filteredIndices;
    for (int recIdx : std::as_const(allRecordIndices)) {
        const auto &bucket = buckets[recIdx];

        if (!needle.isEmpty()) {
            bool matchedSearch = false;
            for (const FieldRow &fr : std::as_const(fieldRows)) {
                if (bucket.valuesByField.value(fr.id).contains(needle, Qt::CaseInsensitive)) {
                    matchedSearch = true;
                    break;
                }
            }
            if (!matchedSearch)
                continue;
        }

        bool matchedFilters = true;
        for (auto fit = m_fieldFilters.constBegin(); fit != m_fieldFilters.constEnd(); ++fit) {
            const QString filterVal = fit.value().trimmed();
            if (filterVal.isEmpty())
                continue;
            const int fId = fit.key();
            const QString cellVal = bucket.valuesByField.value(fId);

            // Check if field is Tags or Status.
            QString fType;
            for (const FieldRow &fr : std::as_const(fieldRows)) {
                if (fr.id == fId) {
                    fType = fr.datatype;
                    break;
                }
            }
            if (fType == QLatin1String("Tags")) {
                const QStringList parts = cellVal.split(QLatin1Char(','), Qt::SkipEmptyParts);
                bool hasTag = false;
                for (const QString &p : parts) {
                    if (p.trimmed().compare(filterVal, Qt::CaseInsensitive) == 0) {
                        hasTag = true;
                        break;
                    }
                }
                if (!hasTag) {
                    matchedFilters = false;
                    break;
                }
            } else if (cellVal != filterVal) {
                matchedFilters = false;
                break;
            }
        }
        if (!matchedFilters)
            continue;

        for (auto dit = m_dateFilters.constBegin(); dit != m_dateFilters.constEnd(); ++dit) {
            const QString startDate = dit.value().first.trimmed();
            const QString endDate = dit.value().second.trimmed();
            if (startDate.isEmpty() && endDate.isEmpty())
                continue;
            const QString cellDate = bucket.valuesByField.value(dit.key()).trimmed();
            if (cellDate.isEmpty()) {
                matchedFilters = false;
                break;
            }
            if (!startDate.isEmpty() && cellDate < startDate) {
                matchedFilters = false;
                break;
            }
            if (!endDate.isEmpty() && cellDate > endDate) {
                matchedFilters = false;
                break;
            }
        }
        if (!matchedFilters)
            continue;

        filteredIndices.append(recIdx);
    }

    // 4. Sort filtered records.
    QString sortDatatype;
    for (const FieldRow &fr : std::as_const(fieldRows)) {
        if (fr.id == m_sortFieldId) {
            sortDatatype = fr.datatype;
            break;
        }
    }
    const bool sortNumeric = (sortDatatype == QLatin1String("Number") ||
                              sortDatatype == QLatin1String("Euros") ||
                              sortDatatype == QLatin1String("Formula") ||
                              sortDatatype == QLatin1String("Stars"));

    std::sort(filteredIndices.begin(), filteredIndices.end(), [&](int a, int b) {
        const auto &ba = buckets[a];
        const auto &bb = buckets[b];
        int cmp = 0;
        if (m_sortFieldId <= 0) {
            if (ba.maxUpdatedAt != bb.maxUpdatedAt)
                cmp = (ba.maxUpdatedAt < bb.maxUpdatedAt) ? -1 : 1;
            else
                cmp = (a < b) ? -1 : (a > b ? 1 : 0);
        } else if (sortNumeric) {
            double da = 0.0, dbVal = 0.0;
            const bool oka = parseDoubleFlexible(ba.valuesByField.value(m_sortFieldId), &da);
            const bool okb = parseDoubleFlexible(bb.valuesByField.value(m_sortFieldId), &dbVal);
            if (oka && okb)
                cmp = (da < dbVal) ? -1 : (da > dbVal ? 1 : 0);
            else if (oka != okb)
                cmp = oka ? 1 : -1;
        } else {
            const QString sa = ba.valuesByField.value(m_sortFieldId);
            const QString sb = bb.valuesByField.value(m_sortFieldId);
            cmp = QString::compare(sa, sb, Qt::CaseInsensitive);
        }
        if (cmp == 0)
            cmp = (a < b) ? -1 : (a > b ? 1 : 0);
        return m_sortAscending ? (cmp < 0) : (cmp > 0);
    });

    // 5. Compute footer aggregations (Sum / Average) and filter distinct choices per field.
    m_hasFooterRow = false;
    QVariantList nextFields;
    for (const FieldRow &fr : std::as_const(fieldRows)) {
        QVariantMap fmap;
        fmap.insert(QStringLiteral("id"), fr.id);
        fmap.insert(QStringLiteral("name"), fr.name);
        fmap.insert(QStringLiteral("datatype"), fr.datatype);
        fmap.insert(QStringLiteral("items"), fr.items);
        fmap.insert(QStringLiteral("filtre"), fr.filtre);
        fmap.insert(QStringLiteral("obligatoire"), fr.obligatoire);
        fmap.insert(QStringLiteral("rowOrder"), fr.rowOrder);
        fmap.insert(QStringLiteral("operation"), fr.operation);
        fmap.insert(QStringLiteral("description"), fr.description);

        // Parse choices for List or Status.
        QStringList choices;
        QVariantMap statusColors;
        if (fr.datatype == QLatin1String("List")) {
            const QStringList parts = fr.items.split(QLatin1Char(','), Qt::SkipEmptyParts);
            for (const QString &p : parts)
                choices.append(p.trimmed());
        } else if (fr.datatype == QLatin1String("Status")) {
            const QStringList parts = fr.items.split(QLatin1Char(','), Qt::SkipEmptyParts);
            for (const QString &p : parts) {
                const QString trimmed = p.trimmed();
                const int colon = trimmed.indexOf(QLatin1Char(':'));
                const QString label = (colon >= 0) ? trimmed.left(colon).trimmed() : trimmed;
                const QString colorName = (colon >= 0) ? trimmed.mid(colon + 1).trimmed() : QStringLiteral("gray");
                if (!label.isEmpty()) {
                    choices.append(label);
                    statusColors.insert(label, statusBadgeColor(colorName));
                }
            }
        }
        fmap.insert(QStringLiteral("choices"), choices);
        fmap.insert(QStringLiteral("statusColors"), statusColors);

        // Distinct values across all records for quick filter dropdown.
        QSet<QString> distinctSet;
        for (int recIdx : std::as_const(allRecordIndices)) {
            const QString val = buckets[recIdx].valuesByField.value(fr.id).trimmed();
            if (val.isEmpty())
                continue;
            if (fr.datatype == QLatin1String("Tags")) {
                for (const QString &tag : val.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
                    if (!tag.trimmed().isEmpty())
                        distinctSet.insert(tag.trimmed());
                }
            } else {
                distinctSet.insert(val);
            }
        }
        QStringList filterChoices = distinctSet.values();
        filterChoices.sort(Qt::CaseInsensitive);
        fmap.insert(QStringLiteral("filterChoices"), filterChoices);

        // Footer aggregation over filteredIndices.
        const bool isNumericField = (fr.datatype == QLatin1String("Number") ||
                                     fr.datatype == QLatin1String("Euros") ||
                                     fr.datatype == QLatin1String("Formula") ||
                                     fr.datatype == QLatin1String("Stars"));
        bool hasFooter = false;
        double footerVal = 0.0;
        if (isNumericField && (fr.operation == QLatin1String("Sum") || fr.operation == QLatin1String("Average"))) {
            double sum = 0.0;
            int numericCount = 0;
            for (int recIdx : std::as_const(filteredIndices)) {
                double v = 0.0;
                if (parseDoubleFlexible(buckets[recIdx].valuesByField.value(fr.id), &v)) {
                    sum += v;
                    ++numericCount;
                }
            }
            hasFooter = true;
            m_hasFooterRow = true;
            footerVal = (fr.operation == QLatin1String("Average") && numericCount > 0)
                            ? (sum / numericCount)
                            : sum;
        }
        fmap.insert(QStringLiteral("hasFooter"), hasFooter);
        fmap.insert(QStringLiteral("footerValue"), footerVal);

        // Compute ideal content width based on header, footer, and visible cell values.
        QFont headerFont;
        headerFont.setPixelSize(12);
        headerFont.setWeight(QFont::DemiBold);
        const QFontMetrics headerFm(headerFont);

        QFont cellFont;
        cellFont.setPixelSize(13);
        const QFontMetrics cellFm(cellFont);

        const QString headerText = fr.name + (fr.obligatoire ? QStringLiteral(" *") : QString());
        int contentWidth = std::max(96, headerFm.horizontalAdvance(headerText) + 38);

        if (hasFooter) {
            const int dec = (fr.datatype == QLatin1String("Euros") || fr.operation == QLatin1String("Average")) ? 2 : 0;
            QString footerStr = QStringLiteral("Average: ") + QLocale().toString(footerVal, 'f', dec);
            if (fr.datatype == QLatin1String("Euros"))
                footerStr += QStringLiteral(" €");
            contentWidth = std::max(contentWidth, headerFm.horizontalAdvance(footerStr) + 24);
        }

        for (int recIdx : std::as_const(filteredIndices)) {
            const QString raw = buckets[recIdx].valuesByField.value(fr.id);
            if (raw.isEmpty())
                continue;
            QString displayStr = raw;
            double numVal = 0.0;
            const bool isNum = parseDoubleFlexible(raw, &numVal);
            if (fr.datatype == QLatin1String("Euros") && isNum) {
                displayStr = QLocale().toString(numVal, 'f', 2) + QStringLiteral(" €");
            } else if ((fr.datatype == QLatin1String("Number") || fr.datatype == QLatin1String("Formula")) && isNum) {
                const int dec = raw.contains(QLatin1Char('.')) ? 2 : 0;
                displayStr = QLocale().toString(numVal, 'f', dec);
            } else if (fr.datatype == QLatin1String("Stars")) {
                displayStr = QStringLiteral("★★★★★");
            }
            const int pad = (fr.datatype == QLatin1String("Status") || fr.datatype == QLatin1String("YesNo")) ? 40 : 28;
            contentWidth = std::max(contentWidth, cellFm.horizontalAdvance(displayStr) + pad);
        }
        contentWidth = std::min(contentWidth, 560);
        fmap.insert(QStringLiteral("contentWidth"), contentWidth);

        nextFields.append(fmap);
    }

    // 6. Build record rows for QML.
    QVariantList nextRecords;
    for (int recIdx : std::as_const(filteredIndices)) {
        const auto &bucket = buckets[recIdx];
        QVariantList cells;
        QStringList summaryParts;

        for (int i = 0; i < fieldRows.size(); ++i) {
            const FieldRow &fr = fieldRows.at(i);
            const QVariantMap fmap = nextFields.at(i).toMap();
            const QString raw = bucket.valuesByField.value(fr.id);

            QVariantMap cell;
            cell.insert(QStringLiteral("fieldId"), fr.id);
            cell.insert(QStringLiteral("name"), fr.name);
            cell.insert(QStringLiteral("datatype"), fr.datatype);
            cell.insert(QStringLiteral("rawValue"), raw);

            double numVal = 0.0;
            const bool isNum = parseDoubleFlexible(raw, &numVal);
            cell.insert(QStringLiteral("isNumeric"), isNum);
            cell.insert(QStringLiteral("numericValue"), numVal);

            QString badgeColor;
            if (fr.datatype == QLatin1String("Status") && !raw.isEmpty()) {
                const QVariantMap sColors = fmap.value(QStringLiteral("statusColors")).toMap();
                badgeColor = sColors.value(raw, QStringLiteral("#4b5563")).toString();
            }
            cell.insert(QStringLiteral("badgeColor"), badgeColor);
            cells.append(cell);

            if (!raw.isEmpty() && summaryParts.size() < 2)
                summaryParts.append(raw);
        }

        QVariantMap rowMap;
        rowMap.insert(QStringLiteral("recordIndex"), recIdx);
        rowMap.insert(QStringLiteral("updatedAt"), formatTimestamp(bucket.maxUpdatedAt));
        rowMap.insert(QStringLiteral("summary"),
                      summaryParts.isEmpty() ? QStringLiteral("#%1").arg(recIdx) : summaryParts.join(QStringLiteral(" — ")));
        rowMap.insert(QStringLiteral("cells"), cells);
        nextRecords.append(rowMap);
    }

    m_fields = nextFields;
    m_records = nextRecords;
    if (m_records.isEmpty())
        m_selectedRowIndex = -1;
    else if (m_selectedRowIndex < 0 || m_selectedRowIndex >= m_records.size())
        m_selectedRowIndex = 0;

    emit fieldsChanged();
    emit recordsChanged();
    emit selectedRowIndexChanged();
}

QVariantList Backend::filterableFields() const {
    QVariantList result;
    for (const QVariant &v : m_fields) {
        const QVariantMap map = v.toMap();
        if (map.value(QStringLiteral("filtre")).toBool())
            result.append(map);
    }
    return result;
}

void Backend::setSelectedRowIndex(int index) {
    const int clamped = m_records.isEmpty() ? -1 : std::clamp(index, 0, int(m_records.size()) - 1);
    if (clamped == m_selectedRowIndex)
        return;
    m_selectedRowIndex = clamped;
    emit selectedRowIndexChanged();
}

int Backend::selectedRecordIndex() const {
    if (m_selectedRowIndex < 0 || m_selectedRowIndex >= m_records.size())
        return 0;
    return m_records.at(m_selectedRowIndex).toMap().value(QStringLiteral("recordIndex")).toInt();
}

void Backend::setSearchText(const QString &text) {
    if (text == m_searchText)
        return;
    m_searchText = text;
    emit filtersChanged();
    reloadFieldsAndRecords();
}

bool Backend::hasActiveFilters() const {
    if (!m_searchText.trimmed().isEmpty())
        return true;
    for (const QString &val : m_fieldFilters) {
        if (!val.trimmed().isEmpty())
            return true;
    }
    for (const auto &pair : m_dateFilters) {
        if (!pair.first.trimmed().isEmpty() || !pair.second.trimmed().isEmpty())
            return true;
    }
    return false;
}

int Backend::createTable(const QString &name, bool lifo) {
    clearMessages();
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        setError(tr("Table name cannot be empty."));
        return 0;
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
    if (!db.isOpen())
        return 0;

    pushUndoSnapshot();

    const QString ts = nowIso();
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO tables (name, lifo, record_index, created_at, updated_at) "
        "VALUES (?, ?, 0, ?, ?)"));
    q.addBindValue(trimmed);
    q.addBindValue(lifo ? 1 : 0);
    q.addBindValue(ts);
    q.addBindValue(ts);
    if (!q.exec()) {
        m_undoStack.removeLast();
        emit undoStackChanged();
        setError(tr("Could not create table: %1").arg(q.lastError().text()));
        return 0;
    }

    const int newId = q.lastInsertId().toInt();
    m_currentTableId = newId;
    m_sortFieldId = 0;
    m_sortAscending = !lifo;
    reloadAll();
    setNotice(tr("Created table \"%1\".").arg(trimmed));
    return newId;
}

bool Backend::updateTable(int tableId, const QString &name, bool lifo) {
    clearMessages();
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        setError(tr("Table name cannot be empty."));
        return false;
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
    if (!db.isOpen())
        return false;

    pushUndoSnapshot();

    QSqlQuery q(db);
    q.prepare(QStringLiteral("UPDATE tables SET name = ?, lifo = ?, updated_at = ? WHERE id = ?"));
    q.addBindValue(trimmed);
    q.addBindValue(lifo ? 1 : 0);
    q.addBindValue(nowIso());
    q.addBindValue(tableId);
    if (!q.exec()) {
        m_undoStack.removeLast();
        emit undoStackChanged();
        setError(tr("Could not update table: %1").arg(q.lastError().text()));
        return false;
    }

    reloadAll();
    setNotice(tr("Updated table \"%1\".").arg(trimmed));
    return true;
}

bool Backend::deleteTable(int tableId) {
    clearMessages();
    if (tableId <= 0)
        return false;

    QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
    if (!db.isOpen())
        return false;

    pushUndoSnapshot();

    QSqlQuery q(db);
    q.prepare(QStringLiteral("DELETE FROM tables WHERE id = ?"));
    q.addBindValue(tableId);
    if (!q.exec()) {
        m_undoStack.removeLast();
        emit undoStackChanged();
        setError(tr("Could not delete table: %1").arg(q.lastError().text()));
        return false;
    }

    if (m_currentTableId == tableId)
        m_currentTableId = 0;
    reloadAll();
    setNotice(tr("Table deleted."));
    return true;
}

void Backend::selectTable(int tableId) {
    if (tableId == m_currentTableId)
        return;
    for (const QVariant &v : std::as_const(m_tables)) {
        const QVariantMap map = v.toMap();
        if (map.value(QStringLiteral("id")).toInt() == tableId) {
            m_currentTableId = tableId;
            m_currentTableName = map.value(QStringLiteral("name")).toString();
            m_currentTableLifo = map.value(QStringLiteral("lifo")).toBool();
            m_searchText.clear();
            m_fieldFilters.clear();
            m_dateFilters.clear();
            m_sortFieldId = 0;
            m_sortAscending = !m_currentTableLifo;
            emit currentTableChanged();
            emit filtersChanged();
            emit sortChanged();
            reloadFieldsAndRecords();
            return;
        }
    }
}

void Backend::selectTableByIndex(int index) {
    if (index < 0 || index >= m_tables.size())
        return;
    selectTable(m_tables.at(index).toMap().value(QStringLiteral("id")).toInt());
}

int Backend::createSampleTable() {
    clearMessages();
    const int tableId = createTable(tr("Inventory"), true);
    if (tableId <= 0)
        return 0;

    // Pop the single table undo step and replace with one undo snapshot before the whole sample creation.
    if (!m_undoStack.isEmpty()) {
        const Snapshot beforeSample = m_undoStack.takeLast();
        restoreSnapshot(beforeSample);
        pushUndoSnapshot();
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
    const QString ts = nowIso();
    QSqlQuery q(db);
    q.prepare(QStringLiteral("INSERT INTO tables (name, lifo, record_index, created_at, updated_at) VALUES (?, 1, 0, ?, ?)"));
    q.addBindValue(tr("Inventory"));
    q.addBindValue(ts);
    q.addBindValue(ts);
    if (!q.exec())
        return 0;
    const int tId = q.lastInsertId().toInt();
    m_currentTableId = tId;

    auto insertField = [&](const QString &name, const QString &datatype, const QString &items,
                           bool obligatoire, bool filtre, const QString &operation, int order, const QString &desc) {
        QSqlQuery fq(db);
        fq.prepare(QStringLiteral(
            "INSERT INTO fields (table_id, name, datatype, items, filtre, obligatoire, row_order, operation, description) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"));
        fq.addBindValue(tId);
        fq.addBindValue(name);
        fq.addBindValue(datatype);
        fq.addBindValue(items.isEmpty() ? QStringLiteral("") : items);
        fq.addBindValue(filtre ? 1 : 0);
        fq.addBindValue(obligatoire ? 1 : 0);
        fq.addBindValue(order);
        fq.addBindValue(operation.isEmpty() ? QStringLiteral("") : operation);
        fq.addBindValue(desc.isEmpty() ? QStringLiteral("") : desc);
        fq.exec();
        return fq.lastInsertId().toInt();
    };

    const int fItem = insertField(QStringLiteral("Item"), QStringLiteral("Text"), {}, true, false, {}, 1, QStringLiteral("Equipment name"));
    const int fCat = insertField(QStringLiteral("Category"), QStringLiteral("List"), QStringLiteral("Hardware, Peripherals, Audio, Network"), false, true, {}, 2, QStringLiteral("Asset category"));
    const int fStatus = insertField(QStringLiteral("Status"), QStringLiteral("Status"), QStringLiteral("In Stock:green, Low Stock:yellow, Ordered:blue, Retired:gray"), false, true, {}, 3, QStringLiteral("Current stock status"));
    const int fQty = insertField(QStringLiteral("Qty"), QStringLiteral("Number"), {}, true, false, QStringLiteral("Sum"), 4, QStringLiteral("Units on hand"));
    const int fPrice = insertField(QStringLiteral("Price"), QStringLiteral("Euros"), {}, true, false, QStringLiteral("Average"), 5, QStringLiteral("Unit cost in EUR"));
    const int fTotal = insertField(QStringLiteral("Total"), QStringLiteral("Formula"), QStringLiteral("[Qty] * [Price]"), false, false, QStringLiteral("Sum"), 6, QStringLiteral("Computed value"));
    const int fRating = insertField(QStringLiteral("Rating"), QStringLiteral("Stars"), {}, false, false, {}, 7, QStringLiteral("Condition rating 1-5"));

    struct SampleRow {
        QString item;
        QString cat;
        QString status;
        QString qty;
        QString price;
        QString total;
        QString rating;
    };
    const QList<SampleRow> rows = {
        {QStringLiteral("ThinkPad X1"), QStringLiteral("Hardware"), QStringLiteral("In Stock"), QStringLiteral("6"), QStringLiteral("1450"), QStringLiteral("8700"), QStringLiteral("5")},
        {QStringLiteral("Keychron Q1"), QStringLiteral("Peripherals"), QStringLiteral("In Stock"), QStringLiteral("12"), QStringLiteral("195"), QStringLiteral("2340"), QStringLiteral("5")},
        {QStringLiteral("Shure MV7 Mic"), QStringLiteral("Audio"), QStringLiteral("Low Stock"), QStringLiteral("2"), QStringLiteral("260"), QStringLiteral("520"), QStringLiteral("4")},
        {QStringLiteral("Ubiquiti U7 AP"), QStringLiteral("Network"), QStringLiteral("Ordered"), QStringLiteral("4"), QStringLiteral("189"), QStringLiteral("756"), QStringLiteral("5")},
        {QStringLiteral("Dell 27\" 4K"), QStringLiteral("Peripherals"), QStringLiteral("In Stock"), QStringLiteral("8"), QStringLiteral("540"), QStringLiteral("4320"), QStringLiteral("4")},
    };

    for (int i = 0; i < rows.size(); ++i) {
        const int recIdx = i + 1;
        const SampleRow &r = rows.at(i);
        const QList<QPair<int, QString>> pairs = {
            {fItem, r.item}, {fCat, r.cat}, {fStatus, r.status}, {fQty, r.qty},
            {fPrice, r.price}, {fTotal, r.total}, {fRating, r.rating}
        };
        for (const auto &p : pairs) {
            QSqlQuery vq(db);
            vq.prepare(QStringLiteral(
                "INSERT INTO \"values\" (field_id, record_index, data, old_value, created_at, updated_at) "
                "VALUES (?, ?, ?, '', ?, ?)"));
            vq.addBindValue(p.first);
            vq.addBindValue(recIdx);
            vq.addBindValue(p.second);
            vq.addBindValue(ts);
            vq.addBindValue(ts);
            vq.exec();
        }
    }

    QSqlQuery uq(db);
    uq.prepare(QStringLiteral("UPDATE tables SET record_index = ? WHERE id = ?"));
    uq.addBindValue(rows.size());
    uq.addBindValue(tId);
    uq.exec();

    reloadAll();
    setNotice(tr("Created sample table \"%1\".").arg(tr("Inventory")));
    return tId;
}

bool Backend::validateFieldOptions(const QString &datatype, const QString &items, QString *errorOut) const {
    const QString trimmed = items.trimmed();
    if (datatype == QLatin1String("List")) {
        if (!trimmed.contains(QLatin1Char(','))) {
            if (errorOut)
                *errorOut = tr("List fields require comma-separated options (e.g. \"Todo, Doing, Done\").");
            return false;
        }
    } else if (datatype == QLatin1String("Status")) {
        if (trimmed.isEmpty()) {
            if (errorOut)
                *errorOut = tr("Status fields require Label:color pairs (e.g. \"Open:blue, Done:green\").");
            return false;
        }
        const QStringList parts = trimmed.split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const QString &part : parts) {
            if (part.count(QLatin1Char(':')) != 1) {
                if (errorOut)
                    *errorOut = tr("Each status option must use Label:color format (e.g. \"Open:blue, Closed:green\").");
                return false;
            }
        }
    } else if (datatype == QLatin1String("Formula")) {
        if (!trimmed.contains(QLatin1Char('[')) || !trimmed.contains(QLatin1Char(']'))) {
            if (errorOut)
                *errorOut = tr("Formula fields must reference attributes in brackets (e.g. \"[Qty] * [Unit Price]\").");
            return false;
        }
    }
    return true;
}

int Backend::addField(const QString &name, const QString &datatype, const QString &items,
                      bool obligatoire, bool filtre, const QString &operation, const QString &description) {
    clearMessages();
    if (m_currentTableId <= 0) {
        setError(tr("Select or create a table first."));
        return 0;
    }
    const QString trimmedName = name.trimmed();
    if (trimmedName.isEmpty()) {
        setError(tr("Attribute name cannot be empty."));
        return 0;
    }

    QString optionErr;
    if (!validateFieldOptions(datatype, items, &optionErr)) {
        setError(optionErr);
        return 0;
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
    if (!db.isOpen())
        return 0;

    pushUndoSnapshot();

    int maxOrder = 0;
    {
        QSqlQuery mq(db);
        mq.prepare(QStringLiteral("SELECT COALESCE(MAX(row_order), 0) FROM fields WHERE table_id = ?"));
        mq.addBindValue(m_currentTableId);
        if (mq.exec() && mq.next())
            maxOrder = mq.value(0).toInt();
    }

    auto nonNull = [](const QString &s) {
        const QString t = s.trimmed();
        return t.isEmpty() ? QStringLiteral("") : t;
    };

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO fields (table_id, name, datatype, items, filtre, obligatoire, row_order, operation, description) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(m_currentTableId);
    q.addBindValue(trimmedName);
    q.addBindValue(datatype.isEmpty() ? QStringLiteral("Text") : datatype);
    q.addBindValue(nonNull(items));
    q.addBindValue(filtre ? 1 : 0);
    q.addBindValue(obligatoire ? 1 : 0);
    q.addBindValue(maxOrder + 1);
    q.addBindValue(nonNull(operation));
    q.addBindValue(nonNull(description));
    if (!q.exec()) {
        m_undoStack.removeLast();
        emit undoStackChanged();
        setError(tr("Could not add attribute: %1").arg(q.lastError().text()));
        return 0;
    }

    const int newFieldId = q.lastInsertId().toInt();
    reloadAll();
    setNotice(tr("Added attribute \"%1\".").arg(trimmedName));
    return newFieldId;
}

bool Backend::updateField(int fieldId, const QString &name, const QString &datatype, const QString &items,
                          bool obligatoire, bool filtre, const QString &operation, const QString &description) {
    clearMessages();
    const QString trimmedName = name.trimmed();
    if (trimmedName.isEmpty()) {
        setError(tr("Attribute name cannot be empty."));
        return false;
    }

    QString optionErr;
    if (!validateFieldOptions(datatype, items, &optionErr)) {
        setError(optionErr);
        return false;
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
    if (!db.isOpen())
        return false;

    pushUndoSnapshot();

    auto nonNull = [](const QString &s) {
        const QString t = s.trimmed();
        return t.isEmpty() ? QStringLiteral("") : t;
    };

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "UPDATE fields SET name = ?, datatype = ?, items = ?, filtre = ?, obligatoire = ?, operation = ?, description = ? "
        "WHERE id = ?"));
    q.addBindValue(trimmedName);
    q.addBindValue(datatype);
    q.addBindValue(nonNull(items));
    q.addBindValue(filtre ? 1 : 0);
    q.addBindValue(obligatoire ? 1 : 0);
    q.addBindValue(nonNull(operation));
    q.addBindValue(nonNull(description));
    q.addBindValue(fieldId);
    if (!q.exec()) {
        m_undoStack.removeLast();
        emit undoStackChanged();
        setError(tr("Could not update attribute: %1").arg(q.lastError().text()));
        return false;
    }

    reloadAll();
    setNotice(tr("Updated attribute \"%1\".").arg(trimmedName));
    return true;
}

bool Backend::deleteField(int fieldId) {
    clearMessages();
    if (fieldId <= 0)
        return false;

    QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
    if (!db.isOpen())
        return false;

    pushUndoSnapshot();

    QSqlQuery q(db);
    q.prepare(QStringLiteral("DELETE FROM fields WHERE id = ?"));
    q.addBindValue(fieldId);
    if (!q.exec()) {
        m_undoStack.removeLast();
        emit undoStackChanged();
        setError(tr("Could not delete attribute: %1").arg(q.lastError().text()));
        return false;
    }

    m_fieldFilters.remove(fieldId);
    m_dateFilters.remove(fieldId);
    if (m_sortFieldId == fieldId)
        m_sortFieldId = 0;

    reloadAll();
    setNotice(tr("Attribute deleted."));
    return true;
}

bool Backend::moveField(int fieldId, int delta) {
    clearMessages();
    if (delta == 0 || m_fields.size() < 2)
        return false;

    int idx = -1;
    for (int i = 0; i < m_fields.size(); ++i) {
        if (m_fields.at(i).toMap().value(QStringLiteral("id")).toInt() == fieldId) {
            idx = i;
            break;
        }
    }
    const int targetIdx = idx + (delta < 0 ? -1 : 1);
    if (idx < 0 || targetIdx < 0 || targetIdx >= m_fields.size())
        return false;

    const int otherId = m_fields.at(targetIdx).toMap().value(QStringLiteral("id")).toInt();
    QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
    if (!db.isOpen())
        return false;

    pushUndoSnapshot();

    QList<int> orderedIds;
    for (const QVariant &v : std::as_const(m_fields))
        orderedIds.append(v.toMap().value(QStringLiteral("id")).toInt());
    orderedIds.swapItemsAt(idx, targetIdx);
    Q_UNUSED(otherId);

    for (int i = 0; i < orderedIds.size(); ++i) {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("UPDATE fields SET row_order = ? WHERE id = ?"));
        q.addBindValue(i + 1);
        q.addBindValue(orderedIds.at(i));
        q.exec();
    }

    reloadAll();
    return true;
}

QVariantMap Backend::fieldDetails(int fieldId) const {
    for (const QVariant &v : m_fields) {
        const QVariantMap map = v.toMap();
        if (map.value(QStringLiteral("id")).toInt() == fieldId)
            return map;
    }
    return {};
}

int Backend::saveRecord(int recordIndex, const QVariantMap &fieldValues) {
    clearMessages();
    if (m_currentTableId <= 0) {
        setError(tr("Select a table first."));
        return 0;
    }
    if (m_fields.isEmpty()) {
        setError(tr("Add at least one attribute before creating records."));
        return 0;
    }

    // Validate required fields and ensure at least one non-empty value.
    bool anyValuePresent = false;
    QHash<QString, QString> nameToValue;
    for (const QVariant &fv : std::as_const(m_fields)) {
        const QVariantMap fmap = fv.toMap();
        const int fId = fmap.value(QStringLiteral("id")).toInt();
        const QString fName = fmap.value(QStringLiteral("name")).toString();
        const QString fType = fmap.value(QStringLiteral("datatype")).toString();
        const bool required = fmap.value(QStringLiteral("obligatoire")).toBool();
        const QString val = fieldValues.value(QString::number(fId)).toString().trimmed();

        if (fType != QLatin1String("Formula")) {
            if (required && val.isEmpty()) {
                setError(tr("Required field \"%1\" is missing.").arg(fName));
                return 0;
            }
            if (!val.isEmpty())
                anyValuePresent = true;
            nameToValue.insert(fName, val);
        }
    }

    if (!anyValuePresent) {
        setError(tr("Cannot save an empty record."));
        return 0;
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
    if (!db.isOpen())
        return 0;

    pushUndoSnapshot();

    const bool isUpdate = (recordIndex > 0);
    int targetRecordIndex = recordIndex;
    const QString ts = nowIso();

    db.transaction();

    if (!isUpdate) {
        QSqlQuery rq(db);
        rq.prepare(QStringLiteral("SELECT record_index FROM tables WHERE id = ?"));
        rq.addBindValue(m_currentTableId);
        if (!rq.exec() || !rq.next()) {
            db.rollback();
            m_undoStack.removeLast();
            emit undoStackChanged();
            setError(tr("Could not allocate record index."));
            return 0;
        }
        targetRecordIndex = rq.value(0).toInt() + 1;

        QSqlQuery uq(db);
        uq.prepare(QStringLiteral("UPDATE tables SET record_index = ?, updated_at = ? WHERE id = ?"));
        uq.addBindValue(targetRecordIndex);
        uq.addBindValue(ts);
        uq.addBindValue(m_currentTableId);
        uq.exec();
    }

    for (const QVariant &fv : std::as_const(m_fields)) {
        const QVariantMap fmap = fv.toMap();
        const int fId = fmap.value(QStringLiteral("id")).toInt();
        const QString fName = fmap.value(QStringLiteral("name")).toString();
        const QString fType = fmap.value(QStringLiteral("datatype")).toString();
        const QString fItems = fmap.value(QStringLiteral("items")).toString();

        QString newVal = fieldValues.value(QString::number(fId)).toString().trimmed();
        if (fType == QLatin1String("Formula")) {
            newVal = evaluateFormula(fItems, nameToValue);
            nameToValue.insert(fName, newVal);
        }
        if (newVal.isEmpty())
            newVal = QStringLiteral("");

        // Check existing value for (field_id, record_index).
        QSqlQuery eq(db);
        eq.prepare(QStringLiteral("SELECT id, data, created_at FROM \"values\" WHERE field_id = ? AND record_index = ?"));
        eq.addBindValue(fId);
        eq.addBindValue(targetRecordIndex);
        if (eq.exec() && eq.next()) {
            const int valId = eq.value(0).toInt();
            const QString oldData = eq.value(1).toString();
            if (oldData != newVal) {
                QSqlQuery uq(db);
                uq.prepare(QStringLiteral("UPDATE \"values\" SET data = ?, old_value = ?, updated_at = ? WHERE id = ?"));
                uq.addBindValue(newVal);
                uq.addBindValue(oldData);
                uq.addBindValue(ts);
                uq.addBindValue(valId);
                uq.exec();
            }
        } else {
            QSqlQuery iq(db);
            iq.prepare(QStringLiteral(
                "INSERT INTO \"values\" (field_id, record_index, data, old_value, created_at, updated_at) "
                "VALUES (?, ?, ?, '', ?, ?)"));
            iq.addBindValue(fId);
            iq.addBindValue(targetRecordIndex);
            iq.addBindValue(newVal);
            iq.addBindValue(ts);
            iq.addBindValue(ts);
            iq.exec();
        }
    }

    db.commit();
    reloadAll();

    // Select the saved record row in the table view.
    for (int i = 0; i < m_records.size(); ++i) {
        if (m_records.at(i).toMap().value(QStringLiteral("recordIndex")).toInt() == targetRecordIndex) {
            setSelectedRowIndex(i);
            break;
        }
    }

    setNotice(isUpdate ? tr("Updated record #%1.").arg(targetRecordIndex)
                       : tr("Added record #%1.").arg(targetRecordIndex));
    return targetRecordIndex;
}

bool Backend::deleteRecord(int recordIndex) {
    clearMessages();
    if (m_currentTableId <= 0 || recordIndex <= 0)
        return false;

    QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
    if (!db.isOpen())
        return false;

    pushUndoSnapshot();

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "DELETE FROM \"values\" WHERE record_index = ? AND field_id IN "
        "(SELECT id FROM fields WHERE table_id = ?)"));
    q.addBindValue(recordIndex);
    q.addBindValue(m_currentTableId);
    if (!q.exec()) {
        m_undoStack.removeLast();
        emit undoStackChanged();
        setError(tr("Could not delete record: %1").arg(q.lastError().text()));
        return false;
    }

    reloadAll();
    setNotice(tr("Deleted record #%1.").arg(recordIndex));
    return true;
}

QVariantMap Backend::recordValues(int recordIndex) const {
    QVariantMap result;
    if (recordIndex <= 0 || m_currentTableId <= 0)
        return result;

    for (const QVariant &rv : m_records) {
        const QVariantMap rmap = rv.toMap();
        if (rmap.value(QStringLiteral("recordIndex")).toInt() == recordIndex) {
            const QVariantList cells = rmap.value(QStringLiteral("cells")).toList();
            for (const QVariant &cv : cells) {
                const QVariantMap cmap = cv.toMap();
                result.insert(QString::number(cmap.value(QStringLiteral("fieldId")).toInt()),
                              cmap.value(QStringLiteral("rawValue")).toString());
            }
            return result;
        }
    }

    // Fallback to querying SQLite directly in case the record is currently filtered out.
    QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
    if (!db.isOpen())
        return result;
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT v.field_id, v.data FROM \"values\" v JOIN fields f ON f.id = v.field_id "
        "WHERE f.table_id = ? AND v.record_index = ?"));
    q.addBindValue(m_currentTableId);
    q.addBindValue(recordIndex);
    if (q.exec()) {
        while (q.next())
            result.insert(QString::number(q.value(0).toInt()), q.value(1).toString());
    }
    return result;
}

void Backend::setFieldFilter(int fieldId, const QString &value) {
    if (value.trimmed().isEmpty())
        m_fieldFilters.remove(fieldId);
    else
        m_fieldFilters.insert(fieldId, value.trimmed());
    emit filtersChanged();
    reloadFieldsAndRecords();
}

QString Backend::fieldFilter(int fieldId) const {
    return m_fieldFilters.value(fieldId);
}

void Backend::setDateFilter(int fieldId, const QString &startDate, const QString &endDate) {
    if (startDate.trimmed().isEmpty() && endDate.trimmed().isEmpty())
        m_dateFilters.remove(fieldId);
    else
        m_dateFilters.insert(fieldId, qMakePair(startDate.trimmed(), endDate.trimmed()));
    emit filtersChanged();
    reloadFieldsAndRecords();
}

QString Backend::dateFilterStart(int fieldId) const {
    return m_dateFilters.value(fieldId).first;
}

QString Backend::dateFilterEnd(int fieldId) const {
    return m_dateFilters.value(fieldId).second;
}

void Backend::clearFilters() {
    m_searchText.clear();
    m_fieldFilters.clear();
    m_dateFilters.clear();
    emit filtersChanged();
    reloadFieldsAndRecords();
}

void Backend::toggleSort(int fieldId) {
    if (m_sortFieldId == fieldId) {
        m_sortAscending = !m_sortAscending;
    } else {
        m_sortFieldId = fieldId;
        m_sortAscending = true;
    }
    emit sortChanged();
    reloadFieldsAndRecords();
}

bool Backend::openDatabaseFile(const QString &filePath) {
    clearMessages();
    if (filePath.trimmed().isEmpty())
        return false;
    if (!initDatabase(filePath.trimmed()))
        return false;
    setNotice(tr("Opened database \"%1\".").arg(databaseName()));
    return true;
}

void Backend::openDatabaseDialog() {
    if (!m_filePicker) {
        setError(tr("No file picker is configured."));
        return;
    }
    m_filePicker->openDatabase();
}

QList<QStringList> Backend::parseCsv(const QString &content, QChar separator) {
    QList<QStringList> rows;
    QStringList currentRow;
    QString currentCell;
    bool inQuotes = false;

    for (int i = 0; i < content.size(); ++i) {
        const QChar ch = content.at(i);
        if (inQuotes) {
            if (ch == QLatin1Char('"')) {
                if (i + 1 < content.size() && content.at(i + 1) == QLatin1Char('"')) {
                    currentCell.append(QLatin1Char('"'));
                    ++i;
                } else {
                    inQuotes = false;
                }
            } else {
                currentCell.append(ch);
            }
        } else {
            if (ch == QLatin1Char('"')) {
                inQuotes = true;
            } else if (ch == separator) {
                currentRow.append(currentCell);
                currentCell.clear();
            } else if (ch == QLatin1Char('\r') || ch == QLatin1Char('\n')) {
                if (ch == QLatin1Char('\r') && i + 1 < content.size() && content.at(i + 1) == QLatin1Char('\n'))
                    ++i;
                currentRow.append(currentCell);
                currentCell.clear();
                bool nonEmpty = false;
                for (const QString &c : std::as_const(currentRow)) {
                    if (!c.trimmed().isEmpty()) {
                        nonEmpty = true;
                        break;
                    }
                }
                if (nonEmpty)
                    rows.append(currentRow);
                currentRow.clear();
            } else {
                currentCell.append(ch);
            }
        }
    }

    if (!currentCell.isEmpty() || !currentRow.isEmpty()) {
        currentRow.append(currentCell);
        bool nonEmpty = false;
        for (const QString &c : std::as_const(currentRow)) {
            if (!c.trimmed().isEmpty()) {
                nonEmpty = true;
                break;
            }
        }
        if (nonEmpty)
            rows.append(currentRow);
    }
    return rows;
}

bool Backend::importCsvFile(const QString &filePath, int targetTableId, const QString &separator) {
    clearMessages();
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setError(tr("Could not read CSV file: %1").arg(file.errorString()));
        return false;
    }

    const QString content = QString::fromUtf8(file.readAll());
    file.close();

    QChar sep = separator.isEmpty() ? QLatin1Char(',') : separator.at(0);
    // Auto-detect semicolon if first line has semicolons and no commas.
    const QString firstLine = content.section(QLatin1Char('\n'), 0, 0);
    if (separator == QLatin1String(",") && !firstLine.contains(QLatin1Char(',')) && firstLine.contains(QLatin1Char(';')))
        sep = QLatin1Char(';');

    const QList<QStringList> rows = parseCsv(content, sep);
    if (rows.isEmpty()) {
        setError(tr("CSV file is empty."));
        return false;
    }

    const QStringList headers = rows.first();
    if (headers.isEmpty()) {
        setError(tr("CSV file has no column headers."));
        return false;
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
    if (!db.isOpen())
        return false;

    pushUndoSnapshot();
    db.transaction();

    const QString ts = nowIso();
    int tableId = targetTableId;
    QList<int> fieldIds;
    int recordIndex = 0;

    if (tableId > 0) {
        QSqlQuery tq(db);
        tq.prepare(QStringLiteral("SELECT record_index FROM tables WHERE id = ?"));
        tq.addBindValue(tableId);
        if (!tq.exec() || !tq.next()) {
            db.rollback();
            m_undoStack.removeLast();
            emit undoStackChanged();
            setError(tr("Target table not found."));
            return false;
        }
        recordIndex = tq.value(0).toInt();

        QSqlQuery fq(db);
        fq.prepare(QStringLiteral("SELECT id FROM fields WHERE table_id = ? ORDER BY row_order ASC, id ASC"));
        fq.addBindValue(tableId);
        if (fq.exec()) {
            while (fq.next())
                fieldIds.append(fq.value(0).toInt());
        }
    } else {
        QString tableName = QFileInfo(filePath).completeBaseName().trimmed();
        if (tableName.isEmpty())
            tableName = tr("Imported");

        QSqlQuery tq(db);
        tq.prepare(QStringLiteral(
            "INSERT INTO tables (name, lifo, record_index, created_at, updated_at) "
            "VALUES (?, 1, 0, ?, ?)"));
        tq.addBindValue(tableName);
        tq.addBindValue(ts);
        tq.addBindValue(ts);
        if (!tq.exec()) {
            db.rollback();
            m_undoStack.removeLast();
            emit undoStackChanged();
            setError(tr("Could not create table for CSV import: %1").arg(tq.lastError().text()));
            return false;
        }
        tableId = tq.lastInsertId().toInt();

        const QStringList sampleRow = (rows.size() > 1) ? rows.at(1) : QStringList();
        for (int col = 0; col < headers.size(); ++col) {
            const QString colName = headers.at(col).trimmed().isEmpty()
                                        ? QStringLiteral("Column %1").arg(col + 1)
                                        : headers.at(col).trimmed();
            const QString sampleVal = (col < sampleRow.size()) ? sampleRow.at(col).trimmed() : QString();
            const QString detectedType = detectDatatype(sampleVal);

            QSqlQuery fq(db);
            fq.prepare(QStringLiteral(
                "INSERT INTO fields (table_id, name, datatype, items, filtre, obligatoire, row_order, operation, description) "
                "VALUES (?, ?, ?, '', 0, 0, ?, '', '')"));
            fq.addBindValue(tableId);
            fq.addBindValue(colName);
            fq.addBindValue(detectedType);
            fq.addBindValue(col + 1);
            if (!fq.exec()) {
                db.rollback();
                m_undoStack.removeLast();
                emit undoStackChanged();
                setError(tr("Could not create attribute during CSV import: %1").arg(fq.lastError().text()));
                return false;
            }
            fieldIds.append(fq.lastInsertId().toInt());
        }
    }

    int importedRows = 0;
    for (int r = 1; r < rows.size(); ++r) {
        const QStringList &row = rows.at(r);
        ++recordIndex;
        ++importedRows;
        const int colLimit = std::min(row.size(), fieldIds.size());
        for (int col = 0; col < colLimit; ++col) {
            QString cellData = row.at(col).trimmed();
            if (cellData.compare(QLatin1String("oui"), Qt::CaseInsensitive) == 0)
                cellData = QStringLiteral("true");
            else if (cellData.compare(QLatin1String("non"), Qt::CaseInsensitive) == 0)
                cellData = QStringLiteral("false");

            QSqlQuery vq(db);
            vq.prepare(QStringLiteral(
                "INSERT INTO \"values\" (field_id, record_index, data, old_value, created_at, updated_at) "
                "VALUES (?, ?, ?, '', ?, ?)"));
            vq.addBindValue(fieldIds.at(col));
            vq.addBindValue(recordIndex);
            vq.addBindValue(cellData);
            vq.addBindValue(ts);
            vq.addBindValue(ts);
            if (!vq.exec()) {
                db.rollback();
                m_undoStack.removeLast();
                emit undoStackChanged();
                setError(tr("Failed to insert CSV row %1: %2").arg(r).arg(vq.lastError().text()));
                return false;
            }
        }
    }

    QSqlQuery uq(db);
    uq.prepare(QStringLiteral("UPDATE tables SET record_index = ?, updated_at = ? WHERE id = ?"));
    uq.addBindValue(recordIndex);
    uq.addBindValue(ts);
    uq.addBindValue(tableId);
    uq.exec();

    db.commit();
    m_currentTableId = tableId;
    reloadAll();
    setNotice(tr("Imported %n records from CSV.", "", importedRows));
    return true;
}

void Backend::importCsvDialog(int targetTableId) {
    if (!m_filePicker) {
        setError(tr("No file picker is configured."));
        return;
    }
    m_pendingImportTableId = targetTableId;
    m_filePicker->openCsv();
}

bool Backend::exportCsvFile(const QString &filePath) {
    clearMessages();
    if (m_currentTableId <= 0 || m_fields.isEmpty()) {
        setError(tr("No table with attributes is selected to export."));
        return false;
    }

    QSaveFile saveFile(filePath);
    if (!saveFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        setError(tr("Could not write CSV file: %1").arg(saveFile.errorString()));
        return false;
    }

    QStringList headerCells;
    for (const QVariant &fv : std::as_const(m_fields))
        headerCells.append(escapeCsvCell(fv.toMap().value(QStringLiteral("name")).toString()));
    saveFile.write((headerCells.join(QLatin1Char(',')) + QLatin1Char('\n')).toUtf8());

    for (const QVariant &rv : std::as_const(m_records)) {
        const QVariantList cells = rv.toMap().value(QStringLiteral("cells")).toList();
        QStringList rowCells;
        for (const QVariant &cv : cells) {
            QString val = cv.toMap().value(QStringLiteral("rawValue")).toString();
            rowCells.append(escapeCsvCell(val));
        }
        saveFile.write((rowCells.join(QLatin1Char(',')) + QLatin1Char('\n')).toUtf8());
    }

    if (!saveFile.commit()) {
        setError(tr("Failed to save CSV export: %1").arg(saveFile.errorString()));
        return false;
    }

    setNotice(tr("Exported %n records to \"%1\".", "", m_records.size()).arg(QFileInfo(filePath).fileName()));
    return true;
}

void Backend::exportCsvDialog() {
    if (!m_filePicker) {
        setError(tr("No file picker is configured."));
        return;
    }
    if (m_currentTableId <= 0) {
        setError(tr("Select a table to export first."));
        return;
    }
    const QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString folder = QDir(docs).exists() ? docs : QDir::homePath();
    QString safeName = m_currentTableName;
    safeName.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]+")), QStringLiteral("_"));
    const QString suggested = folder + QStringLiteral("/Export_%1.csv").arg(safeName.isEmpty() ? QStringLiteral("Table") : safeName);
    m_filePicker->exportCsv(QUrl::fromLocalFile(suggested));
}

bool Backend::handleDroppedUrls(const QList<QUrl> &urls) {
    bool handled = false;
    for (const QUrl &url : urls) {
        const QString local = url.toLocalFile();
        if (local.isEmpty())
            continue;
        if (local.endsWith(QLatin1String(".csv"), Qt::CaseInsensitive)) {
            if (importCsvFile(local, 0))
                handled = true;
        } else if (local.endsWith(QLatin1String(".sqlite"), Qt::CaseInsensitive) ||
                   local.endsWith(QLatin1String(".sqlite3"), Qt::CaseInsensitive) ||
                   local.endsWith(QLatin1String(".db"), Qt::CaseInsensitive)) {
            if (openDatabaseFile(local))
                handled = true;
        }
    }
    return handled;
}

Backend::Snapshot Backend::captureSnapshot() const {
    Snapshot snap;
    snap.currentTableId = m_currentTableId;

    QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
    if (!db.isOpen())
        return snap;

    {
        QSqlQuery q(db);
        if (q.exec(QStringLiteral("SELECT id, name, lifo, record_index, created_at, updated_at FROM tables ORDER BY id ASC"))) {
            while (q.next()) {
                TableRow tr;
                tr.id = q.value(0).toInt();
                tr.name = q.value(1).toString();
                tr.lifo = q.value(2).toInt() != 0;
                tr.recordIndex = q.value(3).toInt();
                tr.createdAt = q.value(4).toString();
                tr.updatedAt = q.value(5).toString();
                snap.tables.append(tr);
            }
        }
    }
    {
        QSqlQuery q(db);
        if (q.exec(QStringLiteral(
                "SELECT id, table_id, name, datatype, items, filtre, obligatoire, row_order, operation, description "
                "FROM fields ORDER BY id ASC"))) {
            while (q.next()) {
                FieldRow fr;
                fr.id = q.value(0).toInt();
                fr.tableId = q.value(1).toInt();
                fr.name = q.value(2).toString();
                fr.datatype = q.value(3).toString();
                fr.items = q.value(4).toString();
                fr.filtre = q.value(5).toInt() != 0;
                fr.obligatoire = q.value(6).toInt() != 0;
                fr.rowOrder = q.value(7).toInt();
                fr.operation = q.value(8).toString();
                fr.description = q.value(9).toString();
                snap.fields.append(fr);
            }
        }
    }
    {
        QSqlQuery q(db);
        if (q.exec(QStringLiteral(
                "SELECT id, field_id, record_index, data, old_value, created_at, updated_at "
                "FROM \"values\" ORDER BY id ASC"))) {
            while (q.next()) {
                ValueRow vr;
                vr.id = q.value(0).toInt();
                vr.fieldId = q.value(1).toInt();
                vr.recordIndex = q.value(2).toInt();
                vr.data = q.value(3).toString();
                vr.oldValue = q.value(4).toString();
                vr.createdAt = q.value(5).toString();
                vr.updatedAt = q.value(6).toString();
                snap.values.append(vr);
            }
        }
    }
    return snap;
}

bool Backend::restoreSnapshot(const Snapshot &snapshot) {
    QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
    if (!db.isOpen())
        return false;

    db.transaction();
    QSqlQuery q(db);
    q.exec(QStringLiteral("DELETE FROM \"values\""));
    q.exec(QStringLiteral("DELETE FROM fields"));
    q.exec(QStringLiteral("DELETE FROM tables"));

    auto nonNull = [](const QString &s) {
        return s.isEmpty() ? QStringLiteral("") : s;
    };

    for (const TableRow &tr : snapshot.tables) {
        QSqlQuery iq(db);
        iq.prepare(QStringLiteral(
            "INSERT INTO tables (id, name, lifo, record_index, created_at, updated_at) "
            "VALUES (?, ?, ?, ?, ?, ?)"));
        iq.addBindValue(tr.id);
        iq.addBindValue(nonNull(tr.name));
        iq.addBindValue(tr.lifo ? 1 : 0);
        iq.addBindValue(tr.recordIndex);
        iq.addBindValue(nonNull(tr.createdAt));
        iq.addBindValue(nonNull(tr.updatedAt));
        iq.exec();
    }
    for (const FieldRow &fr : snapshot.fields) {
        QSqlQuery iq(db);
        iq.prepare(QStringLiteral(
            "INSERT INTO fields (id, table_id, name, datatype, items, filtre, obligatoire, row_order, operation, description) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
        iq.addBindValue(fr.id);
        iq.addBindValue(fr.tableId);
        iq.addBindValue(nonNull(fr.name));
        iq.addBindValue(nonNull(fr.datatype));
        iq.addBindValue(nonNull(fr.items));
        iq.addBindValue(fr.filtre ? 1 : 0);
        iq.addBindValue(fr.obligatoire ? 1 : 0);
        iq.addBindValue(fr.rowOrder);
        iq.addBindValue(nonNull(fr.operation));
        iq.addBindValue(nonNull(fr.description));
        iq.exec();
    }
    for (const ValueRow &vr : snapshot.values) {
        QSqlQuery iq(db);
        iq.prepare(QStringLiteral(
            "INSERT INTO \"values\" (id, field_id, record_index, data, old_value, created_at, updated_at) "
            "VALUES (?, ?, ?, ?, ?, ?, ?)"));
        iq.addBindValue(vr.id);
        iq.addBindValue(vr.fieldId);
        iq.addBindValue(vr.recordIndex);
        iq.addBindValue(nonNull(vr.data));
        iq.addBindValue(nonNull(vr.oldValue));
        iq.addBindValue(nonNull(vr.createdAt));
        iq.addBindValue(nonNull(vr.updatedAt));
        iq.exec();
    }
    db.commit();

    m_currentTableId = snapshot.currentTableId;
    reloadAll();
    return true;
}

void Backend::pushUndoSnapshot() {
    m_undoStack.append(captureSnapshot());
    if (m_undoStack.size() > 50)
        m_undoStack.removeFirst();
    m_redoStack.clear();
    emit undoStackChanged();
}

void Backend::undo() {
    clearMessages();
    if (m_undoStack.isEmpty())
        return;
    m_redoStack.append(captureSnapshot());
    const Snapshot prev = m_undoStack.takeLast();
    restoreSnapshot(prev);
    emit undoStackChanged();
    setNotice(tr("Undid last change."));
}

void Backend::redo() {
    clearMessages();
    if (m_redoStack.isEmpty())
        return;
    m_undoStack.append(captureSnapshot());
    const Snapshot next = m_redoStack.takeLast();
    restoreSnapshot(next);
    emit undoStackChanged();
    setNotice(tr("Redid change."));
}

void Backend::clearMessages() {
    if (m_errorMessage.isEmpty() && m_noticeMessage.isEmpty())
        return;
    m_errorMessage.clear();
    m_noticeMessage.clear();
    emit statusChanged();
}

void Backend::setError(const QString &message) {
    m_errorMessage = message;
    m_noticeMessage.clear();
    emit statusChanged();
}

void Backend::setNotice(const QString &message) {
    m_noticeMessage = message;
    m_errorMessage.clear();
    emit statusChanged();
}

// Follows Aikku Access ImportCollection#detect_string_type:
// YesNo for true/false/oui/non, Number for integers/decimals, Date for valid dates, otherwise Text.
QString Backend::detectDatatype(const QString &sample) {
    const QString s = sample.trimmed();
    if (s.isEmpty())
        return QStringLiteral("Text");

    const QString lower = s.toLower();
    if (lower == QLatin1String("true") || lower == QLatin1String("false") ||
        lower == QLatin1String("oui") || lower == QLatin1String("non")) {
        return QStringLiteral("YesNo");
    }

    bool intOk = false;
    s.toLongLong(&intOk);
    if (intOk)
        return QStringLiteral("Number");

    bool dblOk = false;
    s.toDouble(&dblOk);
    if (dblOk)
        return QStringLiteral("Number");

    const QDate isoDate = QDate::fromString(s, Qt::ISODate);
    if (isoDate.isValid())
        return QStringLiteral("Date");

    return QStringLiteral("Text");
}

// Follows Aikku Access Field#evaluate: replaces [Field Name] with numeric values and evaluates.
QString Backend::evaluateFormula(const QString &formulaExpr,
                                 const QHash<QString, QString> &fieldNameToValue,
                                 bool *okOut) {
    if (okOut)
        *okOut = false;
    QString expr = formulaExpr;
    static const QRegularExpression bracketRe(QStringLiteral("\\[([^\\]]+)\\]"));
    auto it = bracketRe.globalMatch(formulaExpr);
    if (!it.hasNext())
        return QStringLiteral("n/a");

    while (it.hasNext()) {
        const auto match = it.next();
        const QString operandName = match.captured(1).trimmed();
        QString rawVal;
        bool found = false;
        for (auto fit = fieldNameToValue.constBegin(); fit != fieldNameToValue.constEnd(); ++fit) {
            if (fit.key().trimmed().compare(operandName, Qt::CaseInsensitive) == 0) {
                rawVal = fit.value().trimmed();
                found = true;
                break;
            }
        }
        double num = 0.0;
        if (!found || rawVal.isEmpty() || !parseDoubleFlexible(rawVal, &num))
            return QStringLiteral("n/a");
        expr.replace(match.captured(0), QString::number(num, 'g', 12));
    }

    bool parseOk = false;
    ArithmeticParser parser(expr);
    const double val = parser.parse(&parseOk);
    if (!parseOk)
        return QStringLiteral("err");

    if (okOut)
        *okOut = true;
    // Format whole integers without trailing .00, and decimals cleanly.
    if (std::abs(val - std::round(val)) < 1e-9)
        return QString::number(static_cast<long long>(std::llround(val)));
    return QString::number(val, 'f', 2);
}

// Follows Aikku Access Field::COLOR_MAPPINGS (French and English names).
QString Backend::statusBadgeColor(const QString &colorName) {
    QString key = colorName.toLower();
    key.remove(QLatin1Char(' ')).remove(QLatin1Char('_')).remove(QLatin1Char('-'));
    if (key == QLatin1String("bleu") || key == QLatin1String("blue") || key == QLatin1String("primary"))
        return QStringLiteral("#2563eb");
    if (key == QLatin1String("vert") || key == QLatin1String("green") || key == QLatin1String("success"))
        return QStringLiteral("#16a34a");
    if (key == QLatin1String("jaune") || key == QLatin1String("yellow") || key == QLatin1String("warning") || key == QLatin1String("orange"))
        return QStringLiteral("#d97706");
    if (key == QLatin1String("rouge") || key == QLatin1String("red") || key == QLatin1String("danger"))
        return QStringLiteral("#dc2626");
    if (key == QLatin1String("bleuclair") || key == QLatin1String("lightblue") || key == QLatin1String("info") || key == QLatin1String("cyan"))
        return QStringLiteral("#0891b2");
    if (key == QLatin1String("violet") || key == QLatin1String("purple"))
        return QStringLiteral("#7c3aed");
    if (key == QLatin1String("blanc") || key == QLatin1String("white") || key == QLatin1String("light"))
        return QStringLiteral("#64748b");
    if (key == QLatin1String("noir") || key == QLatin1String("black") || key == QLatin1String("dark"))
        return QStringLiteral("#1f2937");
    return QStringLiteral("#4b5563");
}
