import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 1120
    height: 720
    minimumWidth: 780
    minimumHeight: 520
    visible: true
    title: backend.currentTableName === "" ? "aikku-database" : "aikku-database — " + backend.currentTableName
    color: "#0e0e10"

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    // Modal state
    property bool helpVisible: false
    property bool tableDialogVisible: false
    property int editingTableId: 0
    property string tableDialogName: ""
    property bool tableDialogLifo: true

    property bool fieldsDialogVisible: false
    property int editingFieldId: 0
    property string fieldFormName: ""
    property string fieldFormDatatype: "Text"
    property string fieldFormItems: ""
    property bool fieldFormObligatoire: false
    property bool fieldFormFiltre: false
    property string fieldFormOperation: ""
    property string fieldFormDescription: ""

    property bool recordDialogVisible: false
    property int editingRecordIndex: 0
    property var recordFormValues: ({})
    property bool recordDirty: false
    signal recordFormReset()

    property bool deleteTableConfirmVisible: false
    property bool deleteRecordConfirmVisible: false
    property bool discardRecordConfirmVisible: false
    property bool quitConfirmVisible: false

    property bool imagePreviewVisible: false
    property string imagePreviewUrl: ""
    property string imagePreviewTitle: ""
    property string imagePreviewMeta: ""
    property real imagePreviewZoom: 1.0

    readonly property bool overlayOpen: helpVisible || tableDialogVisible || fieldsDialogVisible
        || recordDialogVisible || deleteTableConfirmVisible || deleteRecordConfirmVisible
        || discardRecordConfirmVisible || quitConfirmVisible || imagePreviewVisible
    readonly property bool typingInSearch: searchInput.activeFocus

    readonly property bool tableHasImageField: {
        var flist = backend.fields
        for (var i = 0; i < flist.length; ++i) {
            if (flist[i].datatype === "Image")
                return true
        }
        return false
    }

    readonly property var datatypesModel: [
        { code: "Text", label: qsTr("Text") },
        { code: "Number", label: qsTr("Number") },
        { code: "Euros", label: qsTr("Euros") },
        { code: "Date", label: qsTr("Date") },
        { code: "YesNo", label: qsTr("Yes / No") },
        { code: "List", label: qsTr("List") },
        { code: "Status", label: qsTr("Status") },
        { code: "Relation", label: qsTr("Relation") },
        { code: "Formula", label: qsTr("Formula") },
        { code: "Tags", label: qsTr("Tags") },
        { code: "Stars", label: qsTr("Stars") },
        { code: "URL", label: qsTr("URL") },
        { code: "Email", label: qsTr("Email") },
        { code: "Image", label: qsTr("Image") },
        { code: "File", label: qsTr("File") }
    ]

    readonly property string relationTargetTable: {
        var raw = fieldFormItems.trim()
        var idx = raw.indexOf(":")
        return (idx >= 0 ? raw.slice(0, idx) : raw).trim()
    }

    readonly property string relationTargetField: {
        var raw = fieldFormItems.trim()
        var idx = raw.indexOf(":")
        return idx >= 0 ? raw.slice(idx + 1).trim() : ""
    }

    readonly property var relationTargetFieldNames: {
        var target = relationTargetTable.toLowerCase()
        if (target === "")
            return []
        var tlist = backend.tables
        for (var i = 0; i < tlist.length; ++i) {
            if (String(tlist[i].name).toLowerCase() === target)
                return tlist[i].fieldNames || []
        }
        return []
    }

    function setRelationTarget(tableName, fieldName) {
        var t = String(tableName || "").trim()
        var f = String(fieldName || "").trim()
        if (t === "") {
            fieldFormItems = ""
            return
        }
        if (f === "") {
            var tlist = backend.tables
            for (var i = 0; i < tlist.length; ++i) {
                if (String(tlist[i].name).toLowerCase() === t.toLowerCase()) {
                    var fnames = tlist[i].fieldNames || []
                    for (var j = 0; j < fnames.length; ++j) {
                        var low = String(fnames[j]).toLowerCase()
                        if (low === "name" || low === "nom" || low === "title") {
                            f = fnames[j]
                            break
                        }
                    }
                    if (f === "" && fnames.length > 0)
                        f = fnames[0]
                    break
                }
            }
        }
        fieldFormItems = f !== "" ? (t + ":" + f) : t
    }

    function datatypeLabel(code) {
        var _ = translator.language
        for (var i = 0; i < datatypesModel.length; ++i) {
            if (datatypesModel[i].code === code)
                return datatypesModel[i].label
        }
        return code
    }

    function formatNumber(val, decimals) {
        var _ = translator.language
        return Number(val).toLocaleString(Qt.locale(), 'f', decimals)
    }

    function starsText(val) {
        var n = Math.max(0, Math.min(5, Math.round(Number(val) || 0)))
        var out = ""
        for (var i = 1; i <= 5; ++i)
            out += (i <= n) ? "★" : "☆"
        return out
    }

    function fileBadgeColor(ext) {
        var e = String(ext || "").toUpperCase()
        if (e === "PDF")
            return "#b91c1c"
        if (e === "DOC" || e === "DOCX" || e === "ODT" || e === "RTF")
            return "#2563eb"
        if (e === "XLS" || e === "XLSX" || e === "ODS" || e === "CSV")
            return "#15803d"
        if (e === "ZIP" || e === "TAR" || e === "GZ" || e === "7Z" || e === "RAR")
            return "#b45309"
        return "#3f3f4a"
    }

    function formatCellDisplay(datatype, rawValue, isNumeric, numericValue) {
        var _ = translator.language
        if (rawValue === "")
            return "—"
        if (datatype === "YesNo")
            return rawValue === "true" ? qsTr("Yes") : qsTr("No")
        if (datatype === "Euros" && isNumeric)
            return formatNumber(numericValue, 2) + " €"
        if ((datatype === "Number" || datatype === "Formula") && isNumeric) {
            var dec = rawValue.indexOf(".") >= 0 ? 2 : 0
            return formatNumber(numericValue, dec)
        }
        if (datatype === "Stars")
            return starsText(numericValue)
        return rawValue
    }

    function formatFooterDisplay(fieldObj) {
        var _ = translator.language
        if (!fieldObj.hasFooter)
            return ""
        var opLabel = fieldObj.operation === "Average" ? qsTr("Avg") : qsTr("Sum")
        var formatted = formatNumber(fieldObj.footerValue, fieldObj.datatype === "Euros" || fieldObj.operation === "Average" ? 2 : 0)
        if (fieldObj.datatype === "Euros")
            formatted += " €"
        return opLabel + ": " + formatted
    }

    function openNewTableDialog() {
        backend.clearMessages()
        editingTableId = 0
        tableDialogName = ""
        tableDialogLifo = true
        tableDialogVisible = true
        tableNameInput.forceActiveFocus()
    }

    function openEditTableDialog() {
        if (backend.currentTableId <= 0)
            return
        backend.clearMessages()
        editingTableId = backend.currentTableId
        tableDialogName = backend.currentTableName
        tableDialogLifo = backend.currentTableLifo
        tableDialogVisible = true
        tableNameInput.forceActiveFocus()
    }

    function submitTableDialog() {
        if (editingTableId > 0) {
            if (backend.updateTable(editingTableId, tableDialogName, tableDialogLifo))
                tableDialogVisible = false
        } else {
            var newId = backend.createTable(tableDialogName, tableDialogLifo)
            if (newId > 0) {
                tableDialogVisible = false
                openFieldsDialog()
            }
        }
    }

    function resetFieldForm() {
        editingFieldId = 0
        fieldFormName = ""
        fieldFormDatatype = "Text"
        fieldFormItems = ""
        fieldFormObligatoire = false
        fieldFormFiltre = false
        fieldFormOperation = ""
        fieldFormDescription = ""
    }

    function openFieldsDialog() {
        if (backend.currentTableId <= 0)
            return
        backend.clearMessages()
        resetFieldForm()
        fieldsDialogVisible = true
        fieldNameInput.forceActiveFocus()
    }

    function startEditField(fieldObj) {
        editingFieldId = fieldObj.id
        fieldFormName = fieldObj.name
        fieldFormDatatype = fieldObj.datatype
        fieldFormItems = fieldObj.items
        fieldFormObligatoire = fieldObj.obligatoire
        fieldFormFiltre = fieldObj.filtre
        fieldFormOperation = fieldObj.operation
        fieldFormDescription = fieldObj.description
        fieldNameInput.forceActiveFocus()
    }

    function submitFieldForm() {
        var ok = false
        if (editingFieldId > 0) {
            ok = backend.updateField(editingFieldId, fieldFormName, fieldFormDatatype,
                                     fieldFormItems, fieldFormObligatoire, fieldFormFiltre,
                                     fieldFormOperation, fieldFormDescription)
        } else {
            ok = backend.addField(fieldFormName, fieldFormDatatype,
                                  fieldFormItems, fieldFormObligatoire, fieldFormFiltre,
                                  fieldFormOperation, fieldFormDescription) > 0
        }
        if (ok) {
            resetFieldForm()
            fieldNameInput.forceActiveFocus()
        }
    }

    function openRecordDialog(recIdx) {
        if (backend.currentTableId <= 0)
            return
        if (backend.fieldCount === 0) {
            openFieldsDialog()
            return
        }
        backend.clearMessages()
        editingRecordIndex = recIdx
        var initial = {}
        var flist = backend.fields
        if (recIdx > 0) {
            var existing = backend.recordValues(recIdx)
            for (var j = 0; j < flist.length; ++j) {
                var k = String(flist[j].id)
                initial[k] = existing[k] !== undefined ? String(existing[k]) : ""
            }
        } else {
            for (var i = 0; i < flist.length; ++i) {
                var f = flist[i]
                if (f.datatype === "YesNo")
                    initial[String(f.id)] = "false"
                else if (f.datatype === "Date")
                    initial[String(f.id)] = Qt.formatDate(new Date(), "yyyy-MM-dd")
                else if (f.datatype === "Stars")
                    initial[String(f.id)] = "3"
                else if ((f.datatype === "List" || f.datatype === "Status" || (f.datatype === "Relation" && f.obligatoire)) && f.choices && f.choices.length > 0)
                    initial[String(f.id)] = f.choices[0]
                else
                    initial[String(f.id)] = ""
            }
        }
        recordFormValues = initial
        recordDirty = false
        recordDialogVisible = true
        recordFormReset()
        Qt.callLater(function() {
            for (var idx = 0; idx < recordFieldsRepeater.count; ++idx) {
                var item = recordFieldsRepeater.itemAt(idx)
                if (item && item.focusInput && item.focusInput())
                    break
            }
        })
    }

    function setRecordFieldValue(fieldId, val) {
        var copy = Object.assign({}, recordFormValues)
        var key = String(fieldId)
        if (copy[key] !== val) {
            copy[key] = val
            recordFormValues = copy
            recordDirty = true
        }
    }

    function submitRecordDialog(refill) {
        var savedIdx = backend.saveRecord(editingRecordIndex, recordFormValues)
        if (savedIdx > 0) {
            recordDirty = false
            if (refill)
                openRecordDialog(0)
            else
                recordDialogVisible = false
        }
    }

    function closeRecordDialog() {
        if (recordDirty) {
            discardRecordConfirmVisible = true
            return
        }
        recordDialogVisible = false
    }

    function openImagePreview(url, filename, width, height, fieldName) {
        if (!url || url === "")
            return
        imagePreviewUrl = url
        var title = filename && filename !== "" ? filename : (fieldName || qsTr("Image"))
        if (fieldName && fieldName !== "" && filename && filename !== "")
            title = fieldName + " — " + filename
        imagePreviewTitle = title
        imagePreviewMeta = (width > 0 && height > 0) ? (width + " × " + height + " px") : ""
        imagePreviewZoom = 1.0
        imagePreviewVisible = true
    }

    function closeImagePreview() {
        imagePreviewVisible = false
        imagePreviewZoom = 1.0
    }

    function adjustImagePreviewZoom(delta) {
        imagePreviewZoom = Math.max(1.0, Math.min(4.0, Math.round((imagePreviewZoom + delta) * 4) / 4))
    }

    Connections {
        target: backend
        function onImagePicked(fieldId, blobRef) {
            if (win.recordDialogVisible && blobRef !== "")
                win.setRecordFieldValue(fieldId, blobRef)
        }
        function onFilePicked(fieldId, blobRef) {
            if (win.recordDialogVisible && blobRef !== "")
                win.setRecordFieldValue(fieldId, blobRef)
        }
    }

    function handleSpaceAction() {
        if (backend.tableCount === 0)
            openNewTableDialog()
        else if (backend.fieldCount === 0)
            openFieldsDialog()
        else
            openRecordDialog(0)
    }

    function requestQuit() {
        if (recordDialogVisible && recordDirty) {
            quitConfirmVisible = true
            return
        }
        Qt.quit()
    }

    // Keyboard shortcuts
    Shortcut {
        sequence: "?"
        context: Qt.ApplicationShortcut
        enabled: !win.typingInSearch && !win.tableDialogVisible && !win.fieldsDialogVisible && !win.recordDialogVisible && !win.imagePreviewVisible
        onActivated: win.helpVisible = !win.helpVisible
    }
    Shortcut {
        sequence: "Escape"
        context: Qt.ApplicationShortcut
        onActivated: {
            if (win.imagePreviewVisible) win.closeImagePreview()
            else if (win.helpVisible) win.helpVisible = false
            else if (win.quitConfirmVisible) win.quitConfirmVisible = false
            else if (win.discardRecordConfirmVisible) win.discardRecordConfirmVisible = false
            else if (win.deleteRecordConfirmVisible) win.deleteRecordConfirmVisible = false
            else if (win.deleteTableConfirmVisible) win.deleteTableConfirmVisible = false
            else if (win.recordDialogVisible) win.closeRecordDialog()
            else if (win.fieldsDialogVisible) win.fieldsDialogVisible = false
            else if (win.tableDialogVisible) win.tableDialogVisible = false
            else if (win.typingInSearch) {
                searchInput.focus = false
                backend.searchText = ""
            }
        }
    }
    Shortcut {
        sequences: ["+", "="]
        context: Qt.ApplicationShortcut
        enabled: win.imagePreviewVisible
        onActivated: win.adjustImagePreviewZoom(0.5)
    }
    Shortcut {
        sequence: "-"
        context: Qt.ApplicationShortcut
        enabled: win.imagePreviewVisible
        onActivated: win.adjustImagePreviewZoom(-0.5)
    }
    Shortcut {
        sequence: "0"
        context: Qt.ApplicationShortcut
        enabled: win.imagePreviewVisible
        onActivated: win.imagePreviewZoom = 1.0
    }
    Shortcut {
        sequence: "Q"
        context: Qt.ApplicationShortcut
        enabled: !win.overlayOpen && !win.typingInSearch
        onActivated: win.requestQuit()
    }
    Shortcut {
        sequence: "Space"
        context: Qt.ApplicationShortcut
        enabled: !win.overlayOpen && !win.typingInSearch
        onActivated: win.handleSpaceAction()
    }
    Shortcut {
        sequence: "N"
        context: Qt.ApplicationShortcut
        enabled: !win.overlayOpen && !win.typingInSearch
        onActivated: win.openNewTableDialog()
    }
    Shortcut {
        sequence: "R"
        context: Qt.ApplicationShortcut
        enabled: !win.overlayOpen && !win.typingInSearch && backend.currentTableId > 0
        onActivated: win.openEditTableDialog()
    }
    Shortcut {
        sequence: "F"
        context: Qt.ApplicationShortcut
        enabled: !win.overlayOpen && !win.typingInSearch && backend.currentTableId > 0
        onActivated: win.openFieldsDialog()
    }
    Shortcut {
        sequence: "/"
        context: Qt.ApplicationShortcut
        enabled: !win.overlayOpen && !win.typingInSearch && backend.fieldCount > 0
        onActivated: searchInput.forceActiveFocus()
    }
    Shortcut {
        sequence: "I"
        context: Qt.ApplicationShortcut
        enabled: !win.overlayOpen && !win.typingInSearch
        onActivated: backend.importCsvDialog(0)
    }
    Shortcut {
        sequence: "E"
        context: Qt.ApplicationShortcut
        enabled: !win.overlayOpen && !win.typingInSearch && backend.currentTableId > 0 && backend.fieldCount > 0
        onActivated: backend.exportCsvDialog()
    }
    Shortcut {
        sequence: "O"
        context: Qt.ApplicationShortcut
        enabled: !win.overlayOpen && !win.typingInSearch
        onActivated: backend.openDatabaseDialog()
    }
    Shortcut {
        sequences: ["Return", "Enter"]
        context: Qt.ApplicationShortcut
        enabled: !win.overlayOpen && !win.typingInSearch && backend.selectedRecordIndex > 0
        onActivated: win.openRecordDialog(backend.selectedRecordIndex)
    }
    Shortcut {
        sequences: ["Delete", "X"]
        context: Qt.ApplicationShortcut
        enabled: !win.overlayOpen && !win.typingInSearch && backend.selectedRecordIndex > 0
        onActivated: win.deleteRecordConfirmVisible = true
    }
    Shortcut {
        sequence: "Up"
        context: Qt.ApplicationShortcut
        enabled: !win.overlayOpen && !win.typingInSearch && backend.recordCount > 0
        onActivated: backend.selectedRowIndex = Math.max(0, backend.selectedRowIndex - 1)
    }
    Shortcut {
        sequence: "Down"
        context: Qt.ApplicationShortcut
        enabled: !win.overlayOpen && !win.typingInSearch && backend.recordCount > 0
        onActivated: backend.selectedRowIndex = Math.min(backend.recordCount - 1, backend.selectedRowIndex + 1)
    }
    Shortcut {
        sequences: [StandardKey.Undo]
        context: Qt.ApplicationShortcut
        enabled: !win.overlayOpen && backend.canUndo
        onActivated: backend.undo()
    }
    Shortcut {
        sequences: [StandardKey.Redo, "Ctrl+Shift+Z"]
        context: Qt.ApplicationShortcut
        enabled: !win.overlayOpen && backend.canRedo
        onActivated: backend.redo()
    }

    // 1..9 table switch shortcuts
    Repeater {
        model: 9
        Item {
            required property int index
            Shortcut {
                sequence: String(parent.index + 1)
                context: Qt.ApplicationShortcut
                enabled: !win.overlayOpen && !win.typingInSearch && parent.index < backend.tableCount
                onActivated: backend.selectTableByIndex(parent.index)
            }
        }
    }

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Item {
        id: rootContent
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        HoverHandler {
            id: contentHover
        }

        RowLayout {
            anchors.fill: parent
            spacing: 0

            // --- Left Sidebar: Tables list ---
            Rectangle {
                Layout.preferredWidth: 250
                Layout.fillHeight: true
                color: "#141417"

                Rectangle {
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: 1
                    color: "#26262b"
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 12

                    // App title and current SQLite file
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Rectangle {
                            width: 10
                            height: 10
                            radius: 5
                            color: theme.accent
                        }
                        Label {
                            text: "AIKKU"
                            color: "white"
                            font.pixelSize: 15
                            font.weight: Font.Bold
                            font.letterSpacing: 1.2
                        }
                        Item { Layout.fillWidth: true }
                        Rectangle {
                            height: 24
                            width: dbLabel.implicitWidth + 14
                            radius: Math.min(4, theme.radius)
                            color: dbHover.hovered ? "#2c2c32" : "#1f1f24"
                            border.width: 1
                            border.color: "#323238"

                            Label {
                                id: dbLabel
                                anchors.centerIn: parent
                                text: backend.databaseName
                                color: "#b8b8c0"
                                font.pixelSize: 11
                            }
                            HoverHandler { id: dbHover; cursorShape: Qt.PointingHandCursor }
                            TapHandler { onTapped: backend.openDatabaseDialog() }
                        }
                    }

                    // Section header: Tables + New button
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        Label {
                            text: qsTr("Tables")
                            color: "#9a9aa2"
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                        }
                        Label {
                            text: "· " + qsTr("%n tables", "", backend.tableCount)
                            color: "#6e6e77"
                            font.pixelSize: 12
                        }
                        Item { Layout.fillWidth: true }
                        Rectangle {
                            id: newTableSidebarBtn
                            objectName: "newTableSidebarBtn"
                            height: 26
                            width: newTableSidebarLbl.implicitWidth + 16
                            radius: Math.min(4, theme.radius)
                            color: newTableSidebarHover.hovered ? Qt.darker(theme.accent, 1.08) : theme.accent

                            Label {
                                id: newTableSidebarLbl
                                anchors.centerIn: parent
                                text: "+ " + qsTr("New")
                                color: theme.accentForeground
                                font.pixelSize: 12
                                font.weight: Font.DemiBold
                            }
                            HoverHandler { id: newTableSidebarHover; cursorShape: Qt.PointingHandCursor }
                            TapHandler { onTapped: win.openNewTableDialog() }
                        }
                    }

                    // Tables ListView
                    ListView {
                        id: tablesListView
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 4
                        model: backend.tables

                        delegate: Item {
                            required property var modelData
                            required property int index
                            width: tablesListView.width
                            height: 54

                            readonly property bool isCurrent: modelData.id === backend.currentTableId

                            Rectangle {
                                anchors.fill: parent
                                radius: Math.min(4, theme.radius)
                                color: parent.isCurrent ? "#232329" : (tableItemHover.hovered ? "#1b1b20" : "transparent")
                                border.width: parent.isCurrent ? 1 : 0
                                border.color: parent.isCurrent ? theme.accent : "transparent"

                                Rectangle {
                                    visible: parent.parent.isCurrent
                                    anchors.left: parent.left
                                    anchors.top: parent.top
                                    anchors.bottom: parent.bottom
                                    width: 3
                                    radius: Math.min(2, theme.radius)
                                    color: theme.accent
                                }

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 10
                                    anchors.rightMargin: 10
                                    spacing: 8

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 6

                                            Label {
                                                visible: index < 9
                                                text: String(index + 1)
                                                color: "#6e6e77"
                                                font.pixelSize: 11
                                                font.family: "monospace"
                                            }
                                            Label {
                                                Layout.fillWidth: true
                                                text: modelData.name
                                                color: parent.parent.parent.parent.parent.isCurrent ? "white" : "#dcdce2"
                                                font.pixelSize: 13
                                                font.weight: parent.parent.parent.parent.parent.isCurrent ? Font.DemiBold : Font.Normal
                                                elide: Text.ElideRight
                                            }
                                        }

                                        Label {
                                            Layout.fillWidth: true
                                            text: modelData.fieldSummary !== "" ? modelData.fieldSummary : qsTr("No attributes")
                                            color: "#7c7c86"
                                            font.pixelSize: 11
                                            elide: Text.ElideRight
                                        }
                                    }

                                    Rectangle {
                                        height: 20
                                        width: Math.max(24, recCountBadge.implicitWidth + 10)
                                        radius: Math.min(4, theme.radius)
                                        color: "#18181c"
                                        border.width: 1
                                        border.color: "#2e2e36"

                                        Label {
                                            id: recCountBadge
                                            anchors.centerIn: parent
                                            text: {
                                                var _ = translator.language
                                                return Number(modelData.recordCount).toLocaleString(Qt.locale(), 'f', 0)
                                            }
                                            color: "#b4b4be"
                                            font.pixelSize: 11
                                        }
                                    }
                                }

                                HoverHandler { id: tableItemHover; cursorShape: Qt.PointingHandCursor }
                                TapHandler { onTapped: backend.selectTable(modelData.id) }
                            }
                        }
                    }

                    // Bottom Sidebar Actions: Import CSV & Sample Table
                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: "#242429"
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Rectangle {
                            Layout.fillWidth: true
                            height: 32
                            radius: Math.min(4, theme.radius)
                            color: importBtnHover.hovered ? "#292930" : "#1e1e24"
                            border.width: 1
                            border.color: "#32323a"

                            Label {
                                anchors.centerIn: parent
                                text: qsTr("Import CSV")
                                color: "#e0e0e6"
                                font.pixelSize: 12
                                elide: Text.ElideRight
                            }
                            HoverHandler { id: importBtnHover; cursorShape: Qt.PointingHandCursor }
                            TapHandler { onTapped: backend.importCsvDialog(0) }
                        }

                        Rectangle {
                            id: sampleBtn
                            objectName: "sampleBtn"
                            Layout.fillWidth: true
                            height: 32
                            radius: Math.min(4, theme.radius)
                            color: sampleBtnHover.hovered ? "#292930" : "#1e1e24"
                            border.width: 1
                            border.color: "#32323a"

                            Label {
                                anchors.centerIn: parent
                                text: qsTr("Sample table")
                                color: "#e0e0e6"
                                font.pixelSize: 12
                                elide: Text.ElideRight
                            }
                            HoverHandler { id: sampleBtnHover; cursorShape: Qt.PointingHandCursor }
                            TapHandler { onTapped: backend.createSampleTable() }
                        }
                    }
                }
            }

            // --- Right Main Panel ---
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                // Error / Notice Banner
                Rectangle {
                    visible: backend.errorMessage !== "" || backend.noticeMessage !== ""
                    Layout.fillWidth: true
                    height: 38
                    color: backend.errorMessage !== "" ? "#3b1216" : "#1c1c22"

                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 1
                        color: backend.errorMessage !== "" ? "#dc2626" : theme.accent
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 16
                        anchors.rightMargin: 16
                        spacing: 10

                        Label {
                            Layout.fillWidth: true
                            text: backend.errorMessage !== "" ? backend.errorMessage : backend.noticeMessage
                            color: backend.errorMessage !== "" ? "#fca5a5" : theme.accent
                            font.pixelSize: 13
                            elide: Text.ElideRight
                        }

                        Rectangle {
                            width: 24
                            height: 24
                            radius: Math.min(4, theme.radius)
                            color: dismissHover.hovered ? "#2c2c34" : "transparent"

                            Label {
                                anchors.centerIn: parent
                                text: "×"
                                color: "#b8b8c2"
                                font.pixelSize: 15
                            }
                            HoverHandler { id: dismissHover; cursorShape: Qt.PointingHandCursor }
                            TapHandler { onTapped: backend.clearMessages() }
                        }
                    }
                }

                // Empty State 1: No Tables
                Item {
                    visible: backend.tableCount === 0
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    ColumnLayout {
                        anchors.centerIn: parent
                        width: Math.min(460, parent.width - 64)
                        spacing: 14

                        Label {
                            Layout.fillWidth: true
                            text: qsTr("No tables yet")
                            color: "white"
                            font.pixelSize: 22
                            font.weight: Font.DemiBold
                            horizontalAlignment: Text.AlignHCenter
                        }

                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Create a custom table with typed attributes, import a CSV collection, or load a sample table.")
                            color: "#9a9aa4"
                            font.pixelSize: 14
                            wrapMode: Text.WordWrap
                            horizontalAlignment: Text.AlignHCenter
                        }

                        RowLayout {
                            Layout.alignment: Qt.AlignHCenter
                            spacing: 12

                            DialogButton {
                                text: qsTr("New table")
                                primary: true
                                onClicked: win.openNewTableDialog()
                            }
                            DialogButton {
                                text: qsTr("Import CSV")
                                onClicked: backend.importCsvDialog(0)
                            }
                            DialogButton {
                                text: qsTr("Load sample")
                                onClicked: backend.createSampleTable()
                            }
                        }
                    }
                }

                // Active Table Workspace
                ColumnLayout {
                    visible: backend.currentTableId > 0
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 0

                    // Top Table Toolbar
                    Rectangle {
                        Layout.fillWidth: true
                        height: 62
                        color: "#121215"

                        Rectangle {
                            anchors.bottom: parent.bottom
                            width: parent.width
                            height: 1
                            color: "#24242a"
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 20
                            anchors.rightMargin: 20
                            spacing: 10

                            Label {
                                text: backend.currentTableName
                                color: "white"
                                font.pixelSize: 20
                                font.weight: Font.Bold
                                elide: Text.ElideRight
                                Layout.maximumWidth: 280
                            }

                            Rectangle {
                                height: 24
                                width: topCountLabel.implicitWidth + 16
                                radius: Math.min(4, theme.radius)
                                color: "#1e1e24"
                                border.width: 1
                                border.color: "#2e2e36"

                                Label {
                                    id: topCountLabel
                                    anchors.centerIn: parent
                                    text: qsTr("%n records", "", backend.recordCount)
                                    color: "#c4c4ce"
                                    font.pixelSize: 12
                                }
                            }

                            Item { Layout.fillWidth: true }

                            // Add Record Button (Primary)
                            Rectangle {
                                id: addRecordBtn
                                objectName: "addRecordBtn"
                                visible: backend.fieldCount > 0
                                height: 34
                                width: addRecordLbl.implicitWidth + 24
                                radius: Math.min(4, theme.radius)
                                color: addRecHover.hovered ? Qt.darker(theme.accent, 1.08) : theme.accent

                                Label {
                                    id: addRecordLbl
                                    anchors.centerIn: parent
                                    text: "+ " + qsTr("Add record")
                                    color: theme.accentForeground
                                    font.pixelSize: 13
                                    font.weight: Font.DemiBold
                                }
                                HoverHandler { id: addRecHover; cursorShape: Qt.PointingHandCursor }
                                TapHandler { onTapped: win.openRecordDialog(0) }
                            }

                            // Attributes (Fields) Button
                            Rectangle {
                                id: attrsBtn
                                objectName: "attrsBtn"
                                height: 34
                                width: attrsBtnLbl.implicitWidth + 22
                                radius: Math.min(4, theme.radius)
                                color: attrsBtnHover.hovered ? "#2a2a32" : "#1e1e24"
                                border.width: 1
                                border.color: "#34343d"

                                Label {
                                    id: attrsBtnLbl
                                    anchors.centerIn: parent
                                    text: qsTr("Attributes") + " (" + qsTr("%n fields", "", backend.fieldCount) + ")"
                                    color: "#e4e4ec"
                                    font.pixelSize: 12
                                    font.weight: Font.DemiBold
                                }
                                HoverHandler { id: attrsBtnHover; cursorShape: Qt.PointingHandCursor }
                                TapHandler { onTapped: win.openFieldsDialog() }
                            }

                            // Edit Table Button
                            Rectangle {
                                height: 34
                                width: editTableLbl.implicitWidth + 20
                                radius: Math.min(4, theme.radius)
                                color: editTableHover.hovered ? "#2a2a32" : "#1e1e24"
                                border.width: 1
                                border.color: "#34343d"

                                Label {
                                    id: editTableLbl
                                    anchors.centerIn: parent
                                    text: qsTr("Edit table")
                                    color: "#d4d4dc"
                                    font.pixelSize: 12
                                }
                                HoverHandler { id: editTableHover; cursorShape: Qt.PointingHandCursor }
                                TapHandler { onTapped: win.openEditTableDialog() }
                            }

                            // Export CSV Button
                            Rectangle {
                                visible: backend.fieldCount > 0
                                height: 34
                                width: exportCsvLbl.implicitWidth + 20
                                radius: Math.min(4, theme.radius)
                                color: exportCsvHover.hovered ? "#2a2a32" : "#1e1e24"
                                border.width: 1
                                border.color: "#34343d"

                                Label {
                                    id: exportCsvLbl
                                    anchors.centerIn: parent
                                    text: qsTr("Export CSV")
                                    color: "#d4d4dc"
                                    font.pixelSize: 12
                                }
                                HoverHandler { id: exportCsvHover; cursorShape: Qt.PointingHandCursor }
                                TapHandler { onTapped: backend.exportCsvDialog() }
                            }

                            // Delete Table Button
                            Rectangle {
                                height: 34
                                width: delTableLbl.implicitWidth + 20
                                radius: Math.min(4, theme.radius)
                                color: delTableHover.hovered ? "#3f181c" : "#1e1e24"
                                border.width: 1
                                border.color: delTableHover.hovered ? "#dc2626" : "#34343d"

                                Label {
                                    id: delTableLbl
                                    anchors.centerIn: parent
                                    text: qsTr("Delete")
                                    color: delTableHover.hovered ? "#fca5a5" : "#b8b8c2"
                                    font.pixelSize: 12
                                }
                                HoverHandler { id: delTableHover; cursorShape: Qt.PointingHandCursor }
                                TapHandler { onTapped: win.deleteTableConfirmVisible = true }
                            }
                        }
                    }

                    // Search and Filter Bar
                    Rectangle {
                        visible: backend.fieldCount > 0
                        Layout.fillWidth: true
                        height: 52
                        color: "#111114"

                        Rectangle {
                            anchors.bottom: parent.bottom
                            width: parent.width
                            height: 1
                            color: "#222228"
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 20
                            anchors.rightMargin: 20
                            spacing: 12

                            // Search Box
                            Rectangle {
                                Layout.preferredWidth: 230
                                height: 32
                                radius: Math.min(4, theme.radius)
                                color: "#1a1a1f"
                                border.width: searchInput.activeFocus ? 2 : 1
                                border.color: searchInput.activeFocus ? theme.accent : "#2e2e36"

                                TextInput {
                                    id: searchInput
                                    objectName: "searchInput"
                                    anchors.fill: parent
                                    anchors.leftMargin: 10
                                    anchors.rightMargin: 10
                                    verticalAlignment: TextInput.AlignVCenter
                                    color: "white"
                                    font.pixelSize: 13
                                    selectByMouse: true
                                    clip: true
                                    text: backend.searchText
                                    onTextEdited: backend.searchText = text
                                    Keys.onReturnPressed: focus = false
                                    Keys.onEnterPressed: focus = false

                                    Label {
                                        anchors.fill: parent
                                        verticalAlignment: Text.AlignVCenter
                                        visible: searchInput.text === "" && !searchInput.activeFocus
                                        text: qsTr("Search records (/)")
                                        color: "#6e6e78"
                                        font.pixelSize: 12
                                    }
                                }
                            }

                            // Quick Filters for fields marked with filtre = true
                            Repeater {
                                model: backend.filterableFields

                                RowLayout {
                                    required property var modelData
                                    spacing: 6

                                    Label {
                                        text: modelData.name + ":"
                                        color: "#9a9aa4"
                                        font.pixelSize: 12
                                    }

                                    // Date range filter
                                    RowLayout {
                                        visible: modelData.datatype === "Date"
                                        spacing: 4

                                        Rectangle {
                                            width: 104
                                            height: 30
                                            radius: Math.min(4, theme.radius)
                                            color: "#1a1a1f"
                                            border.width: 1
                                            border.color: "#2e2e36"

                                            TextInput {
                                                id: dateStartField
                                                anchors.fill: parent
                                                anchors.leftMargin: 6
                                                anchors.rightMargin: 6
                                                verticalAlignment: TextInput.AlignVCenter
                                                color: "white"
                                                font.pixelSize: 12
                                                selectByMouse: true
                                                text: backend.dateFilterStart(modelData.id)
                                                onTextEdited: backend.setDateFilter(modelData.id, text, dateEndField.text)

                                                Label {
                                                    anchors.fill: parent
                                                    verticalAlignment: Text.AlignVCenter
                                                    visible: dateStartField.text === "" && !dateStartField.activeFocus
                                                    text: qsTr("From")
                                                    color: "#64646e"
                                                    font.pixelSize: 11
                                                }
                                            }
                                        }

                                        Rectangle {
                                            width: 104
                                            height: 30
                                            radius: Math.min(4, theme.radius)
                                            color: "#1a1a1f"
                                            border.width: 1
                                            border.color: "#2e2e36"

                                            TextInput {
                                                id: dateEndField
                                                anchors.fill: parent
                                                anchors.leftMargin: 6
                                                anchors.rightMargin: 6
                                                verticalAlignment: TextInput.AlignVCenter
                                                color: "white"
                                                font.pixelSize: 12
                                                selectByMouse: true
                                                text: backend.dateFilterEnd(modelData.id)
                                                onTextEdited: backend.setDateFilter(modelData.id, dateStartField.text, text)

                                                Label {
                                                    anchors.fill: parent
                                                    verticalAlignment: Text.AlignVCenter
                                                    visible: dateEndField.text === "" && !dateEndField.activeFocus
                                                    text: qsTr("To")
                                                    color: "#64646e"
                                                    font.pixelSize: 11
                                                }
                                            }
                                        }
                                    }

                                    // Value selector pill cycles through All + filterChoices
                                    Rectangle {
                                        visible: modelData.datatype !== "Date"
                                        height: 30
                                        width: Math.max(84, filterPillLbl.implicitWidth + 20)
                                        radius: Math.min(4, theme.radius)
                                        readonly property string currentVal: {
                                            var _ = backend.hasActiveFilters
                                            return backend.fieldFilter(modelData.id)
                                        }
                                        color: currentVal !== "" ? "#262632" : "#1a1a1f"
                                        border.width: 1
                                        border.color: currentVal !== "" ? theme.accent : "#2e2e36"

                                        Label {
                                            id: filterPillLbl
                                            anchors.centerIn: parent
                                            text: parent.currentVal === "" ? qsTr("All") : parent.currentVal
                                            color: parent.currentVal === "" ? "#b0b0ba" : "white"
                                            font.pixelSize: 12
                                        }
                                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                                        TapHandler {
                                            onTapped: {
                                                var opts = [""].concat(modelData.filterChoices || [])
                                                var cur = backend.fieldFilter(modelData.id)
                                                var idx = opts.indexOf(cur)
                                                var next = opts[(idx + 1) % opts.length]
                                                backend.setFieldFilter(modelData.id, next)
                                            }
                                        }
                                    }
                                }
                            }

                            Item { Layout.fillWidth: true }

                            Rectangle {
                                visible: backend.hasActiveFilters
                                height: 28
                                width: clearFilterLbl.implicitWidth + 18
                                radius: Math.min(4, theme.radius)
                                color: clearFilterHover.hovered ? "#2c2c34" : "#1e1e24"
                                border.width: 1
                                border.color: "#383842"

                                Label {
                                    id: clearFilterLbl
                                    anchors.centerIn: parent
                                    text: qsTr("Clear filters")
                                    color: theme.accent
                                    font.pixelSize: 12
                                }
                                HoverHandler { id: clearFilterHover; cursorShape: Qt.PointingHandCursor }
                                TapHandler {
                                    onTapped: {
                                        searchInput.text = ""
                                        backend.clearFilters()
                                    }
                                }
                            }
                        }
                    }

                    // Empty State 2: Table has no fields yet
                    Item {
                        visible: backend.fieldCount === 0
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        ColumnLayout {
                            anchors.centerIn: parent
                            spacing: 12

                            Label {
                                Layout.alignment: Qt.AlignHCenter
                                text: qsTr("This table has no attributes yet.")
                                color: "#d4d4dc"
                                font.pixelSize: 16
                                font.weight: Font.DemiBold
                            }
                            DialogButton {
                                Layout.alignment: Qt.AlignHCenter
                                text: qsTr("Define attributes")
                                primary: true
                                onClicked: win.openFieldsDialog()
                            }
                        }
                    }

                    // Data Table Grid (Header + Rows + Footer Aggregation)
                    Item {
                        id: dataGridContainer
                        objectName: "dataGridContainer"
                        visible: backend.fieldCount > 0
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true

                        readonly property int fixedColsWidth: 44 + 96 + 96
                        readonly property int sumMinFieldWidths: {
                            var flist = backend.fields
                            var sum = 0
                            for (var i = 0; i < flist.length; ++i)
                                sum += Math.max(96, Number(flist[i].contentWidth) || 120)
                            return sum
                        }
                        readonly property int extraWidthPerCol: {
                            var count = backend.fieldCount
                            if (count <= 0)
                                return 0
                            var rem = width - fixedColsWidth - sumMinFieldWidths
                            return rem > 0 ? Math.floor(rem / count) : 0
                        }
                        readonly property int extraWidthRemainder: {
                            var count = backend.fieldCount
                            if (count <= 0)
                                return 0
                            var rem = width - fixedColsWidth - sumMinFieldWidths
                            return rem > 0 ? (rem - extraWidthPerCol * count) : 0
                        }
                        readonly property int totalGridWidth: {
                            var count = backend.fieldCount
                            return fixedColsWidth + sumMinFieldWidths + extraWidthPerCol * count + extraWidthRemainder
                        }

                        function columnWidthAt(idx) {
                            var flist = backend.fields
                            if (idx < 0 || idx >= flist.length)
                                return 120
                            var baseW = Math.max(96, Number(flist[idx].contentWidth) || 120)
                            return baseW + extraWidthPerCol + (idx + 1 === flist.length ? extraWidthRemainder : 0)
                        }

                        Flickable {
                            id: horizFlick
                            objectName: "horizFlick"
                            anchors.fill: parent
                            contentWidth: Math.max(parent.width, parent.totalGridWidth)
                            contentHeight: parent.height
                            flickableDirection: Flickable.HorizontalFlick
                            boundsBehavior: Flickable.StopAtBounds
                            clip: true

                            ScrollBar.horizontal: ScrollBar {
                                id: horizScrollBar
                                objectName: "horizScrollBar"
                                policy: horizFlick.contentWidth > horizFlick.width ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
                                height: 8
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                            }

                            WheelHandler {
                                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                                onWheel: (event) => {
                                    var dx = event.angleDelta.x
                                    if (dx === 0 && (event.modifiers & Qt.ShiftModifier))
                                        dx = event.angleDelta.y
                                    if (dx !== 0 && horizFlick.contentWidth > horizFlick.width) {
                                        var maxScroll = horizFlick.contentWidth - horizFlick.width
                                        horizFlick.contentX = Math.max(0, Math.min(maxScroll, horizFlick.contentX - dx))
                                        event.accepted = true
                                    } else {
                                        event.accepted = false
                                    }
                                }
                            }

                            ColumnLayout {
                                width: horizFlick.contentWidth
                                height: horizFlick.height
                                spacing: 0

                                // Column Header Row
                                Rectangle {
                                    Layout.fillWidth: true
                                    height: 40
                                    color: "#16161a"

                                    Rectangle {
                                        anchors.bottom: parent.bottom
                                        width: parent.width
                                        height: 1
                                        color: "#282830"
                                    }

                                    Row {
                                        anchors.fill: parent

                                        // # Column
                                        Item {
                                            width: 44
                                            height: parent.height
                                            Label {
                                                anchors.centerIn: parent
                                                text: "#"
                                                color: "#7a7a84"
                                                font.pixelSize: 12
                                                font.weight: Font.DemiBold
                                            }
                                        }

                                        // Dynamic Field Columns
                                        Repeater {
                                            model: backend.fields

                                            Rectangle {
                                                required property var modelData
                                                required property int index
                                                width: dataGridContainer.columnWidthAt(index)
                                                height: 40
                                                color: colHeaderHover.hovered ? "#1f1f26" : "transparent"

                                                RowLayout {
                                                    anchors.fill: parent
                                                    anchors.leftMargin: 10
                                                    anchors.rightMargin: 10
                                                    spacing: 4

                                                    Label {
                                                        Layout.fillWidth: true
                                                        text: modelData.name + (modelData.obligatoire ? " *" : "")
                                                        color: backend.sortFieldId === modelData.id ? theme.accent : "#dcdce4"
                                                        font.pixelSize: 12
                                                        font.weight: Font.DemiBold
                                                        horizontalAlignment: (modelData.datatype === "Number" || modelData.datatype === "Euros" || modelData.datatype === "Formula")
                                                            ? Text.AlignRight
                                                            : Text.AlignLeft
                                                        elide: Text.ElideRight
                                                    }

                                                    Label {
                                                        visible: backend.sortFieldId === modelData.id
                                                        text: backend.sortAscending ? "△" : "▽"
                                                        color: theme.accent
                                                        font.pixelSize: 10
                                                    }
                                                }

                                                HoverHandler { id: colHeaderHover; cursorShape: Qt.PointingHandCursor }
                                                TapHandler { onTapped: backend.toggleSort(modelData.id) }
                                            }
                                        }

                                        // Updated At Column
                                        Rectangle {
                                            width: 96
                                            height: 40
                                            color: updHeaderHover.hovered ? "#1f1f26" : "transparent"

                                            RowLayout {
                                                anchors.fill: parent
                                                anchors.leftMargin: 8
                                                anchors.rightMargin: 8
                                                spacing: 4

                                                Label {
                                                    Layout.fillWidth: true
                                                    text: qsTr("Updated")
                                                    color: backend.sortFieldId === 0 ? theme.accent : "#9a9aa4"
                                                    font.pixelSize: 12
                                                    font.weight: Font.DemiBold
                                                    elide: Text.ElideRight
                                                }
                                                Label {
                                                    visible: backend.sortFieldId === 0
                                                    text: backend.sortAscending ? "△" : "▽"
                                                    color: theme.accent
                                                    font.pixelSize: 10
                                                }
                                            }
                                            HoverHandler { id: updHeaderHover; cursorShape: Qt.PointingHandCursor }
                                            TapHandler { onTapped: backend.toggleSort(0) }
                                        }

                                        // Actions Column Header
                                        Item {
                                            width: 96
                                            height: 40
                                        }
                                    }
                                }

                                // Records ListView
                                ListView {
                                    id: recordsListView
                                    objectName: "recordsListView"
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    clip: true
                                    model: backend.records

                                    readonly property int rowHeight: win.tableHasImageField ? 52 : 42

                                    ScrollBar.vertical: ScrollBar {
                                        policy: ScrollBar.AsNeeded
                                    }

                                    Label {
                                        anchors.centerIn: parent
                                        visible: backend.recordCount === 0
                                        text: backend.hasActiveFilters
                                            ? qsTr("No records match the active filters.")
                                            : qsTr("No records yet. Press Space to add one.")
                                        color: "#7a7a84"
                                        font.pixelSize: 14
                                    }

                                    delegate: Item {
                                        id: rowDelegate
                                        required property var modelData
                                        required property int index
                                        width: recordsListView.width
                                        height: recordsListView.rowHeight

                                        readonly property int rowIdx: index
                                        readonly property bool isSelected: index === backend.selectedRowIndex

                                        Rectangle {
                                            anchors.fill: parent
                                            color: parent.isSelected
                                                ? "#1f2029"
                                                : (rowHover.hovered ? "#17171c" : (index % 2 === 1 ? "#111114" : "#0e0e10"))

                                            Rectangle {
                                                visible: parent.parent.isSelected
                                                anchors.left: parent.left
                                                anchors.top: parent.top
                                                anchors.bottom: parent.bottom
                                                width: 3
                                                color: theme.accent
                                            }

                                            Rectangle {
                                                anchors.bottom: parent.bottom
                                                width: parent.width
                                                height: 1
                                                color: "#1c1c22"
                                            }

                                            MouseArea {
                                                anchors.fill: parent
                                                onClicked: backend.selectedRowIndex = index
                                                onDoubleClicked: {
                                                    backend.selectedRowIndex = index
                                                    win.openRecordDialog(modelData.recordIndex)
                                                }
                                            }

                                            Row {
                                                anchors.fill: parent

                                                // # Index Cell
                                                Item {
                                                    width: 44
                                                    height: parent.height
                                                    Label {
                                                        anchors.centerIn: parent
                                                        text: String(modelData.recordIndex)
                                                        color: "#70707a"
                                                        font.pixelSize: 12
                                                        font.family: "monospace"
                                                    }
                                                }

                                                // Record Value Cells
                                                Repeater {
                                                    model: modelData.cells

                                                    Item {
                                                        required property var modelData
                                                        required property int index
                                                        width: dataGridContainer.columnWidthAt(index)
                                                        height: parent.height

                                                        // Status Pill Badge
                                                        Rectangle {
                                                            visible: modelData.datatype === "Status" && modelData.rawValue !== ""
                                                            anchors.verticalCenter: parent.verticalCenter
                                                            anchors.left: parent.left
                                                            anchors.leftMargin: 10
                                                            height: 22
                                                            width: Math.min(parent.width - 16, statusCellLbl.implicitWidth + 16)
                                                            radius: Math.min(4, theme.radius)
                                                            color: modelData.badgeColor !== "" ? modelData.badgeColor : "#4b5563"

                                                            Label {
                                                                id: statusCellLbl
                                                                anchors.centerIn: parent
                                                                width: parent.width - 12
                                                                text: modelData.rawValue
                                                                color: "white"
                                                                font.pixelSize: 11
                                                                font.weight: Font.DemiBold
                                                                horizontalAlignment: Text.AlignHCenter
                                                                elide: Text.ElideRight
                                                            }
                                                        }

                                                        // Yes / No Badge
                                                        Rectangle {
                                                            visible: modelData.datatype === "YesNo" && modelData.rawValue !== ""
                                                            anchors.verticalCenter: parent.verticalCenter
                                                            anchors.left: parent.left
                                                            anchors.leftMargin: 10
                                                            height: 22
                                                            width: yesNoLbl.implicitWidth + 14
                                                            radius: Math.min(4, theme.radius)
                                                            readonly property bool isYes: modelData.rawValue === "true"
                                                            color: isYes ? "#143822" : "#24242b"
                                                            border.width: 1
                                                            border.color: isYes ? "#22c55e" : "#3a3a44"

                                                            Label {
                                                                id: yesNoLbl
                                                                anchors.centerIn: parent
                                                                text: parent.isYes ? qsTr("Yes") : qsTr("No")
                                                                color: parent.isYes ? "#86efac" : "#a0a0aa"
                                                                font.pixelSize: 11
                                                                font.weight: Font.DemiBold
                                                            }
                                                        }

                                                        // Image Thumbnail (click to zoom)
                                                        Rectangle {
                                                            objectName: "gridImageThumb_" + rowDelegate.rowIdx + "_" + index
                                                            visible: modelData.datatype === "Image" && modelData.imageUrl !== ""
                                                            anchors.verticalCenter: parent.verticalCenter
                                                            anchors.left: parent.left
                                                            anchors.leftMargin: 10
                                                            width: 40
                                                            height: 40
                                                            radius: Math.min(4, theme.radius)
                                                            color: "#18181f"
                                                            border.width: 1
                                                            border.color: imgThumbHover.hovered ? theme.accent : "#34343e"
                                                            clip: true

                                                            Image {
                                                                anchors.fill: parent
                                                                anchors.margins: 1
                                                                source: modelData.imageUrl
                                                                sourceSize.width: 80
                                                                sourceSize.height: 80
                                                                fillMode: Image.PreserveAspectCrop
                                                                smooth: true
                                                                mipmap: true
                                                                asynchronous: false
                                                            }

                                                            HoverHandler {
                                                                id: imgThumbHover
                                                                cursorShape: Qt.PointingHandCursor
                                                            }

                                                            MouseArea {
                                                                anchors.fill: parent
                                                                cursorShape: Qt.PointingHandCursor
                                                                onClicked: (mouse) => {
                                                                    mouse.accepted = true
                                                                    backend.selectedRowIndex = rowDelegate.rowIdx
                                                                    win.openImagePreview(modelData.imageUrl, modelData.imageName, modelData.imageWidth, modelData.imageHeight, modelData.name)
                                                                }
                                                            }
                                                        }

                                                        // File Attachment Chip (click to open)
                                                        Rectangle {
                                                            objectName: "gridFileChip_" + rowDelegate.rowIdx + "_" + index
                                                            visible: modelData.datatype === "File" && modelData.fileName !== ""
                                                            anchors.verticalCenter: parent.verticalCenter
                                                            anchors.left: parent.left
                                                            anchors.leftMargin: 10
                                                            height: 26
                                                            width: Math.min(parent.width - 20, fileChipRow.implicitWidth + 14)
                                                            radius: Math.min(4, theme.radius)
                                                            color: fileChipHover.hovered ? "#24242f" : "#18181f"
                                                            border.width: 1
                                                            border.color: fileChipHover.hovered ? theme.accent : "#32323d"

                                                            RowLayout {
                                                                id: fileChipRow
                                                                anchors.fill: parent
                                                                anchors.leftMargin: 5
                                                                anchors.rightMargin: 8
                                                                spacing: 6

                                                                Rectangle {
                                                                    implicitWidth: fileExtLbl.implicitWidth + 10
                                                                    implicitHeight: 18
                                                                    width: fileExtLbl.implicitWidth + 10
                                                                    height: 18
                                                                    radius: Math.min(3, theme.radius)
                                                                    color: win.fileBadgeColor(modelData.fileExt)

                                                                    Label {
                                                                        id: fileExtLbl
                                                                        anchors.centerIn: parent
                                                                        text: modelData.fileExt || "FILE"
                                                                        color: "white"
                                                                        font.pixelSize: 9
                                                                        font.weight: Font.Bold
                                                                        font.family: "monospace"
                                                                    }
                                                                }

                                                                Label {
                                                                    Layout.fillWidth: true
                                                                    text: modelData.fileName
                                                                    color: fileChipHover.hovered ? "white" : "#dcdce4"
                                                                    font.pixelSize: 12
                                                                    elide: Text.ElideMiddle
                                                                }
                                                            }

                                                            HoverHandler {
                                                                id: fileChipHover
                                                                cursorShape: Qt.PointingHandCursor
                                                            }

                                                            MouseArea {
                                                                anchors.fill: parent
                                                                cursorShape: Qt.PointingHandCursor
                                                                onClicked: (mouse) => {
                                                                    mouse.accepted = true
                                                                    backend.selectedRowIndex = rowDelegate.rowIdx
                                                                    backend.openBlobAttachment(modelData.rawValue)
                                                                }
                                                            }
                                                        }

                                                        // Standard / Numeric / Stars / Euros Text Cell
                                                        Label {
                                                            visible: modelData.datatype !== "Status" && modelData.datatype !== "YesNo"
                                                                && (modelData.datatype !== "Image" || modelData.imageUrl === "")
                                                                && (modelData.datatype !== "File" || modelData.fileName === "")
                                                            anchors.fill: parent
                                                            anchors.leftMargin: 10
                                                            anchors.rightMargin: 10
                                                            verticalAlignment: Text.AlignVCenter
                                                            horizontalAlignment: (modelData.datatype === "Number" || modelData.datatype === "Euros" || modelData.datatype === "Formula")
                                                                ? Text.AlignRight
                                                                : Text.AlignLeft
                                                            color: modelData.datatype === "Formula"
                                                                ? theme.accent
                                                                : (modelData.datatype === "Stars" ? "#fbbf24" : "#e8e8ee")
                                                            font.pixelSize: 13
                                                            font.weight: modelData.datatype === "Formula" ? Font.DemiBold : Font.Normal
                                                            elide: Text.ElideRight
                                                            text: (modelData.datatype === "Image" || modelData.datatype === "File") ? "—"
                                                                : win.formatCellDisplay(modelData.datatype, modelData.rawValue, modelData.isNumeric, modelData.numericValue)
                                                        }
                                                    }
                                                }

                                                // Updated Timestamp Cell
                                                Item {
                                                    width: 96
                                                    height: parent.height
                                                    Label {
                                                        anchors.fill: parent
                                                        anchors.leftMargin: 8
                                                        anchors.rightMargin: 8
                                                        verticalAlignment: Text.AlignVCenter
                                                        text: modelData.updatedAt.slice(5)
                                                        color: "#7c7c86"
                                                        font.pixelSize: 11
                                                        font.family: "monospace"
                                                        elide: Text.ElideRight
                                                    }
                                                }

                                                // Row Edit / Delete Actions
                                                Row {
                                                    width: 96
                                                    height: parent.height
                                                    spacing: 4
                                                    leftPadding: 4

                                                    Rectangle {
                                                        anchors.verticalCenter: parent.verticalCenter
                                                        height: 26
                                                        width: editRowLbl.implicitWidth + 14
                                                        radius: Math.min(4, theme.radius)
                                                        color: editRowHover.hovered ? "#2c2c34" : "#1c1c22"
                                                        border.width: 1
                                                        border.color: "#32323c"

                                                        Label {
                                                            id: editRowLbl
                                                            anchors.centerIn: parent
                                                            text: qsTr("Edit")
                                                            color: "#d8d8e0"
                                                            font.pixelSize: 11
                                                        }
                                                        HoverHandler { id: editRowHover; cursorShape: Qt.PointingHandCursor }
                                                        TapHandler {
                                                            onTapped: {
                                                                backend.selectedRowIndex = index
                                                                win.openRecordDialog(modelData.recordIndex)
                                                            }
                                                        }
                                                    }

                                                    Rectangle {
                                                        anchors.verticalCenter: parent.verticalCenter
                                                        height: 26
                                                        width: 26
                                                        radius: Math.min(4, theme.radius)
                                                        color: delRowHover.hovered ? "#3f181c" : "#1c1c22"
                                                        border.width: 1
                                                        border.color: delRowHover.hovered ? "#dc2626" : "#32323c"

                                                        Label {
                                                            anchors.centerIn: parent
                                                            text: "×"
                                                            color: delRowHover.hovered ? "#fca5a5" : "#9a9aa4"
                                                            font.pixelSize: 13
                                                        }
                                                        HoverHandler { id: delRowHover; cursorShape: Qt.PointingHandCursor }
                                                        TapHandler {
                                                            onTapped: {
                                                                backend.selectedRowIndex = index
                                                                win.deleteRecordConfirmVisible = true
                                                            }
                                                        }
                                                    }
                                                }
                                            }

                                            HoverHandler { id: rowHover }
                                        }
                                    }
                                }

                                // Sticky Footer Aggregation Row (Sum / Average)
                                Rectangle {
                                    visible: backend.hasFooterRow && backend.recordCount > 0
                                    Layout.fillWidth: true
                                    height: 40
                                    color: "#16161b"

                                    Rectangle {
                                        anchors.top: parent.top
                                        width: parent.width
                                        height: 1
                                        color: "#2c2c36"
                                    }

                                    Row {
                                        anchors.fill: parent

                                        Item {
                                            width: 44
                                            height: parent.height
                                            Label {
                                                anchors.centerIn: parent
                                                text: "∑"
                                                color: theme.accent
                                                font.pixelSize: 13
                                                font.weight: Font.Bold
                                            }
                                        }

                                        Repeater {
                                            model: backend.fields

                                            Item {
                                                required property var modelData
                                                required property int index
                                                width: dataGridContainer.columnWidthAt(index)
                                                height: 40

                                                Label {
                                                    visible: modelData.hasFooter
                                                    anchors.fill: parent
                                                    anchors.leftMargin: 10
                                                    anchors.rightMargin: 10
                                                    verticalAlignment: Text.AlignVCenter
                                                    horizontalAlignment: (modelData.datatype === "Number" || modelData.datatype === "Euros" || modelData.datatype === "Formula")
                                                        ? Text.AlignRight
                                                        : Text.AlignLeft
                                                    color: theme.accent
                                                    font.pixelSize: 11
                                                    font.weight: Font.DemiBold
                                                    elide: Text.ElideRight
                                                    text: win.formatFooterDisplay(modelData)
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // --- Drop Area for CSV or SQLite files ---
        DropArea {
            id: fileDropArea
            anchors.fill: parent
            enabled: !win.overlayOpen
            onEntered: (drag) => {
                drag.accepted = drag.hasUrls && drag.urls.length > 0
            }
            onDropped: (drop) => {
                if (backend.handleDroppedUrls(drop.urls))
                    drop.accept(Qt.CopyAction)
            }

            Rectangle {
                anchors.fill: parent
                anchors.margins: 10
                visible: fileDropArea.containsDrag
                color: "#cc0e0e10"
                border.color: theme.accent
                border.width: 2
                radius: theme.radius

                Label {
                    anchors.centerIn: parent
                    text: qsTr("Drop a CSV file to import or a SQLite database to open")
                    color: theme.accent
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                }
            }
        }

        // --- Subtle Help Toggle (?) in the bottom-right corner ---
        Rectangle {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 10
            width: 24
            height: 24
            radius: 12
            color: helpCornerHover.hovered ? "#2c2c2f" : "transparent"

            Text {
                anchors.centerIn: parent
                text: "?"
                color: helpCornerHover.hovered ? "white" : "#7a7a80"
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }
            HoverHandler { id: helpCornerHover; cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: win.helpVisible = !win.helpVisible }
        }

        // --- Dialog 1: Create / Edit Table ---
        Rectangle {
            visible: win.tableDialogVisible
            anchors.fill: parent
            color: "#cc000000"

            MouseArea {
                anchors.fill: parent
                onClicked: win.tableDialogVisible = false
            }

            DialogPanel {
                anchors.centerIn: parent
                width: 420
                height: tableDlgCol.implicitHeight + 48

                MouseArea { anchors.fill: parent }

                ColumnLayout {
                    id: tableDlgCol
                    anchors.centerIn: parent
                    width: parent.width - 48
                    spacing: 14

                    Label {
                        text: win.editingTableId > 0 ? qsTr("Edit table") : qsTr("New table")
                        color: "white"
                        font.pixelSize: 17
                        font.weight: Font.DemiBold
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        Label {
                            text: qsTr("Table name")
                            color: "#b8b8c2"
                            font.pixelSize: 12
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            height: 36
                            radius: Math.min(4, theme.radius)
                            color: "#121216"
                            border.width: tableNameInput.activeFocus ? 2 : 1
                            border.color: tableNameInput.activeFocus ? theme.accent : "#383842"

                            TextInput {
                                id: tableNameInput
                                objectName: "tableNameInput"
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 10
                                verticalAlignment: TextInput.AlignVCenter
                                color: "white"
                                font.pixelSize: 14
                                selectByMouse: true
                                text: win.tableDialogName
                                onTextEdited: win.tableDialogName = text
                                Keys.onReturnPressed: win.submitTableDialog()
                                Keys.onEnterPressed: win.submitTableDialog()
                            }
                        }
                    }

                    // LIFO Checkbox
                    RowLayout {
                        spacing: 10
                        Rectangle {
                            width: 20
                            height: 20
                            radius: Math.min(4, theme.radius)
                            color: win.tableDialogLifo ? theme.accent : "#18181d"
                            border.width: 1
                            border.color: win.tableDialogLifo ? theme.accent : "#44444f"

                            Label {
                                anchors.centerIn: parent
                                visible: win.tableDialogLifo
                                text: "✓"
                                color: theme.accentForeground
                                font.pixelSize: 13
                                font.weight: Font.Bold
                            }
                            HoverHandler { cursorShape: Qt.PointingHandCursor }
                            TapHandler { onTapped: win.tableDialogLifo = !win.tableDialogLifo }
                        }
                        Label {
                            text: qsTr("Newest records first (LIFO)")
                            color: "#d4d4dc"
                            font.pixelSize: 13
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        Item { Layout.fillWidth: true }
                        DialogButton {
                            text: qsTr("Cancel")
                            onClicked: win.tableDialogVisible = false
                        }
                        DialogButton {
                            text: qsTr("Save")
                            primary: true
                            onClicked: win.submitTableDialog()
                        }
                    }
                }
            }
        }

        // --- Dialog 2: Manage Table Attributes (Fields) ---
        Rectangle {
            visible: win.fieldsDialogVisible
            anchors.fill: parent
            color: "#cc000000"

            MouseArea {
                anchors.fill: parent
                onClicked: win.fieldsDialogVisible = false
            }

            DialogPanel {
                id: fieldsDialogPanel
                objectName: "fieldsDialogPanel"
                anchors.centerIn: parent
                width: Math.min(820, parent.width - 64)
                height: Math.min(620, parent.height - 48)

                MouseArea { anchors.fill: parent }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 24
                    spacing: 14

                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Attributes for \"%1\"").arg(backend.currentTableName)
                            color: "white"
                            font.pixelSize: 17
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }
                        DialogButton {
                            text: qsTr("Close")
                            onClicked: win.fieldsDialogVisible = false
                        }
                    }

                    // Existing Attributes List
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: Math.min(4, theme.radius)
                        color: "#131317"
                        border.width: 1
                        border.color: "#2c2c34"

                        ListView {
                            id: schemaFieldsList
                            anchors.fill: parent
                            anchors.margins: 6
                            clip: true
                            spacing: 4
                            model: backend.fields

                            Label {
                                anchors.centerIn: parent
                                visible: backend.fieldCount === 0
                                text: qsTr("No attributes yet. Add one below.")
                                color: "#7c7c86"
                                font.pixelSize: 13
                            }

                            delegate: Rectangle {
                                required property var modelData
                                required property int index
                                width: schemaFieldsList.width
                                height: 40
                                radius: Math.min(4, theme.radius)
                                color: win.editingFieldId === modelData.id ? "#24242e" : "#1a1a20"
                                border.width: win.editingFieldId === modelData.id ? 1 : 0
                                border.color: theme.accent

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 10
                                    anchors.rightMargin: 10
                                    spacing: 8

                                    Label {
                                        text: String(index + 1) + "."
                                        color: "#70707a"
                                        font.pixelSize: 12
                                        font.family: "monospace"
                                    }

                                    Label {
                                        text: modelData.name
                                        color: "white"
                                        font.pixelSize: 13
                                        font.weight: Font.DemiBold
                                    }

                                    Rectangle {
                                        height: 20
                                        width: dtypeBadge.implicitWidth + 12
                                        radius: Math.min(4, theme.radius)
                                        color: "#262630"
                                        Label {
                                            id: dtypeBadge
                                            anchors.centerIn: parent
                                            text: win.datatypeLabel(modelData.datatype)
                                            color: theme.accent
                                            font.pixelSize: 11
                                        }
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        text: {
                                            var tags = []
                                            if (modelData.items !== "") tags.push("= " + modelData.items)
                                            if (modelData.obligatoire) tags.push(qsTr("Required"))
                                            if (modelData.filtre) tags.push(qsTr("Filter"))
                                            if (modelData.operation !== "") tags.push(modelData.operation === "Average" ? qsTr("Average") : qsTr("Sum"))
                                            return tags.join(" · ")
                                        }
                                        color: "#9a9aa6"
                                        font.pixelSize: 11
                                        elide: Text.ElideRight
                                    }

                                    // Move Up / Down
                                    Rectangle {
                                        width: 24; height: 24
                                        radius: Math.min(4, theme.radius)
                                        color: "#24242c"
                                        enabled: index > 0
                                        opacity: enabled ? 1.0 : 0.4
                                        Label { anchors.centerIn: parent; text: "↑"; color: "white"; font.pixelSize: 12 }
                                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                                        TapHandler { onTapped: backend.moveField(modelData.id, -1) }
                                    }
                                    Rectangle {
                                        width: 24; height: 24
                                        radius: Math.min(4, theme.radius)
                                        color: "#24242c"
                                        enabled: index + 1 < backend.fieldCount
                                        opacity: enabled ? 1.0 : 0.4
                                        Label { anchors.centerIn: parent; text: "↓"; color: "white"; font.pixelSize: 12 }
                                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                                        TapHandler { onTapped: backend.moveField(modelData.id, 1) }
                                    }

                                    // Edit Field
                                    Rectangle {
                                        height: 24
                                        width: editFldLbl.implicitWidth + 14
                                        radius: Math.min(4, theme.radius)
                                        color: "#24242c"
                                        Label { id: editFldLbl; anchors.centerIn: parent; text: qsTr("Edit"); color: "#e0e0e8"; font.pixelSize: 11 }
                                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                                        TapHandler { onTapped: win.startEditField(modelData) }
                                    }

                                    // Delete Field
                                    Rectangle {
                                        width: 24; height: 24
                                        radius: Math.min(4, theme.radius)
                                        color: "#2c181b"
                                        Label { anchors.centerIn: parent; text: "×"; color: "#fca5a5"; font.pixelSize: 13 }
                                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                                        TapHandler { onTapped: backend.deleteField(modelData.id) }
                                    }
                                }
                            }
                        }
                    }

                    // Add / Update Attribute Form
                    Rectangle {
                        id: fieldFormBox
                        objectName: "fieldFormBox"
                        Layout.fillWidth: true
                        implicitHeight: fieldFormCol.implicitHeight + 24
                        Layout.preferredHeight: fieldFormCol.implicitHeight + 24
                        Layout.minimumHeight: fieldFormCol.implicitHeight + 24
                        radius: Math.min(4, theme.radius)
                        color: "#16161b"
                        border.width: 1
                        border.color: "#2c2c34"

                        ColumnLayout {
                            id: fieldFormCol
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 10

                            Label {
                                text: win.editingFieldId > 0 ? qsTr("Update attribute") : qsTr("New attribute")
                                color: theme.accent
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                            }

                            // Datatype selector chips
                            Flow {
                                Layout.fillWidth: true
                                spacing: 6

                                Repeater {
                                    model: win.datatypesModel

                                    Rectangle {
                                        required property var modelData
                                        height: 26
                                        width: dtChipLbl.implicitWidth + 16
                                        radius: Math.min(4, theme.radius)
                                        readonly property bool active: win.fieldFormDatatype === modelData.code
                                        color: active ? theme.accent : "#22222a"
                                        border.width: 1
                                        border.color: active ? theme.accent : "#34343e"

                                        Label {
                                            id: dtChipLbl
                                            anchors.centerIn: parent
                                            text: modelData.label
                                            color: parent.active ? theme.accentForeground : "#d4d4dc"
                                            font.pixelSize: 11
                                            font.weight: parent.active ? Font.DemiBold : Font.Normal
                                        }
                                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                                        TapHandler {
                                            onTapped: {
                                                win.fieldFormDatatype = modelData.code
                                                if (modelData.code === "Relation" && win.relationTargetTable === "") {
                                                    var tlist = backend.tables
                                                    for (var i = 0; i < tlist.length; ++i) {
                                                        if (tlist[i].id !== backend.currentTableId) {
                                                            win.setRelationTarget(tlist[i].name, "")
                                                            break
                                                        }
                                                    }
                                                    if (win.relationTargetTable === "" && tlist.length > 0)
                                                        win.setRelationTarget(tlist[0].name, "")
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10

                                // Attribute Name Input
                                Rectangle {
                                    Layout.preferredWidth: 220
                                    implicitHeight: 34
                                    height: 34
                                    radius: Math.min(4, theme.radius)
                                    color: "#101014"
                                    border.width: fieldNameInput.activeFocus ? 2 : 1
                                    border.color: fieldNameInput.activeFocus ? theme.accent : "#34343e"

                                    TextInput {
                                        id: fieldNameInput
                                        objectName: "fieldNameInput"
                                        anchors.fill: parent
                                        anchors.leftMargin: 8
                                        anchors.rightMargin: 8
                                        verticalAlignment: TextInput.AlignVCenter
                                        color: "white"
                                        font.pixelSize: 13
                                        selectByMouse: true
                                        text: win.fieldFormName
                                        onTextEdited: win.fieldFormName = text
                                        Keys.onReturnPressed: win.submitFieldForm()
                                        Keys.onEnterPressed: win.submitFieldForm()

                                        Label {
                                            anchors.fill: parent
                                            verticalAlignment: Text.AlignVCenter
                                            visible: fieldNameInput.text === "" && !fieldNameInput.activeFocus
                                            text: qsTr("Attribute name")
                                            color: "#6a6a74"
                                            font.pixelSize: 12
                                        }
                                    }
                                }

                                // Parameters / Options Input (for List, Status, Formula)
                                Rectangle {
                                    Layout.fillWidth: true
                                    implicitHeight: 34
                                    height: 34
                                    radius: Math.min(4, theme.radius)
                                    color: "#101014"
                                    border.width: fieldItemsInput.activeFocus ? 2 : 1
                                    border.color: fieldItemsInput.activeFocus ? theme.accent : "#34343e"

                                    TextInput {
                                        id: fieldItemsInput
                                        objectName: "fieldItemsInput"
                                        anchors.fill: parent
                                        anchors.leftMargin: 8
                                        anchors.rightMargin: 8
                                        verticalAlignment: TextInput.AlignVCenter
                                        color: "white"
                                        font.pixelSize: 13
                                        selectByMouse: true
                                        text: win.fieldFormItems
                                        onTextEdited: win.fieldFormItems = text
                                        Keys.onReturnPressed: win.submitFieldForm()
                                        Keys.onEnterPressed: win.submitFieldForm()

                                        Label {
                                            anchors.fill: parent
                                            verticalAlignment: Text.AlignVCenter
                                            visible: fieldItemsInput.text === "" && !fieldItemsInput.activeFocus
                                            text: {
                                                if (win.fieldFormDatatype === "List")
                                                    return qsTr("Options: Todo, Doing, Done")
                                                if (win.fieldFormDatatype === "Status")
                                                    return qsTr("Status: Open:blue, Closed:green")
                                                if (win.fieldFormDatatype === "Relation")
                                                    return qsTr("Relation: Table:Attribute (e.g. Clients:name)")
                                                if (win.fieldFormDatatype === "Formula")
                                                    return qsTr("Formula: [Qty] * [Unit Price]")
                                                return qsTr("Optional parameters")
                                            }
                                            color: "#6a6a74"
                                            font.pixelSize: 12
                                        }
                                    }
                                }
                            }

                            // Relation Target Table & Attribute Picker
                            ColumnLayout {
                                visible: win.fieldFormDatatype === "Relation"
                                Layout.fillWidth: true
                                spacing: 6

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 6

                                    Label {
                                        text: qsTr("Source table:")
                                        color: "#9a9aa4"
                                        font.pixelSize: 12
                                    }

                                    Flow {
                                        Layout.fillWidth: true
                                        spacing: 6

                                        Repeater {
                                            model: backend.tables

                                            Rectangle {
                                                required property var modelData
                                                height: 24
                                                width: relTblLbl.implicitWidth + 14
                                                radius: Math.min(4, theme.radius)
                                                readonly property bool active: win.relationTargetTable.toLowerCase() === String(modelData.name).toLowerCase()
                                                color: active ? theme.accent : "#22222a"
                                                border.width: 1
                                                border.color: active ? theme.accent : "#34343e"

                                                Label {
                                                    id: relTblLbl
                                                    anchors.centerIn: parent
                                                    text: modelData.name
                                                    color: parent.active ? theme.accentForeground : "#d4d4dc"
                                                    font.pixelSize: 11
                                                    font.weight: parent.active ? Font.DemiBold : Font.Normal
                                                }
                                                HoverHandler { cursorShape: Qt.PointingHandCursor }
                                                TapHandler { onTapped: win.setRelationTarget(modelData.name, "") }
                                            }
                                        }
                                    }
                                }

                                RowLayout {
                                    visible: win.relationTargetFieldNames.length > 0
                                    Layout.fillWidth: true
                                    spacing: 6

                                    Label {
                                        text: qsTr("Display attribute:")
                                        color: "#9a9aa4"
                                        font.pixelSize: 12
                                    }

                                    Flow {
                                        Layout.fillWidth: true
                                        spacing: 6

                                        Repeater {
                                            model: win.relationTargetFieldNames

                                            Rectangle {
                                                required property string modelData
                                                height: 24
                                                width: relFldLbl.implicitWidth + 14
                                                radius: Math.min(4, theme.radius)
                                                readonly property bool active: win.relationTargetField.toLowerCase() === modelData.toLowerCase()
                                                color: active ? "#2d2d38" : "#1c1c22"
                                                border.width: 1
                                                border.color: active ? theme.accent : "#32323c"

                                                Label {
                                                    id: relFldLbl
                                                    anchors.centerIn: parent
                                                    text: modelData
                                                    color: parent.active ? theme.accent : "#b8b8c2"
                                                    font.pixelSize: 11
                                                    font.weight: parent.active ? Font.DemiBold : Font.Normal
                                                }
                                                HoverHandler { cursorShape: Qt.PointingHandCursor }
                                                TapHandler { onTapped: win.setRelationTarget(win.relationTargetTable, modelData) }
                                            }
                                        }
                                    }
                                }
                            }

                            // Flags (Required, Filter, Footer Aggregation, Submit)
                            RowLayout {
                                id: fieldFormFlagsRow
                                objectName: "fieldFormFlagsRow"
                                Layout.fillWidth: true
                                spacing: 14

                                // Required toggle
                                RowLayout {
                                    id: fieldFormRequiredCheck
                                    objectName: "fieldFormRequiredCheck"
                                    spacing: 6
                                    Rectangle {
                                        implicitWidth: 18; implicitHeight: 18
                                        width: 18; height: 18
                                        radius: Math.min(4, theme.radius)
                                        color: win.fieldFormObligatoire ? theme.accent : "#18181d"
                                        border.width: 1
                                        border.color: win.fieldFormObligatoire ? theme.accent : "#44444f"
                                        Label {
                                            anchors.centerIn: parent
                                            visible: win.fieldFormObligatoire
                                            text: "✓"
                                            color: theme.accentForeground
                                            font.pixelSize: 11
                                            font.weight: Font.Bold
                                        }
                                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                                        TapHandler { onTapped: win.fieldFormObligatoire = !win.fieldFormObligatoire }
                                    }
                                    Label { text: qsTr("Required"); color: "#d4d4dc"; font.pixelSize: 12 }
                                }

                                // Filter toggle
                                RowLayout {
                                    spacing: 6
                                    Rectangle {
                                        implicitWidth: 18; implicitHeight: 18
                                        width: 18; height: 18
                                        radius: Math.min(4, theme.radius)
                                        color: win.fieldFormFiltre ? theme.accent : "#18181d"
                                        border.width: 1
                                        border.color: win.fieldFormFiltre ? theme.accent : "#44444f"
                                        Label {
                                            anchors.centerIn: parent
                                            visible: win.fieldFormFiltre
                                            text: "✓"
                                            color: theme.accentForeground
                                            font.pixelSize: 11
                                            font.weight: Font.Bold
                                        }
                                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                                        TapHandler { onTapped: win.fieldFormFiltre = !win.fieldFormFiltre }
                                    }
                                    Label { text: qsTr("Quick filter"); color: "#d4d4dc"; font.pixelSize: 12 }
                                }

                                // Footer Operation (None / Sum / Average)
                                RowLayout {
                                    visible: win.fieldFormDatatype === "Number" || win.fieldFormDatatype === "Euros"
                                        || win.fieldFormDatatype === "Formula" || win.fieldFormDatatype === "Stars"
                                    spacing: 6

                                    Label { text: qsTr("Footer:"); color: "#9a9aa4"; font.pixelSize: 12 }

                                    Repeater {
                                        model: [
                                            { code: "", label: qsTr("None") },
                                            { code: "Sum", label: qsTr("Sum") },
                                            { code: "Average", label: qsTr("Average") }
                                        ]

                                        Rectangle {
                                            required property var modelData
                                            implicitHeight: 24
                                            implicitWidth: opChipLbl.implicitWidth + 14
                                            height: 24
                                            width: opChipLbl.implicitWidth + 14
                                            radius: Math.min(4, theme.radius)
                                            readonly property bool selected: win.fieldFormOperation === modelData.code
                                            color: selected ? "#2d2d38" : "#1c1c22"
                                            border.width: 1
                                            border.color: selected ? theme.accent : "#32323c"

                                            Label {
                                                id: opChipLbl
                                                anchors.centerIn: parent
                                                text: modelData.label
                                                color: parent.selected ? theme.accent : "#b8b8c2"
                                                font.pixelSize: 11
                                            }
                                            HoverHandler { cursorShape: Qt.PointingHandCursor }
                                            TapHandler { onTapped: win.fieldFormOperation = modelData.code }
                                        }
                                    }
                                }

                                Item { Layout.fillWidth: true }

                                DialogButton {
                                    visible: win.editingFieldId > 0
                                    text: qsTr("Cancel")
                                    onClicked: win.resetFieldForm()
                                }

                                DialogButton {
                                    text: win.editingFieldId > 0 ? qsTr("Update") : qsTr("Add attribute")
                                    primary: true
                                    onClicked: win.submitFieldForm()
                                }
                            }
                        }
                    }
                }
            }
        }

        // --- Dialog 3: Dynamic Record Form (New / Edit Record) ---
        Rectangle {
            id: recordModalOverlay
            objectName: "recordModalOverlay"
            visible: win.recordDialogVisible
            anchors.fill: parent
            color: "#cc000000"

            MouseArea {
                anchors.fill: parent
                onClicked: win.closeRecordDialog()
            }

            DialogPanel {
                id: recordDialogPanel
                objectName: "recordDialogPanel"
                anchors.centerIn: parent
                width: Math.min(640, parent.width - 64)
                height: Math.min(parent.height - 48, Math.max(240, recordFieldsCol.implicitHeight + 148))

                MouseArea { anchors.fill: parent }

                ColumnLayout {
                    id: recordDlgCol
                    anchors.fill: parent
                    anchors.margins: 24
                    spacing: 14

                    Label {
                        Layout.fillWidth: true
                        text: win.editingRecordIndex > 0
                            ? qsTr("Edit record #%1 — %2").arg(win.editingRecordIndex).arg(backend.currentTableName)
                            : qsTr("New record — %1").arg(backend.currentTableName)
                        color: "white"
                        font.pixelSize: 17
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }

                    Flickable {
                        id: recordFlick
                        objectName: "recordFlick"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        contentWidth: width
                        contentHeight: recordFieldsCol.implicitHeight
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds

                        ColumnLayout {
                            id: recordFieldsCol
                            objectName: "recordFieldsCol"
                            width: recordFlick.width
                            spacing: 12

                            Repeater {
                                id: recordFieldsRepeater
                                model: backend.fields

                                ColumnLayout {
                                    id: fieldDelegate
                                    required property var modelData
                                    required property int index
                                    Layout.fillWidth: true
                                    spacing: 4

                                    readonly property int fieldId: modelData.id
                                    readonly property string fieldKey: String(modelData.id)
                                    readonly property string currentValue: win.recordFormValues[fieldKey] !== undefined
                                        ? String(win.recordFormValues[fieldKey]) : ""

                                    function focusInput() {
                                        if (textInputBox.visible) {
                                            valTextInput.forceActiveFocus()
                                            return true
                                        }
                                        return false
                                    }

                                    RowLayout {
                                        spacing: 6
                                        Label {
                                            text: fieldDelegate.modelData.name + (fieldDelegate.modelData.obligatoire ? " *" : "")
                                            color: "#dcdce4"
                                            font.pixelSize: 12
                                            font.weight: Font.DemiBold
                                        }
                                        Label {
                                            text: "(" + win.datatypeLabel(fieldDelegate.modelData.datatype) + ")"
                                            color: "#74747e"
                                            font.pixelSize: 11
                                        }
                                    }

                                    // Yes / No Switch
                                    RowLayout {
                                        visible: fieldDelegate.modelData.datatype === "YesNo"
                                        spacing: 8

                                        Repeater {
                                            model: [
                                                { val: "true", label: qsTr("Yes") },
                                                { val: "false", label: qsTr("No") }
                                            ]

                                            Rectangle {
                                                required property var modelData
                                                height: 30
                                                width: ynChoiceLbl.implicitWidth + 24
                                                radius: Math.min(4, theme.radius)
                                                readonly property bool active: fieldDelegate.currentValue === modelData.val
                                                color: active ? theme.accent : "#18181f"
                                                border.width: 1
                                                border.color: active ? theme.accent : "#34343e"

                                                Label {
                                                    id: ynChoiceLbl
                                                    anchors.centerIn: parent
                                                    text: modelData.label
                                                    color: parent.active ? theme.accentForeground : "#d0d0d8"
                                                    font.pixelSize: 12
                                                    font.weight: parent.active ? Font.DemiBold : Font.Normal
                                                }
                                                HoverHandler { cursorShape: Qt.PointingHandCursor }
                                                TapHandler { onTapped: win.setRecordFieldValue(fieldDelegate.fieldId, modelData.val) }
                                            }
                                        }
                                    }

                                    // List, Status, or Relation Choice Chips
                                    Flow {
                                        visible: fieldDelegate.modelData.datatype === "List" || fieldDelegate.modelData.datatype === "Status"
                                            || fieldDelegate.modelData.datatype === "Relation"
                                        Layout.fillWidth: true
                                        spacing: 6

                                        Rectangle {
                                            visible: fieldDelegate.modelData.datatype === "Relation" && !fieldDelegate.modelData.obligatoire
                                                && (fieldDelegate.modelData.choices || []).length > 0
                                            height: 28
                                            width: noneRelLbl.implicitWidth + 18
                                            radius: Math.min(4, theme.radius)
                                            readonly property bool active: fieldDelegate.currentValue === ""
                                            color: active ? theme.accent : "#18181f"
                                            border.width: 1
                                            border.color: active ? theme.accent : "#34343e"

                                            Label {
                                                id: noneRelLbl
                                                anchors.centerIn: parent
                                                text: qsTr("None")
                                                color: parent.active ? theme.accentForeground : "#a0a0aa"
                                                font.pixelSize: 12
                                                font.weight: parent.active ? Font.DemiBold : Font.Normal
                                            }
                                            HoverHandler { cursorShape: Qt.PointingHandCursor }
                                            TapHandler { onTapped: win.setRecordFieldValue(fieldDelegate.fieldId, "") }
                                        }

                                        Repeater {
                                            model: fieldDelegate.modelData.choices || []

                                            Rectangle {
                                                required property string modelData
                                                height: 28
                                                width: choiceChipLbl.implicitWidth + 18
                                                radius: Math.min(4, theme.radius)
                                                readonly property bool active: fieldDelegate.currentValue === modelData
                                                color: active ? theme.accent : "#18181f"
                                                border.width: 1
                                                border.color: active ? theme.accent : "#34343e"

                                                Label {
                                                    id: choiceChipLbl
                                                    anchors.centerIn: parent
                                                    text: modelData
                                                    color: parent.active ? theme.accentForeground : "#d4d4dc"
                                                    font.pixelSize: 12
                                                    font.weight: parent.active ? Font.DemiBold : Font.Normal
                                                }
                                                HoverHandler { cursorShape: Qt.PointingHandCursor }
                                                TapHandler { onTapped: win.setRecordFieldValue(fieldDelegate.fieldId, modelData) }
                                            }
                                        }

                                        Label {
                                            visible: fieldDelegate.modelData.datatype === "Relation" && (fieldDelegate.modelData.choices || []).length === 0
                                            text: qsTr("No records found in related table (%1)").arg(fieldDelegate.modelData.items)
                                            color: "#8a8a94"
                                            font.pixelSize: 12
                                        }
                                    }

                                    // Stars (1..5) Selector
                                    RowLayout {
                                        visible: fieldDelegate.modelData.datatype === "Stars"
                                        spacing: 6

                                        Repeater {
                                            model: 5
                                            Rectangle {
                                                required property int index
                                                width: 34; height: 30
                                                radius: Math.min(4, theme.radius)
                                                readonly property int starNum: index + 1
                                                readonly property bool filled: Number(fieldDelegate.currentValue || 0) >= starNum
                                                color: filled ? "#2b2512" : "#18181f"
                                                border.width: 1
                                                border.color: filled ? "#fbbf24" : "#34343e"

                                                Label {
                                                    anchors.centerIn: parent
                                                    text: parent.filled ? "★" : "☆"
                                                    color: parent.filled ? "#fbbf24" : "#7c7c86"
                                                    font.pixelSize: 15
                                                }
                                                HoverHandler { cursorShape: Qt.PointingHandCursor }
                                                TapHandler { onTapped: win.setRecordFieldValue(fieldDelegate.fieldId, String(starNum)) }
                                            }
                                        }
                                    }

                                    // Formula Read-only Banner
                                    Rectangle {
                                        visible: fieldDelegate.modelData.datatype === "Formula"
                                        Layout.fillWidth: true
                                        height: 34
                                        radius: Math.min(4, theme.radius)
                                        color: "#15151a"
                                        border.width: 1
                                        border.color: "#2c2c34"

                                        Label {
                                            anchors.fill: parent
                                            anchors.leftMargin: 10
                                            verticalAlignment: Text.AlignVCenter
                                            text: qsTr("Calculated automatically: %1").arg(fieldDelegate.modelData.items)
                                            color: theme.accent
                                            font.pixelSize: 12
                                            font.family: "monospace"
                                        }
                                    }

                                    // Image Picker & Thumbnail Preview
                                    Rectangle {
                                        id: imageFieldBox
                                        objectName: "recordImageField_" + fieldDelegate.index
                                        visible: fieldDelegate.modelData.datatype === "Image"
                                        Layout.fillWidth: true
                                        implicitHeight: 84
                                        Layout.preferredHeight: 84
                                        radius: Math.min(4, theme.radius)
                                        color: imgDropArea.containsDrag ? "#1c202c" : "#141419"
                                        border.width: imgDropArea.containsDrag ? 2 : 1
                                        border.color: imgDropArea.containsDrag ? theme.accent : "#2e2e36"

                                        readonly property var info: visible && fieldDelegate.currentValue !== ""
                                            ? backend.blobInfo(fieldDelegate.currentValue)
                                            : ({})
                                        readonly property string imgUrl: info.url ? String(info.url) : ""
                                        readonly property string imgName: info.filename ? String(info.filename) : ""
                                        readonly property int imgW: Number(info.width) || 0
                                        readonly property int imgH: Number(info.height) || 0

                                        DropArea {
                                            id: imgDropArea
                                            anchors.fill: parent
                                            enabled: imageFieldBox.visible
                                            onEntered: (drag) => {
                                                drag.accepted = drag.hasUrls && drag.urls.length > 0
                                            }
                                            onDropped: (drop) => {
                                                if (drop.hasUrls && drop.urls.length > 0) {
                                                    var ref = backend.importImageUrl(drop.urls[0])
                                                    if (ref !== "") {
                                                        win.setRecordFieldValue(fieldDelegate.fieldId, ref)
                                                        drop.accept(Qt.CopyAction)
                                                    }
                                                }
                                            }
                                        }

                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.margins: 10
                                            spacing: 12

                                            // 64x64 Preview Box (click to zoom or pick)
                                            Rectangle {
                                                Layout.preferredWidth: 64
                                                Layout.preferredHeight: 64
                                                implicitWidth: 64
                                                implicitHeight: 64
                                                radius: Math.min(4, theme.radius)
                                                color: "#101014"
                                                border.width: 1
                                                border.color: formThumbHover.hovered ? theme.accent : "#34343e"
                                                clip: true

                                                Image {
                                                    visible: imageFieldBox.imgUrl !== ""
                                                    anchors.fill: parent
                                                    anchors.margins: 1
                                                    source: imageFieldBox.imgUrl
                                                    sourceSize.width: 128
                                                    sourceSize.height: 128
                                                    fillMode: Image.PreserveAspectCrop
                                                    smooth: true
                                                    mipmap: true
                                                    asynchronous: false
                                                }

                                                Label {
                                                    visible: imageFieldBox.imgUrl === ""
                                                    anchors.centerIn: parent
                                                    text: qsTr("None")
                                                    color: "#62626c"
                                                    font.pixelSize: 11
                                                }

                                                HoverHandler {
                                                    id: formThumbHover
                                                    cursorShape: Qt.PointingHandCursor
                                                }
                                                TapHandler {
                                                    onTapped: {
                                                        if (imageFieldBox.imgUrl !== "") {
                                                            win.openImagePreview(imageFieldBox.imgUrl, imageFieldBox.imgName, imageFieldBox.imgW, imageFieldBox.imgH, fieldDelegate.modelData.name)
                                                        } else {
                                                            backend.pickImageDialog(fieldDelegate.fieldId)
                                                        }
                                                    }
                                                }
                                            }

                                            ColumnLayout {
                                                Layout.fillWidth: true
                                                spacing: 6

                                                Label {
                                                    Layout.fillWidth: true
                                                    elide: Text.ElideRight
                                                    text: imageFieldBox.imgUrl !== ""
                                                        ? (imageFieldBox.imgName !== "" ? imageFieldBox.imgName : qsTr("Embedded image"))
                                                          + (imageFieldBox.imgW > 0 && imageFieldBox.imgH > 0
                                                             ? (" · " + imageFieldBox.imgW + "×" + imageFieldBox.imgH) : "")
                                                        : qsTr("Drop an image here or choose a file")
                                                    color: imageFieldBox.imgUrl !== "" ? "#dcdce4" : "#7a7a84"
                                                    font.pixelSize: 12
                                                }

                                                RowLayout {
                                                    spacing: 8

                                                    Rectangle {
                                                        objectName: "recordImagePickBtn_" + fieldDelegate.index
                                                        implicitHeight: 28
                                                        implicitWidth: pickImgLbl.implicitWidth + 18
                                                        height: 28
                                                        width: pickImgLbl.implicitWidth + 18
                                                        radius: Math.min(4, theme.radius)
                                                        color: pickImgHover.hovered ? "#2c2c36" : "#202028"
                                                        border.width: 1
                                                        border.color: pickImgHover.hovered ? theme.accent : "#383844"

                                                        Label {
                                                            id: pickImgLbl
                                                            anchors.centerIn: parent
                                                            text: imageFieldBox.imgUrl !== "" ? qsTr("Replace…") : qsTr("Choose image…")
                                                            color: "#e8e8ee"
                                                            font.pixelSize: 12
                                                        }
                                                        HoverHandler { id: pickImgHover; cursorShape: Qt.PointingHandCursor }
                                                        TapHandler { onTapped: backend.pickImageDialog(fieldDelegate.fieldId) }
                                                    }

                                                    Rectangle {
                                                        visible: imageFieldBox.imgUrl !== ""
                                                        implicitHeight: 28
                                                        implicitWidth: prevImgLbl.implicitWidth + 18
                                                        height: 28
                                                        width: prevImgLbl.implicitWidth + 18
                                                        radius: Math.min(4, theme.radius)
                                                        color: prevImgHover.hovered ? "#2c2c36" : "#1c1c22"
                                                        border.width: 1
                                                        border.color: "#32323c"

                                                        Label {
                                                            id: prevImgLbl
                                                            anchors.centerIn: parent
                                                            text: qsTr("Preview")
                                                            color: "#d4d4dc"
                                                            font.pixelSize: 12
                                                        }
                                                        HoverHandler { id: prevImgHover; cursorShape: Qt.PointingHandCursor }
                                                        TapHandler {
                                                            onTapped: win.openImagePreview(imageFieldBox.imgUrl, imageFieldBox.imgName, imageFieldBox.imgW, imageFieldBox.imgH, fieldDelegate.modelData.name)
                                                        }
                                                    }

                                                    Rectangle {
                                                        visible: imageFieldBox.imgUrl !== ""
                                                        implicitHeight: 28
                                                        implicitWidth: clrImgLbl.implicitWidth + 16
                                                        height: 28
                                                        width: clrImgLbl.implicitWidth + 16
                                                        radius: Math.min(4, theme.radius)
                                                        color: clrImgHover.hovered ? "#3f181c" : "#1c1c22"
                                                        border.width: 1
                                                        border.color: clrImgHover.hovered ? "#dc2626" : "#32323c"

                                                        Label {
                                                            id: clrImgLbl
                                                            anchors.centerIn: parent
                                                            text: qsTr("Clear")
                                                            color: clrImgHover.hovered ? "#fca5a5" : "#a0a0aa"
                                                            font.pixelSize: 12
                                                        }
                                                        HoverHandler { id: clrImgHover; cursorShape: Qt.PointingHandCursor }
                                                        TapHandler { onTapped: win.setRecordFieldValue(fieldDelegate.fieldId, "") }
                                                    }
                                                }
                                            }
                                        }
                                    }

                                    // File Attachment Picker & Actions
                                    Rectangle {
                                        id: fileFieldBox
                                        objectName: "recordFileField_" + fieldDelegate.index
                                        visible: fieldDelegate.modelData.datatype === "File"
                                        Layout.fillWidth: true
                                        implicitHeight: 84
                                        Layout.preferredHeight: 84
                                        radius: Math.min(4, theme.radius)
                                        color: fileFieldDropArea.containsDrag ? "#1c202c" : "#141419"
                                        border.width: fileFieldDropArea.containsDrag ? 2 : 1
                                        border.color: fileFieldDropArea.containsDrag ? theme.accent : "#2e2e36"

                                        readonly property var info: visible && fieldDelegate.currentValue !== ""
                                            ? backend.blobInfo(fieldDelegate.currentValue)
                                            : ({})
                                        readonly property bool hasFile: Boolean(info.id && info.id > 0)
                                        readonly property string fileName: info.filename ? String(info.filename) : ""
                                        readonly property string fileExt: info.extension ? String(info.extension) : "FILE"
                                        readonly property string fileSizeLabel: info.sizeLabel ? String(info.sizeLabel) : ""

                                        DropArea {
                                            id: fileFieldDropArea
                                            anchors.fill: parent
                                            enabled: fileFieldBox.visible
                                            onEntered: (drag) => {
                                                drag.accepted = drag.hasUrls && drag.urls.length > 0
                                            }
                                            onDropped: (drop) => {
                                                if (drop.hasUrls && drop.urls.length > 0) {
                                                    var ref = backend.importAttachmentUrl(drop.urls[0])
                                                    if (ref !== "") {
                                                        win.setRecordFieldValue(fieldDelegate.fieldId, ref)
                                                        drop.accept(Qt.CopyAction)
                                                    }
                                                }
                                            }
                                        }

                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.margins: 10
                                            spacing: 12

                                            // 56x56 Extension Badge Box (click to open or pick)
                                            Rectangle {
                                                Layout.preferredWidth: 56
                                                Layout.preferredHeight: 56
                                                implicitWidth: 56
                                                implicitHeight: 56
                                                radius: Math.min(4, theme.radius)
                                                color: fileFieldBox.hasFile ? win.fileBadgeColor(fileFieldBox.fileExt) : "#101014"
                                                border.width: 1
                                                border.color: formFileBadgeHover.hovered ? theme.accent : "#34343e"

                                                Label {
                                                    anchors.centerIn: parent
                                                    text: fileFieldBox.hasFile ? fileFieldBox.fileExt : qsTr("None")
                                                    color: fileFieldBox.hasFile ? "white" : "#62626c"
                                                    font.pixelSize: fileFieldBox.hasFile ? 12 : 11
                                                    font.weight: fileFieldBox.hasFile ? Font.Bold : Font.Normal
                                                    font.family: fileFieldBox.hasFile ? "monospace" : ""
                                                }

                                                HoverHandler {
                                                    id: formFileBadgeHover
                                                    cursorShape: Qt.PointingHandCursor
                                                }
                                                TapHandler {
                                                    onTapped: {
                                                        if (fileFieldBox.hasFile)
                                                            backend.openBlobAttachment(fieldDelegate.currentValue)
                                                        else
                                                            backend.pickFileDialog(fieldDelegate.fieldId)
                                                    }
                                                }
                                            }

                                            ColumnLayout {
                                                Layout.fillWidth: true
                                                spacing: 6

                                                Label {
                                                    Layout.fillWidth: true
                                                    elide: Text.ElideMiddle
                                                    text: fileFieldBox.hasFile
                                                        ? (fileFieldBox.fileName !== "" ? fileFieldBox.fileName : qsTr("Attached file"))
                                                          + (fileFieldBox.fileSizeLabel !== "" ? (" · " + fileFieldBox.fileSizeLabel) : "")
                                                        : qsTr("Drop any file here or choose a file")
                                                    color: fileFieldBox.hasFile ? "#dcdce4" : "#7a7a84"
                                                    font.pixelSize: 12
                                                }

                                                RowLayout {
                                                    spacing: 8

                                                    Rectangle {
                                                        objectName: "recordFilePickBtn_" + fieldDelegate.index
                                                        implicitHeight: 28
                                                        implicitWidth: pickFileLbl.implicitWidth + 18
                                                        height: 28
                                                        width: pickFileLbl.implicitWidth + 18
                                                        radius: Math.min(4, theme.radius)
                                                        color: pickFileHover.hovered ? "#2c2c36" : "#202028"
                                                        border.width: 1
                                                        border.color: pickFileHover.hovered ? theme.accent : "#383844"

                                                        Label {
                                                            id: pickFileLbl
                                                            anchors.centerIn: parent
                                                            text: fileFieldBox.hasFile ? qsTr("Replace…") : qsTr("Choose file…")
                                                            color: "#e8e8ee"
                                                            font.pixelSize: 12
                                                        }
                                                        HoverHandler { id: pickFileHover; cursorShape: Qt.PointingHandCursor }
                                                        TapHandler { onTapped: backend.pickFileDialog(fieldDelegate.fieldId) }
                                                    }

                                                    Rectangle {
                                                        visible: fileFieldBox.hasFile
                                                        implicitHeight: 28
                                                        implicitWidth: openFileLbl.implicitWidth + 18
                                                        height: 28
                                                        width: openFileLbl.implicitWidth + 18
                                                        radius: Math.min(4, theme.radius)
                                                        color: openFileHover.hovered ? "#2c2c36" : "#1c1c22"
                                                        border.width: 1
                                                        border.color: "#32323c"

                                                        Label {
                                                            id: openFileLbl
                                                            anchors.centerIn: parent
                                                            text: qsTr("Open")
                                                            color: "#d4d4dc"
                                                            font.pixelSize: 12
                                                        }
                                                        HoverHandler { id: openFileHover; cursorShape: Qt.PointingHandCursor }
                                                        TapHandler { onTapped: backend.openBlobAttachment(fieldDelegate.currentValue) }
                                                    }

                                                    Rectangle {
                                                        visible: fileFieldBox.hasFile
                                                        implicitHeight: 28
                                                        implicitWidth: saveFileLbl.implicitWidth + 18
                                                        height: 28
                                                        width: saveFileLbl.implicitWidth + 18
                                                        radius: Math.min(4, theme.radius)
                                                        color: saveFileHover.hovered ? "#2c2c36" : "#1c1c22"
                                                        border.width: 1
                                                        border.color: "#32323c"

                                                        Label {
                                                            id: saveFileLbl
                                                            anchors.centerIn: parent
                                                            text: qsTr("Save as…")
                                                            color: "#d4d4dc"
                                                            font.pixelSize: 12
                                                        }
                                                        HoverHandler { id: saveFileHover; cursorShape: Qt.PointingHandCursor }
                                                        TapHandler { onTapped: backend.saveBlobDialog(fieldDelegate.currentValue) }
                                                    }

                                                    Rectangle {
                                                        visible: fileFieldBox.hasFile
                                                        implicitHeight: 28
                                                        implicitWidth: clrFileLbl.implicitWidth + 16
                                                        height: 28
                                                        width: clrFileLbl.implicitWidth + 16
                                                        radius: Math.min(4, theme.radius)
                                                        color: clrFileHover.hovered ? "#3f181c" : "#1c1c22"
                                                        border.width: 1
                                                        border.color: clrFileHover.hovered ? "#dc2626" : "#32323c"

                                                        Label {
                                                            id: clrFileLbl
                                                            anchors.centerIn: parent
                                                            text: qsTr("Clear")
                                                            color: clrFileHover.hovered ? "#fca5a5" : "#a0a0aa"
                                                            font.pixelSize: 12
                                                        }
                                                        HoverHandler { id: clrFileHover; cursorShape: Qt.PointingHandCursor }
                                                        TapHandler { onTapped: win.setRecordFieldValue(fieldDelegate.fieldId, "") }
                                                    }
                                                }
                                            }
                                        }
                                    }

                                    // Standard Text / Number / Euros / Date / Tags / URL / Email Input
                                    Rectangle {
                                        id: textInputBox
                                        visible: fieldDelegate.modelData.datatype !== "YesNo" && fieldDelegate.modelData.datatype !== "List"
                                            && fieldDelegate.modelData.datatype !== "Status" && fieldDelegate.modelData.datatype !== "Relation"
                                            && fieldDelegate.modelData.datatype !== "Stars" && fieldDelegate.modelData.datatype !== "Formula"
                                            && fieldDelegate.modelData.datatype !== "Image" && fieldDelegate.modelData.datatype !== "File"
                                        Layout.fillWidth: true
                                        height: 36
                                        radius: Math.min(4, theme.radius)
                                        color: "#101014"
                                        border.width: valTextInput.activeFocus ? 2 : 1
                                        border.color: valTextInput.activeFocus ? theme.accent : "#34343e"

                                        TextInput {
                                            id: valTextInput
                                            objectName: "recordFieldInput_" + fieldDelegate.index
                                            anchors.fill: parent
                                            anchors.leftMargin: 10
                                            anchors.rightMargin: 10
                                            verticalAlignment: TextInput.AlignVCenter
                                            color: "white"
                                            font.pixelSize: 13
                                            selectByMouse: true
                                            Component.onCompleted: text = fieldDelegate.currentValue
                                            onTextChanged: {
                                                if (text !== fieldDelegate.currentValue)
                                                    win.setRecordFieldValue(fieldDelegate.fieldId, text)
                                            }
                                            Keys.onReturnPressed: win.submitRecordDialog(false)
                                            Keys.onEnterPressed: win.submitRecordDialog(false)

                                            Connections {
                                                target: win
                                                function onRecordFormReset() {
                                                    valTextInput.text = fieldDelegate.currentValue
                                                }
                                            }

                                            Label {
                                                anchors.fill: parent
                                                verticalAlignment: Text.AlignVCenter
                                                visible: valTextInput.text === "" && !valTextInput.activeFocus
                                                text: fieldDelegate.modelData.datatype === "Date" ? "YYYY-MM-DD" : fieldDelegate.modelData.name
                                                color: "#62626c"
                                                font.pixelSize: 12
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        Item { Layout.fillWidth: true }
                        DialogButton {
                            text: qsTr("Cancel")
                            onClicked: win.closeRecordDialog()
                        }
                        DialogButton {
                            visible: win.editingRecordIndex <= 0
                            text: qsTr("Save & add another")
                            onClicked: win.submitRecordDialog(true)
                        }
                        DialogButton {
                            text: qsTr("Save")
                            primary: true
                            onClicked: win.submitRecordDialog(false)
                        }
                    }
                }
            }
        }

        // --- Confirmations ---
        ConfirmDialog {
            visible: win.deleteTableConfirmVisible
            title: qsTr("Delete table \"%1\"?").arg(backend.currentTableName)
            message: qsTr("All attributes and records in this table will be removed. You can undo with Ctrl+Z.")
            buttons: [
                { text: qsTr("Delete"), mnemonic: "D", primary: true },
                { text: qsTr("Cancel"), mnemonic: "C" }
            ]
            onChosen: (index) => {
                win.deleteTableConfirmVisible = false
                if (index === 0)
                    backend.deleteTable(backend.currentTableId)
            }
            onDismissed: win.deleteTableConfirmVisible = false
        }

        ConfirmDialog {
            visible: win.deleteRecordConfirmVisible
            title: qsTr("Delete record #%1?").arg(backend.selectedRecordIndex)
            message: qsTr("This record will be removed from \"%1\". You can undo with Ctrl+Z.").arg(backend.currentTableName)
            buttons: [
                { text: qsTr("Delete"), mnemonic: "D", primary: true },
                { text: qsTr("Cancel"), mnemonic: "C" }
            ]
            onChosen: (index) => {
                win.deleteRecordConfirmVisible = false
                if (index === 0)
                    backend.deleteRecord(backend.selectedRecordIndex)
            }
            onDismissed: win.deleteRecordConfirmVisible = false
        }

        ConfirmDialog {
            visible: win.discardRecordConfirmVisible
            title: qsTr("Discard unsaved record changes?")
            message: qsTr("Your changes in the current form have not been saved.")
            buttons: [
                { text: qsTr("Discard"), mnemonic: "D", primary: true },
                { text: qsTr("Keep editing"), mnemonic: "K" }
            ]
            onChosen: (index) => {
                win.discardRecordConfirmVisible = false
                if (index === 0) {
                    win.recordDirty = false
                    win.recordDialogVisible = false
                }
            }
            onDismissed: win.discardRecordConfirmVisible = false
        }

        ConfirmDialog {
            visible: win.quitConfirmVisible
            title: qsTr("Quit with unsaved form changes?")
            message: qsTr("You have unsaved input in the open record form.")
            buttons: [
                { text: qsTr("Quit"), mnemonic: "Q", primary: true },
                { text: qsTr("Cancel"), mnemonic: "C" }
            ]
            onChosen: (index) => {
                win.quitConfirmVisible = false
                if (index === 0)
                    Qt.quit()
            }
            onDismissed: win.quitConfirmVisible = false
        }

        // --- Click-to-Zoom Image Preview Lightbox ---
        Rectangle {
            id: imagePreviewOverlay
            objectName: "imagePreviewOverlay"
            visible: win.imagePreviewVisible
            anchors.fill: parent
            color: "#e609090c"
            z: 100

            MouseArea {
                anchors.fill: parent
                onClicked: win.closeImagePreview()
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 12

                // Top Bar (Title, Dimensions, Zoom Controls, Close)
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 44
                    height: 44
                    radius: Math.min(6, theme.radius)
                    color: "#cc18181f"
                    border.width: 1
                    border.color: "#2e2e38"

                    // Prevent clicks on toolbar from closing the lightbox
                    MouseArea { anchors.fill: parent }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 14
                        anchors.rightMargin: 10
                        spacing: 10

                        Label {
                            Layout.fillWidth: true
                            text: win.imagePreviewTitle
                            color: "white"
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }

                        Label {
                            visible: win.imagePreviewMeta !== ""
                            text: win.imagePreviewMeta
                            color: "#9a9aa4"
                            font.pixelSize: 12
                            font.family: "monospace"
                        }

                        Rectangle {
                            width: 1
                            height: 18
                            color: "#32323c"
                        }

                        // Zoom Out (-)
                        Rectangle {
                            implicitWidth: 28; implicitHeight: 28
                            width: 28; height: 28
                            radius: Math.min(4, theme.radius)
                            color: zoomOutHover.hovered ? "#2c2c36" : "#202028"
                            border.width: 1
                            border.color: "#34343e"
                            opacity: win.imagePreviewZoom > 1.0 ? 1.0 : 0.45

                            Label {
                                anchors.centerIn: parent
                                text: "−"
                                color: "white"
                                font.pixelSize: 14
                            }
                            HoverHandler { id: zoomOutHover; cursorShape: Qt.PointingHandCursor }
                            TapHandler { onTapped: win.adjustImagePreviewZoom(-0.5) }
                        }

                        // Zoom % / Reset to Fit
                        Rectangle {
                            implicitWidth: zoomPctLbl.implicitWidth + 16
                            implicitHeight: 28
                            width: zoomPctLbl.implicitWidth + 16
                            height: 28
                            radius: Math.min(4, theme.radius)
                            color: zoomResetHover.hovered ? "#2c2c36" : "#202028"
                            border.width: 1
                            border.color: "#34343e"

                            Label {
                                id: zoomPctLbl
                                anchors.centerIn: parent
                                text: Math.round(win.imagePreviewZoom * 100) + "%"
                                color: "#dcdce4"
                                font.pixelSize: 12
                                font.family: "monospace"
                            }
                            HoverHandler { id: zoomResetHover; cursorShape: Qt.PointingHandCursor }
                            TapHandler { onTapped: win.imagePreviewZoom = 1.0 }
                        }

                        // Zoom In (+)
                        Rectangle {
                            implicitWidth: 28; implicitHeight: 28
                            width: 28; height: 28
                            radius: Math.min(4, theme.radius)
                            color: zoomInHover.hovered ? "#2c2c36" : "#202028"
                            border.width: 1
                            border.color: "#34343e"
                            opacity: win.imagePreviewZoom < 4.0 ? 1.0 : 0.45

                            Label {
                                anchors.centerIn: parent
                                text: "+"
                                color: "white"
                                font.pixelSize: 14
                            }
                            HoverHandler { id: zoomInHover; cursorShape: Qt.PointingHandCursor }
                            TapHandler { onTapped: win.adjustImagePreviewZoom(0.5) }
                        }

                        // Close Button
                        Rectangle {
                            objectName: "imagePreviewCloseBtn"
                            implicitWidth: 28; implicitHeight: 28
                            width: 28; height: 28
                            radius: Math.min(4, theme.radius)
                            color: closePrevHover.hovered ? "#3f181c" : "#202028"
                            border.width: 1
                            border.color: closePrevHover.hovered ? "#dc2626" : "#34343e"

                            Label {
                                anchors.centerIn: parent
                                text: "×"
                                color: closePrevHover.hovered ? "#fca5a5" : "#d4d4dc"
                                font.pixelSize: 16
                            }
                            HoverHandler { id: closePrevHover; cursorShape: Qt.PointingHandCursor }
                            TapHandler { onTapped: win.closeImagePreview() }
                        }
                    }
                }

                // Image Viewport (Click to toggle zoom, Wheel to zoom, Drag to pan)
                Flickable {
                    id: previewFlick
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    interactive: win.imagePreviewZoom > 1.01

                    readonly property real maxFitW: Math.max(120, width - 24)
                    readonly property real maxFitH: Math.max(120, height - 24)
                    readonly property real natW: previewImg.implicitWidth > 0 ? previewImg.implicitWidth : maxFitW
                    readonly property real natH: previewImg.implicitHeight > 0 ? previewImg.implicitHeight : maxFitH
                    readonly property real fitScale: Math.min(1.0, maxFitW / natW, maxFitH / natH)
                    readonly property real fitW: natW * fitScale
                    readonly property real fitH: natH * fitScale

                    contentWidth: Math.max(width, fitW * win.imagePreviewZoom)
                    contentHeight: Math.max(height, fitH * win.imagePreviewZoom)

                    WheelHandler {
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                        onWheel: (event) => {
                            if (event.angleDelta.y > 0)
                                win.adjustImagePreviewZoom(0.25)
                            else if (event.angleDelta.y < 0)
                                win.adjustImagePreviewZoom(-0.25)
                            event.accepted = true
                        }
                    }

                    Item {
                        width: previewFlick.contentWidth
                        height: previewFlick.contentHeight

                        // Clicking empty area around the image closes the lightbox
                        MouseArea {
                            anchors.fill: parent
                            onClicked: win.closeImagePreview()
                        }

                        Image {
                            id: previewImg
                            objectName: "imagePreviewImage"
                            anchors.centerIn: parent
                            width: previewFlick.fitW * win.imagePreviewZoom
                            height: previewFlick.fitH * win.imagePreviewZoom
                            source: win.imagePreviewUrl
                            fillMode: Image.PreserveAspectFit
                            smooth: true
                            mipmap: true
                            asynchronous: false

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: win.imagePreviewZoom > 1.01 ? Qt.OpenHandCursor : Qt.PointingHandCursor
                                onClicked: (mouse) => {
                                    mouse.accepted = true
                                    win.imagePreviewZoom = win.imagePreviewZoom > 1.01 ? 1.0 : 2.0
                                }
                            }
                        }
                    }
                }

                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("Click image to zoom · Scroll to adjust · Esc to close")
                    color: "#8a8a94"
                    font.pixelSize: 11
                }
            }
        }

        // --- Keyboard Shortcuts Overlay (?) ---
        Rectangle {
            visible: win.helpVisible
            anchors.fill: parent
            color: "#cc000000"

            MouseArea {
                anchors.fill: parent
                onClicked: win.helpVisible = false
            }

            DialogPanel {
                anchors.centerIn: parent
                width: helpCol.width + 56
                height: helpCol.height + 48

                Column {
                    id: helpCol
                    anchors.centerIn: parent
                    spacing: 8

                    Label {
                        text: qsTr("Keyboard shortcuts")
                        color: "white"
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                        bottomPadding: 8
                    }

                    Repeater {
                        model: [
                            { keys: "Space", action: qsTr("Add a new record (or table)") },
                            { keys: "Enter", action: qsTr("Edit the selected record") },
                            { keys: "Up / Down", action: qsTr("Select previous / next record") },
                            { keys: "X / Delete", action: qsTr("Delete the selected record") },
                            { keys: "N", action: qsTr("Create a new table") },
                            { keys: "R", action: qsTr("Rename or edit current table") },
                            { keys: "F", action: qsTr("Manage table attributes") },
                            { keys: "1–9", action: qsTr("Switch to table 1–9") },
                            { keys: "/", action: qsTr("Focus search box") },
                            { keys: "I", action: qsTr("Import CSV into a table") },
                            { keys: "E", action: qsTr("Export current table to CSV") },
                            { keys: "O", action: qsTr("Open a SQLite database file") },
                            { keys: "Ctrl+Z", action: qsTr("Undo (Ctrl+Shift+Z redo)") },
                            { keys: "?", action: qsTr("Show keyboard shortcuts") },
                            { keys: "Q", action: qsTr("Quit") }
                        ]

                        Row {
                            required property var modelData
                            spacing: 18

                            Label {
                                width: 110
                                text: modelData.keys
                                color: theme.accent
                                font.pixelSize: 13
                                font.family: "monospace"
                                font.weight: Font.DemiBold
                            }
                            Label {
                                text: modelData.action
                                color: "#dcdce4"
                                font.pixelSize: 13
                            }
                        }
                    }
                }
            }
        }
    }
}
