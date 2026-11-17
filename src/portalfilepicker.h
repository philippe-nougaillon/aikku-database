#pragma once

#include <QVariantMap>

#include "filepicker.h"

class QDBusPendingCallWatcher;

// Portal file picker over D-Bus (adapted from Omacut, MIT, David Heinemeier Hansson).
class PortalFilePicker : public FilePicker {
    Q_OBJECT

public:
    explicit PortalFilePicker(QObject *parent = nullptr);

    void openCsv() override;
    void exportCsv(const QUrl &suggestedUrl) override;
    void openDatabase() override;

private slots:
    void handleResponse(uint response, const QVariantMap &results);

private:
    enum class Action {
        None,
        OpenCsv,
        ExportCsv,
        OpenDatabase
    };

    void requestFile(const QString &method, const QString &title,
                     QVariantMap options, Action action);
    bool connectToRequestPath(const QString &path);
    void clearPending();
    void closePendingRequest();

    QString m_pendingPath;
    Action m_pendingAction = Action::None;
    int m_pendingGeneration = 0;
    QDBusPendingCallWatcher *m_pendingWatcher = nullptr;
};
