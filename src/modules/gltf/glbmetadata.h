#ifndef GLBMETADATA_H
#define GLBMETADATA_H

#include <QList>
#include <QString>

struct GltfAnimationInfo {
    QString fName;
    int fChannelCount = 0;
};

struct GltfMetadata {
    bool fValid = false;
    bool fBinaryContainer = false;
    QString fVersion;
    QString fGenerator;
    QString fSourcePath;
    QString fError;
    int fSceneCount = 0;
    int fNodeCount = 0;
    int fMeshCount = 0;
    int fMaterialCount = 0;
    QList<GltfAnimationInfo> fAnimations;

    QStringList animationNames() const;
};

namespace GltfMetadataReader {

GltfMetadata readFromFile(const QString& path);

}

#endif // GLBMETADATA_H
