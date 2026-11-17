#pragma once

#include <QObject>
#include <QUrl>

// Abstract file picker interface (pattern from Omacut, MIT, David Heinemeier Hansson),
// allowing production code to talk to xdg-desktop-portal over D-Bus while tests
// inject a fake picker.
class FilePicker : public QObject {
    Q_OBJECT

public:
    explicit FilePicker(QObject *parent = nullptr) : QObject(parent) {}
    ~FilePicker() override = default;

    virtual void openCsv() = 0;
    virtual void exportCsv(const QUrl &suggestedUrl) = 0;
    virtual void openDatabase() = 0;

signals:
    void csvOpenSelected(const QUrl &url);
    void csvExportSelected(const QUrl &url);
    void databaseOpenSelected(const QUrl &url);
    void failed(const QString &message);
};
