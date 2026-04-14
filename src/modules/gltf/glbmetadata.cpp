#include "glbmetadata.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

constexpr quint32 kGlbMagic = 0x46546C67u;
constexpr quint32 kJsonChunkType = 0x4E4F534Au;

quint32 readLe32(const uchar* const data) {
    return quint32(data[0]) |
           (quint32(data[1]) << 8) |
           (quint32(data[2]) << 16) |
           (quint32(data[3]) << 24);
}

QByteArray readJsonChunk(const QByteArray& bytes, QString* const error) {
    if(bytes.size() < 20) {
        if(error) {
            *error = QStringLiteral("GLB file is too small.");
        }
        return {};
    }

    const auto* const raw = reinterpret_cast<const uchar*>(bytes.constData());
    const quint32 magic = readLe32(raw);
    const quint32 version = readLe32(raw + 4);
    const quint32 length = readLe32(raw + 8);
    if(magic != kGlbMagic) {
        if(error) {
            *error = QStringLiteral("Invalid GLB magic header.");
        }
        return {};
    }
    if(version < 2) {
        if(error) {
            *error = QStringLiteral("Only GLB version 2 is supported.");
        }
        return {};
    }
    if(length > quint32(bytes.size())) {
        if(error) {
            *error = QStringLiteral("GLB length header exceeds file size.");
        }
        return {};
    }

    int offset = 12;
    while(offset + 8 <= bytes.size()) {
        const quint32 chunkLength = readLe32(raw + offset);
        const quint32 chunkType = readLe32(raw + offset + 4);
        offset += 8;
        if(chunkLength > quint32(bytes.size() - offset)) {
            if(error) {
                *error = QStringLiteral("GLB chunk exceeds file bounds.");
            }
            return {};
        }
        if(chunkType == kJsonChunkType) {
            QByteArray json = bytes.mid(offset, int(chunkLength));
            while(!json.isEmpty() &&
                  (json.endsWith('\0') || json.endsWith(' ') ||
                   json.endsWith('\n') || json.endsWith('\r') ||
                   json.endsWith('\t'))) {
                json.chop(1);
            }
            return json;
        }
        offset += int(chunkLength);
    }

    if(error) {
        *error = QStringLiteral("No JSON chunk found in GLB container.");
    }
    return {};
}

GltfMetadata parseMetadata(const QByteArray& jsonBytes,
                           const QString& sourcePath,
                           const bool binaryContainer,
                           QString* const error) {
    GltfMetadata result;
    result.fSourcePath = sourcePath;
    result.fBinaryContainer = binaryContainer;

    QJsonParseError parseError;
    const auto json = QJsonDocument::fromJson(jsonBytes, &parseError);
    if(parseError.error != QJsonParseError::NoError || !json.isObject()) {
        if(error) {
            *error = parseError.errorString().isEmpty() ?
                         QStringLiteral("Invalid glTF JSON document.") :
                         parseError.errorString();
        }
        result.fError = error ? *error : QStringLiteral("Invalid glTF JSON document.");
        return result;
    }

    const auto root = json.object();
    const auto asset = root.value(QStringLiteral("asset")).toObject();
    result.fVersion = asset.value(QStringLiteral("version")).toString();
    result.fGenerator = asset.value(QStringLiteral("generator")).toString();
    result.fSceneCount = root.value(QStringLiteral("scenes")).toArray().size();
    result.fNodeCount = root.value(QStringLiteral("nodes")).toArray().size();
    result.fMeshCount = root.value(QStringLiteral("meshes")).toArray().size();
    result.fMaterialCount = root.value(QStringLiteral("materials")).toArray().size();

    const auto animations = root.value(QStringLiteral("animations")).toArray();
    result.fAnimations.reserve(animations.size());
    for(int i = 0; i < animations.size(); ++i) {
        const auto animation = animations.at(i).toObject();
        GltfAnimationInfo info;
        info.fName = animation.value(QStringLiteral("name")).toString();
        if(info.fName.isEmpty()) {
            info.fName = QStringLiteral("Animation %1").arg(i + 1);
        }
        info.fChannelCount = animation.value(QStringLiteral("channels")).toArray().size();
        result.fAnimations.append(info);
    }

    result.fValid = true;
    return result;
}

}

QStringList GltfMetadata::animationNames() const {
    QStringList result;
    result.reserve(fAnimations.size());
    for(const auto& animation : fAnimations) {
        result << animation.fName;
    }
    return result;
}

GltfMetadata GltfMetadataReader::readFromFile(const QString &path) {
    GltfMetadata result;
    result.fSourcePath = path;

    QFile file(path);
    if(!file.open(QIODevice::ReadOnly)) {
        result.fError = QStringLiteral("Unable to open file.");
        return result;
    }

    const QByteArray bytes = file.readAll();
    const QString suffix = QFileInfo(path).suffix().toLower();

    QString error;
    QByteArray jsonBytes;
    bool isBinary = false;
    if(suffix == QStringLiteral("glb")) {
        isBinary = true;
        jsonBytes = readJsonChunk(bytes, &error);
    } else {
        jsonBytes = bytes;
    }

    if(jsonBytes.isEmpty() && !error.isEmpty()) {
        result.fBinaryContainer = isBinary;
        result.fError = error;
        return result;
    }

    result = parseMetadata(jsonBytes, path, isBinary, &error);
    if(!result.fValid && result.fError.isEmpty()) {
        result.fError = error;
    }
    return result;
}
