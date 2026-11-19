#pragma once

#include <QPointer>
#include <QQuickImageProvider>

class Backend;

// Serves embedded SQLite image blobs to QML via image://aikku/<gen>/<id>.
class BlobImageProvider : public QQuickImageProvider {
public:
    explicit BlobImageProvider(Backend *backend);

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    QPointer<Backend> m_backend;
};
