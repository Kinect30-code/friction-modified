/*
 * PSD import module for friction-modified.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "psdmapper.h"

#include "psdreader.h"

#include "core/Private/document.h"
#include "core/canvas.h"
#include "core/Boxes/imagebox.h"
#include "core/Boxes/containerbox.h"
#include "core/Boxes/boundingbox.h"
#include "core/appsupport.h"

#include <QBuffer>
#include <QDebug>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>

namespace {

// Saves a QImage to a temp PNG and returns the file path (empty on failure).
QString saveLayerImage(const QImage& image, const QString& dir, int index)
{
    if (image.isNull()) { return QString(); }
    const QString path = QStringLiteral("%1/layer_%2.png").arg(dir).arg(index, 4, 10, QLatin1Char('0'));
    if (image.save(path, "PNG")) { return path; }
    return QString();
}

QString tempDirFor(const QFileInfo& fileInfo)
{
    const QByteArray key = fileInfo.absoluteFilePath().toUtf8();
    const QString hash = QString::fromUtf8(
                QCryptographicHash::hash(key, QCryptographicHash::Sha1).toHex());
    const QString root = QDir(AppSupport::getAppConfigPath()).filePath("PsdImports");
    QDir().mkpath(root);
    const QString dir = QDir(root).filePath(hash);
    QDir().mkpath(dir);
    return dir;
}

SkBlendMode psdBlendMode(const QString& mode)
{
    if (mode == QStringLiteral("mul ")) { return SkBlendMode::kMultiply; }
    if (mode == QStringLiteral("scrn")) { return SkBlendMode::kScreen; }
    if (mode == QStringLiteral("over")) { return SkBlendMode::kOverlay; }
    if (mode == QStringLiteral("dark")) { return SkBlendMode::kDarken; }
    if (mode == QStringLiteral("lite")) { return SkBlendMode::kLighten; }
    if (mode == QStringLiteral("dodg")) { return SkBlendMode::kColorDodge; }
    if (mode == QStringLiteral("burn")) { return SkBlendMode::kColorBurn; }
    if (mode == QStringLiteral("hLit")) { return SkBlendMode::kHardLight; }
    if (mode == QStringLiteral("sLit")) { return SkBlendMode::kSoftLight; }
    if (mode == QStringLiteral("diff")) { return SkBlendMode::kDifference; }
    if (mode == QStringLiteral("smud")) { return SkBlendMode::kExclusion; }
    if (mode == QStringLiteral("hue ")) { return SkBlendMode::kHue; }
    if (mode == QStringLiteral("sat ")) { return SkBlendMode::kSaturation; }
    if (mode == QStringLiteral("colr")) { return SkBlendMode::kColor; }
    if (mode == QStringLiteral("lum ")) { return SkBlendMode::kLuminosity; }
    if (mode == QStringLiteral("add ")) { return SkBlendMode::kPlus; }
    return SkBlendMode::kSrcOver;
}

} // namespace

namespace PsdModule {

qsptr<BoundingBox> importPsdFile(const QFileInfo& fileInfo,
                                 Canvas* const scene)
{
    if (!scene || !Document::sInstance) { return nullptr; }

    QFile file(fileInfo.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "PSD import: cannot open" << fileInfo.absoluteFilePath();
        return nullptr;
    }

    PsdDocument doc;
    QString error;
    if (!readPsd(&file, &doc, &error)) {
        qWarning() << "PSD import failed:" << error;
        return nullptr;
    }
    file.close();

    if (doc.width <= 0 || doc.height <= 0) {
        qWarning() << "PSD import failed: invalid dimensions";
        return nullptr;
    }

    const QString baseName = fileInfo.completeBaseName().trimmed().isEmpty()
            ? QStringLiteral("PSD Comp")
            : fileInfo.completeBaseName().trimmed();

    auto* newScene = Document::sInstance->createNewScene(false);
    if (!newScene) { return nullptr; }
    newScene->prp_setName(baseName);
    newScene->setCanvasSize(doc.width, doc.height);
    newScene->setFps(scene->getFps());
    newScene->setFrameRange(scene->getFrameRange(), false);
    newScene->planCenterPivotPosition();

    const QString dir = tempDirFor(fileInfo);

    // PSD layers are stored top-most first; friction addContained appends on
    // top, so iterate in document order (top layer added last stays on top).
    for (int i = 0; i < doc.layers.size(); ++i) {
        const auto& layer = doc.layers.at(i);
        if (!layer.visible) { continue; }
        if (layer.image.isNull()) { continue; }

        const QString imgPath = saveLayerImage(layer.image, dir, i);
        if (imgPath.isEmpty()) { continue; }

        auto box = enve::make_shared<ImageBox>(imgPath);
        const QString name = layer.name.trimmed().isEmpty()
                ? QStringLiteral("Layer %1").arg(i + 1)
                : layer.name;
        box->prp_setName(name);

        // Place at the layer's position within the composition.
        box->setAbsolutePos(QPointF(layer.left, layer.top));
        box->planCenterPivotPosition();

        if (layer.blendMode != QStringLiteral("norm")) {
            box->setBlendModeSk(psdBlendMode(layer.blendMode));
        }
        // PSD opacity is 0..255.
        const qreal opacity = qBound<qreal>(0.0, layer.opacity / 255.0 * 100.0, 100.0);
        box->setOpacity(opacity);

        newScene->addContained(box);
    }

    auto link = newScene->createLink(false);
    link->setPivotRelPos(QPointF(doc.width / 2.0, doc.height / 2.0));
    link->prp_setName(baseName);
    return link;
}

} // namespace PsdModule
