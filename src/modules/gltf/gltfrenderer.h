#ifndef GLTFRENDERER_H
#define GLTFRENDERER_H

#include <QSize>

#include "../../core/skia/skiaincludes.h"

#include "gltfscene.h"

namespace GltfRenderer {

struct RenderTransform {
    QVector3D fTranslation = QVector3D(0.f, 0.f, 0.f);
    QVector3D fRotationEuler = QVector3D(0.f, 0.f, 0.f);
    QVector3D fScale = QVector3D(1.f, 1.f, 1.f);
};

sk_sp<SkImage> renderPreview(const GltfSceneFrame& scene,
                             const QString& renderMode,
                             const RenderTransform& transform,
                             const QSize& size,
                             QString* errorMessage);

}

#endif // GLTFRENDERER_H
