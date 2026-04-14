#ifndef GLTFSCENE_H
#define GLTFSCENE_H

#include <QString>
#include <QImage>
#include <QVector2D>
#include <QVector>
#include <QVector3D>
#include <QVector4D>

#include <memory>

struct GltfSceneDataPrivate;

struct GltfMeshVertex {
    QVector3D fPosition;
    QVector3D fNormal;
    QVector2D fTexCoord0 = QVector2D(0.f, 0.f);
    QVector2D fTexCoord1 = QVector2D(0.f, 0.f);
};

struct GltfMaterialInfo {
    QVector4D fBaseColorFactor = QVector4D(1.f, 1.f, 1.f, 1.f);
    int fBaseColorTexture = -1;
    int fTexCoordSet = 0;
    QVector2D fTexCoordOffset = QVector2D(0.f, 0.f);
    QVector2D fTexCoordScale = QVector2D(1.f, 1.f);
    float fTexCoordRotation = 0.f;
    float fAlphaCutoff = 0.5f;
    bool fAlphaBlend = false;
    bool fAlphaMask = false;
};

struct GltfSceneBatch {
    int fVertexOffset = 0;
    int fVertexCount = 0;
    GltfMaterialInfo fMaterial;
};

struct GltfSceneFrame {
    bool fValid = false;
    QString fError;
    QVector<GltfMeshVertex> fVertices;
    QVector<GltfSceneBatch> fBatches;
    QVector<QImage> fTextures;
    QVector3D fBoundsMin = QVector3D(0.f, 0.f, 0.f);
    QVector3D fBoundsMax = QVector3D(0.f, 0.f, 0.f);
    QVector3D fReferenceBoundsMin = QVector3D(0.f, 0.f, 0.f);
    QVector3D fReferenceBoundsMax = QVector3D(0.f, 0.f, 0.f);
    int fTriangleCount = 0;

    bool empty() const { return fVertices.isEmpty() || fBatches.isEmpty(); }
};

struct GltfAnimationClipInfo {
    QString fName;
    float fDurationSeconds = 0.f;
};

struct GltfAnimationLayer {
    int fClipIndex = -1;
    float fTimeSeconds = 0.f;
    float fWeight = 1.f;
};

struct GltfSceneData {
    bool fValid = false;
    QString fError;
    QVector<GltfAnimationClipInfo> fAnimations;
    QVector3D fBoundsMin = QVector3D(0.f, 0.f, 0.f);
    QVector3D fBoundsMax = QVector3D(0.f, 0.f, 0.f);
    int fTriangleCount = 0;

    std::shared_ptr<GltfSceneDataPrivate> fData;

    bool empty() const { return !fValid || fTriangleCount <= 0 || !fData; }
};

namespace GltfSceneReader {

GltfSceneData readFromFile(const QString& path);
GltfSceneFrame sampleFrame(const GltfSceneData& scene,
                           int clipIndex,
                           float timeSeconds);
GltfSceneFrame sampleFrame(const GltfSceneData& scene,
                           const QVector<GltfAnimationLayer>& layers);

}

#endif // GLTFSCENE_H
