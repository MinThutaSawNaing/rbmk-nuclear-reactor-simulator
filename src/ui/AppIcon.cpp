#include "ui/AppIcon.h"

#include <QDebug>
#include <QFile>
#include <QSize>
#include <QString>

namespace {

//! Icon sizes shipped in the Qt resource (must match tools/make_icons.py).
constexpr int kLogoPixels[] = { 16, 24, 32, 48, 64, 128, 256, 512 };

QString logoResource(int pixels)
{
    return QStringLiteral(":/icons/rbmk-logo-%1.png").arg(pixels);
}

//! Smallest embedded logo that is at least \a pixels wide, so Qt never has to
//! upscale the artwork.
int sourceSizeFor(int pixels)
{
    int smallest = kLogoPixels[sizeof(kLogoPixels) / sizeof(kLogoPixels[0]) - 1];
    for (int candidate : kLogoPixels) {
        if (candidate >= pixels)
            return candidate;
    }
    return smallest;
}

} // namespace

QIcon appicon::windowIcon()
{
    static const QIcon icon = [] {
        QIcon assembled;
        for (int pixels : kLogoPixels) {
            const QString path = logoResource(pixels);
            if (!QFile::exists(path))
                qWarning() << "Application icon resource missing:" << path;
            else
                assembled.addFile(path, QSize(pixels, pixels));
        }
        return assembled;
    }();
    return icon;
}

QPixmap appicon::logo(int side, qreal devicePixelRatio)
{
    const int requested = qMax(1, qRound(side * qMax<qreal>(1.0, devicePixelRatio)));
    QPixmap pixmap(logoResource(sourceSizeFor(requested)));
    if (pixmap.isNull()) {
        qWarning() << "Application logo resource missing for" << requested << "pixels";
        return pixmap;
    }

    if (pixmap.width() != requested)
        pixmap = pixmap.scaled(requested, requested, Qt::KeepAspectRatio,
                               Qt::SmoothTransformation);
    pixmap.setDevicePixelRatio(qMax<qreal>(1.0, devicePixelRatio));
    return pixmap;
}
