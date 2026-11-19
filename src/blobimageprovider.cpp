#include "blobimageprovider.h"

#include <QThread>

#include "backend.h"

BlobImageProvider::BlobImageProvider(Backend *backend)
    : QQuickImageProvider(QQuickImageProvider::Image),
      m_backend(backend) {}

QImage BlobImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize) {
    if (m_backend.isNull())
        return {};

    // Accept both "<gen>/<blobId>" and "<blobId>".
    const int slash = id.lastIndexOf(QLatin1Char('/'));
    const QString idPart = (slash >= 0) ? id.mid(slash + 1) : id;
    bool ok = false;
    const int blobId = idPart.toInt(&ok);
    if (!ok || blobId <= 0)
        return {};

    QImage img = (QThread::currentThread() == m_backend->thread())
                     ? m_backend->loadBlobImage(blobId)
                     : Backend::loadBlobImageFromFile(m_backend->threadSafeDatabasePath(), blobId);

    if (img.isNull())
        return {};

    if (size)
        *size = img.size();

    if (requestedSize.isValid() && (requestedSize.width() > 0 || requestedSize.height() > 0)) {
        const int targetW = requestedSize.width() > 0 ? requestedSize.width() : img.width();
        const int targetH = requestedSize.height() > 0 ? requestedSize.height() : img.height();
        if (img.width() > targetW || img.height() > targetH) {
            img = img.scaled(targetW, targetH, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        }
    }
    return img;
}
