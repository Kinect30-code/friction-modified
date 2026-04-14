#include "gltfscene.h"

#include <QByteArray>
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuaternion>
#include <QVector2D>
#include <QVector4D>

#include <algorithm>
#include <cstring>

#include "../../core/skia/skiaincludes.h"

enum class ChannelPath {
    translation,
    rotation,
    scale,
    weights
};

enum class Interpolation {
    step,
    linear
};

struct Mat4 {
    float m[16] = {
        1.f, 0.f, 0.f, 0.f,
        0.f, 1.f, 0.f, 0.f,
        0.f, 0.f, 1.f, 0.f,
        0.f, 0.f, 0.f, 1.f
    };
};

struct RawMeshVertex {
    QVector3D fPosition;
    QVector3D fNormal;
    QVector2D fTexCoord0 = QVector2D(0.f, 0.f);
    QVector2D fTexCoord1 = QVector2D(0.f, 0.f);
    QVector<QVector3D> fMorphPositionDeltas;
    QVector<QVector3D> fMorphNormalDeltas;
    int fJointIndices[4] = {0, 0, 0, 0};
    QVector4D fJointWeights = QVector4D(0.f, 0.f, 0.f, 0.f);
    bool fHasSkinning = false;
};

struct MeshPrimitive {
    QVector<RawMeshVertex> fVertices;
    int fMaterialIndex = -1;
};

struct MeshData {
    QVector<MeshPrimitive> fPrimitives;
    QVector<float> fDefaultWeights;
};

struct TextureData {
    int fImageIndex = -1;
};

struct MaterialData {
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

struct SkinData {
    QVector<int> fJointNodes;
    QVector<Mat4> fInverseBindMatrices;
};

struct NodeData {
    int fMesh = -1;
    int fSkin = -1;
    QVector<int> fChildren;
    QVector<float> fWeights;
    bool fHasMatrix = false;
    Mat4 fMatrix;
    QVector3D fTranslation = QVector3D(0.f, 0.f, 0.f);
    QQuaternion fRotation;
    QVector3D fScale = QVector3D(1.f, 1.f, 1.f);
};

struct AnimationSampler {
    ChannelPath fPath = ChannelPath::translation;
    Interpolation fInterpolation = Interpolation::linear;
    QVector<float> fInputs;
    QVector<QVector4D> fOutputs;
    QVector<QVector<float>> fWeightOutputs;
};

struct AnimationChannel {
    int fNode = -1;
    int fSampler = -1;
    ChannelPath fPath = ChannelPath::translation;
};

struct AnimationClip {
    QString fName;
    float fDurationSeconds = 0.f;
    QVector<AnimationSampler> fSamplers;
    QVector<AnimationChannel> fChannels;
};

struct GltfSceneDataPrivate {
    QVector<MeshData> fMeshes;
    QVector<QImage> fImages;
    QVector<TextureData> fTextures;
    QVector<MaterialData> fMaterials;
    QVector<SkinData> fSkins;
    QVector<NodeData> fNodes;
    QVector<int> fSceneRoots;
    QVector<AnimationClip> fClips;
};

namespace {

constexpr quint32 kGlbMagic = 0x46546C67u;
constexpr quint32 kJsonChunkType = 0x4E4F534Au;
constexpr quint32 kBinChunkType = 0x004E4942u;
constexpr int kMaxTriangles = 200000;

quint32 readLe32(const uchar* const data) {
    return quint32(data[0]) |
           (quint32(data[1]) << 8) |
           (quint32(data[2]) << 16) |
           (quint32(data[3]) << 24);
}

Mat4 multiply(const Mat4& a, const Mat4& b) {
    Mat4 result;
    for(int col = 0; col < 4; ++col) {
        for(int row = 0; row < 4; ++row) {
            float sum = 0.f;
            for(int k = 0; k < 4; ++k) {
                sum += a.m[k*4 + row] * b.m[col*4 + k];
            }
            result.m[col*4 + row] = sum;
        }
    }
    return result;
}

Mat4 add(const Mat4& a, const Mat4& b) {
    Mat4 result;
    for(int i = 0; i < 16; ++i) {
        result.m[i] = a.m[i] + b.m[i];
    }
    return result;
}

Mat4 multiplyScalar(const Mat4& matrix, const float scalar) {
    Mat4 result;
    for(int i = 0; i < 16; ++i) {
        result.m[i] = matrix.m[i] * scalar;
    }
    return result;
}

Mat4 zeroMatrix() {
    Mat4 result;
    for(float& value : result.m) {
        value = 0.f;
    }
    return result;
}

QVector3D transformPoint(const Mat4& matrix, const QVector3D& point) {
    const float x = matrix.m[0] * point.x() + matrix.m[4] * point.y() +
                    matrix.m[8] * point.z() + matrix.m[12];
    const float y = matrix.m[1] * point.x() + matrix.m[5] * point.y() +
                    matrix.m[9] * point.z() + matrix.m[13];
    const float z = matrix.m[2] * point.x() + matrix.m[6] * point.y() +
                    matrix.m[10] * point.z() + matrix.m[14];
    const float w = matrix.m[3] * point.x() + matrix.m[7] * point.y() +
                    matrix.m[11] * point.z() + matrix.m[15];
    if(!qFuzzyIsNull(w) && !qFuzzyCompare(w, 1.f)) {
        return QVector3D(x / w, y / w, z / w);
    }
    return QVector3D(x, y, z);
}

QVector3D transformVector(const Mat4& matrix, const QVector3D& vector) {
    return QVector3D(
                matrix.m[0] * vector.x() + matrix.m[4] * vector.y() + matrix.m[8] * vector.z(),
                matrix.m[1] * vector.x() + matrix.m[5] * vector.y() + matrix.m[9] * vector.z(),
                matrix.m[2] * vector.x() + matrix.m[6] * vector.y() + matrix.m[10] * vector.z()).normalized();
}

Mat4 translationMatrix(const QVector3D& translation) {
    Mat4 result;
    result.m[12] = translation.x();
    result.m[13] = translation.y();
    result.m[14] = translation.z();
    return result;
}

Mat4 scaleMatrix(const QVector3D& scale) {
    Mat4 result;
    result.m[0] = scale.x();
    result.m[5] = scale.y();
    result.m[10] = scale.z();
    return result;
}

Mat4 rotationMatrix(const QQuaternion& q) {
    Mat4 result;
    const QQuaternion n = q.normalized();
    const float x = n.x();
    const float y = n.y();
    const float z = n.z();
    const float w = n.scalar();

    result.m[0] = 1.f - 2.f * (y*y + z*z);
    result.m[1] = 2.f * (x*y + z*w);
    result.m[2] = 2.f * (x*z - y*w);

    result.m[4] = 2.f * (x*y - z*w);
    result.m[5] = 1.f - 2.f * (x*x + z*z);
    result.m[6] = 2.f * (y*z + x*w);

    result.m[8] = 2.f * (x*z + y*w);
    result.m[9] = 2.f * (y*z - x*w);
    result.m[10] = 1.f - 2.f * (x*x + y*y);
    return result;
}

Mat4 trsMatrix(const QVector3D& translation,
               const QQuaternion& rotation,
               const QVector3D& scale) {
    return multiply(translationMatrix(translation),
                    multiply(rotationMatrix(rotation), scaleMatrix(scale)));
}

bool parseGlb(const QByteArray& bytes,
              QJsonDocument* const json,
              QByteArray* const bin,
              QString* const error) {
    if(bytes.size() < 20) {
        *error = QStringLiteral("GLB file is too small.");
        return false;
    }
    const auto* const raw = reinterpret_cast<const uchar*>(bytes.constData());
    if(readLe32(raw) != kGlbMagic) {
        *error = QStringLiteral("Invalid GLB magic header.");
        return false;
    }
    if(readLe32(raw + 4) < 2) {
        *error = QStringLiteral("Only GLB version 2 is supported.");
        return false;
    }

    QByteArray jsonBytes;
    int offset = 12;
    while(offset + 8 <= bytes.size()) {
        const quint32 chunkLength = readLe32(raw + offset);
        const quint32 chunkType = readLe32(raw + offset + 4);
        offset += 8;
        if(chunkLength > quint32(bytes.size() - offset)) {
            *error = QStringLiteral("GLB chunk exceeds file bounds.");
            return false;
        }
        const QByteArray chunk = bytes.mid(offset, int(chunkLength));
        if(chunkType == kJsonChunkType) {
            jsonBytes = chunk;
            while(!jsonBytes.isEmpty() &&
                  (jsonBytes.endsWith('\0') || jsonBytes.endsWith(' ') ||
                   jsonBytes.endsWith('\n') || jsonBytes.endsWith('\r') ||
                   jsonBytes.endsWith('\t'))) {
                jsonBytes.chop(1);
            }
        } else if(chunkType == kBinChunkType) {
            *bin = chunk;
        }
        offset += int(chunkLength);
    }

    if(jsonBytes.isEmpty()) {
        *error = QStringLiteral("No JSON chunk found in GLB.");
        return false;
    }

    QJsonParseError parseError;
    *json = QJsonDocument::fromJson(jsonBytes, &parseError);
    if(parseError.error != QJsonParseError::NoError || !json->isObject()) {
        *error = parseError.errorString();
        return false;
    }
    return true;
}

bool loadJsonDocument(const QString& path,
                      QJsonDocument* const json,
                      QByteArray* const glbBin,
                      QString* const error) {
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("Unable to open file.");
        return false;
    }

    const QByteArray bytes = file.readAll();
    if(QFileInfo(path).suffix().toLower() == QStringLiteral("glb")) {
        return parseGlb(bytes, json, glbBin, error);
    }

    QJsonParseError parseError;
    *json = QJsonDocument::fromJson(bytes, &parseError);
    if(parseError.error != QJsonParseError::NoError || !json->isObject()) {
        *error = parseError.errorString();
        return false;
    }
    return true;
}

bool decodeDataUri(const QString& uri, QByteArray* const out) {
    const int commaIndex = uri.indexOf(',');
    if(commaIndex < 0) {
        return false;
    }
    const QString meta = uri.left(commaIndex);
    const QByteArray payload = uri.mid(commaIndex + 1).toLatin1();
    *out = meta.contains(QStringLiteral(";base64"), Qt::CaseInsensitive) ?
                QByteArray::fromBase64(payload) :
                QByteArray::fromPercentEncoding(payload);
    return true;
}

bool loadBuffers(const QJsonObject& root,
                 const QString& sourcePath,
                 const QByteArray& glbBin,
                 QVector<QByteArray>* const buffers,
                 QString* const error) {
    const auto bufferArray = root.value(QStringLiteral("buffers")).toArray();
    const QDir baseDir = QFileInfo(sourcePath).dir();

    buffers->clear();
    buffers->reserve(bufferArray.size());
    for(int i = 0; i < bufferArray.size(); ++i) {
        const auto bufferObj = bufferArray.at(i).toObject();
        const QString uri = bufferObj.value(QStringLiteral("uri")).toString();
        QByteArray bytes;
        if(uri.isEmpty()) {
            bytes = glbBin;
        } else if(uri.startsWith(QStringLiteral("data:"), Qt::CaseInsensitive)) {
            if(!decodeDataUri(uri, &bytes)) {
                *error = QStringLiteral("Failed to decode buffer data URI.");
                return false;
            }
        } else {
            QFile file(baseDir.filePath(uri));
            if(!file.open(QIODevice::ReadOnly)) {
                *error = QStringLiteral("Unable to open external buffer %1.").arg(uri);
                return false;
            }
            bytes = file.readAll();
        }

        const int byteLength = bufferObj.value(QStringLiteral("byteLength")).toInt(bytes.size());
        if(byteLength > bytes.size()) {
            *error = QStringLiteral("Buffer %1 shorter than declared byteLength.").arg(i);
            return false;
        }
        buffers->append(bytes.left(byteLength));
    }
    return true;
}

int componentSize(const int componentType) {
    switch(componentType) {
    case 5120:
    case 5121:
        return 1;
    case 5122:
    case 5123:
        return 2;
    case 5125:
    case 5126:
        return 4;
    default:
        return 0;
    }
}

int numComponents(const QString& type) {
    if(type == QStringLiteral("SCALAR")) return 1;
    if(type == QStringLiteral("VEC2")) return 2;
    if(type == QStringLiteral("VEC3")) return 3;
    if(type == QStringLiteral("VEC4")) return 4;
    if(type == QStringLiteral("MAT4")) return 16;
    return 0;
}

bool accessorByteInfo(const QJsonObject& root,
                      const QVector<QByteArray>& buffers,
                      const int accessorIndex,
                      const char** data,
                      int* stride,
                      int* count,
                      int* componentType,
                      bool* normalized,
                      QString* const type,
                      QString* const error) {
    const auto accessorArray = root.value(QStringLiteral("accessors")).toArray();
    const auto viewArray = root.value(QStringLiteral("bufferViews")).toArray();
    if(accessorIndex < 0 || accessorIndex >= accessorArray.size()) {
        *error = QStringLiteral("Accessor index out of range.");
        return false;
    }

    const auto accessor = accessorArray.at(accessorIndex).toObject();
    const int viewIndex = accessor.value(QStringLiteral("bufferView")).toInt(-1);
    if(viewIndex < 0 || viewIndex >= viewArray.size()) {
        *error = QStringLiteral("Sparse or missing bufferView is not supported.");
        return false;
    }

    const auto view = viewArray.at(viewIndex).toObject();
    const int bufferIndex = view.value(QStringLiteral("buffer")).toInt(-1);
    if(bufferIndex < 0 || bufferIndex >= buffers.size()) {
        *error = QStringLiteral("Buffer index out of range.");
        return false;
    }

    *count = accessor.value(QStringLiteral("count")).toInt();
    *componentType = accessor.value(QStringLiteral("componentType")).toInt();
    if(normalized) {
        *normalized = accessor.value(QStringLiteral("normalized")).toBool(false);
    }
    *type = accessor.value(QStringLiteral("type")).toString();

    const int accessorOffset = accessor.value(QStringLiteral("byteOffset")).toInt();
    const int viewOffset = view.value(QStringLiteral("byteOffset")).toInt();
    const int byteStride = view.value(QStringLiteral("byteStride")).toInt();
    const int elemSize = componentSize(*componentType) * numComponents(*type);
    *stride = byteStride > 0 ? byteStride : elemSize;
    if(elemSize <= 0 || *count < 0) {
        *error = QStringLiteral("Unsupported accessor layout.");
        return false;
    }

    const QByteArray& buffer = buffers.at(bufferIndex);
    const int start = accessorOffset + viewOffset;
    const int needed = start + (*count > 0 ? ((*count - 1) * (*stride) + elemSize) : 0);
    if(start < 0 || needed > buffer.size()) {
        *error = QStringLiteral("Accessor exceeds buffer bounds.");
        return false;
    }

    *data = buffer.constData() + start;
    return true;
}

bool readFloatAccessor(const QJsonObject& root,
                       const QVector<QByteArray>& buffers,
                       const int accessorIndex,
                       QVector<float>* const out,
                       QString* const error) {
    const char* data = nullptr;
    int stride = 0;
    int count = 0;
    int compType = 0;
    QString type;
    if(!accessorByteInfo(root, buffers, accessorIndex,
                         &data, &stride, &count, &compType, nullptr, &type, error)) {
        return false;
    }
    if(compType != 5126 || type != QStringLiteral("SCALAR")) {
        *error = QStringLiteral("Only float scalar accessors are supported.");
        return false;
    }

    out->resize(count);
    for(int i = 0; i < count; ++i) {
        float value = 0.f;
        std::memcpy(&value, data + i*stride, sizeof(float));
        (*out)[i] = value;
    }
    return true;
}

bool readWeightKeyframesAccessor(const QJsonObject& root,
                                 const QVector<QByteArray>& buffers,
                                 const int accessorIndex,
                                 const int targetCount,
                                 QVector<QVector<float>>* const out,
                                 QString* const error) {
    QVector<float> flatValues;
    if(!readFloatAccessor(root, buffers, accessorIndex, &flatValues, error)) {
        return false;
    }
    if(targetCount <= 0) {
        *error = QStringLiteral("Invalid morph target count.");
        return false;
    }
    if(flatValues.size() % targetCount != 0) {
        *error = QStringLiteral("Weight animation output size does not match morph target count.");
        return false;
    }
    const int keyframeCount = flatValues.size() / targetCount;
    out->resize(keyframeCount);
    for(int keyIndex = 0; keyIndex < keyframeCount; ++keyIndex) {
        auto& weights = (*out)[keyIndex];
        weights.resize(targetCount);
        float sum = 0.f;
        for(int weightIndex = 0; weightIndex < targetCount; ++weightIndex) {
            const float value = flatValues.at(keyIndex*targetCount + weightIndex);
            weights[weightIndex] = value;
            sum += value;
        }
        Q_UNUSED(sum)
    }
    return true;
}

bool readVecAccessor(const QJsonObject& root,
                     const QVector<QByteArray>& buffers,
                     const int accessorIndex,
                     QVector<QVector4D>* const out,
                     QString* const error) {
    const char* data = nullptr;
    int stride = 0;
    int count = 0;
    int compType = 0;
    QString type;
    if(!accessorByteInfo(root, buffers, accessorIndex,
                         &data, &stride, &count, &compType, nullptr, &type, error)) {
        return false;
    }
    if(compType != 5126) {
        *error = QStringLiteral("Only float vector accessors are supported.");
        return false;
    }

    const int comps = numComponents(type);
    if(comps < 3 || comps > 4) {
        *error = QStringLiteral("Only VEC3/VEC4 accessors are supported.");
        return false;
    }

    out->resize(count);
    for(int i = 0; i < count; ++i) {
        float values[4] = {0.f, 0.f, 0.f, comps == 4 ? 1.f : 0.f};
        std::memcpy(values, data + i*stride, sizeof(float) * comps);
        if(comps == 3) {
            values[3] = 0.f;
        }
        (*out)[i] = QVector4D(values[0], values[1], values[2], values[3]);
    }
    return true;
}

float normalizedComponentValue(const char* elem,
                               int componentType,
                               bool normalized);

bool readVec2Accessor(const QJsonObject& root,
                      const QVector<QByteArray>& buffers,
                      const int accessorIndex,
                      QVector<QVector2D>* const out,
                      QString* const error) {
    const char* data = nullptr;
    int stride = 0;
    int count = 0;
    int compType = 0;
    bool normalized = false;
    QString type;
    if(!accessorByteInfo(root, buffers, accessorIndex,
                         &data, &stride, &count, &compType, &normalized, &type, error)) {
        return false;
    }
    if(type != QStringLiteral("VEC2")) {
        *error = QStringLiteral("Only VEC2 texture coordinate accessors are supported.");
        return false;
    }
    if(compType != 5126 && compType != 5121 && compType != 5123) {
        *error = QStringLiteral("Unsupported texture coordinate component type.");
        return false;
    }

    const int componentBytes = componentSize(compType);
    out->resize(count);
    for(int i = 0; i < count; ++i) {
        const char* const elem = data + i*stride;
        float values[2] = {0.f, 0.f};
        for(int c = 0; c < 2; ++c) {
            values[c] = normalizedComponentValue(elem + c*componentBytes,
                                                 compType,
                                                 normalized || compType != 5126);
        }
        (*out)[i] = QVector2D(values[0], values[1]);
    }
    return true;
}

float normalizedComponentValue(const char* const elem,
                               const int componentType,
                               const bool normalized) {
    switch(componentType) {
    case 5121: {
        const auto value = *reinterpret_cast<const quint8*>(elem);
        return normalized ? float(value) / 255.f : float(value);
    }
    case 5123: {
        quint16 value = 0;
        std::memcpy(&value, elem, sizeof(value));
        return normalized ? float(value) / 65535.f : float(value);
    }
    case 5126: {
        float value = 0.f;
        std::memcpy(&value, elem, sizeof(value));
        return value;
    }
    default:
        return 0.f;
    }
}

bool readWeightsAccessor(const QJsonObject& root,
                         const QVector<QByteArray>& buffers,
                         const int accessorIndex,
                         QVector<QVector4D>* const out,
                         QString* const error) {
    const char* data = nullptr;
    int stride = 0;
    int count = 0;
    int compType = 0;
    bool normalized = false;
    QString type;
    if(!accessorByteInfo(root, buffers, accessorIndex,
                         &data, &stride, &count, &compType, &normalized, &type, error)) {
        return false;
    }
    if(type != QStringLiteral("VEC4")) {
        *error = QStringLiteral("Only VEC4 weight accessors are supported.");
        return false;
    }
    if(compType != 5121 && compType != 5123 && compType != 5126) {
        *error = QStringLiteral("Unsupported weight component type.");
        return false;
    }

    const int componentBytes = componentSize(compType);
    out->resize(count);
    for(int i = 0; i < count; ++i) {
        const char* const elem = data + i*stride;
        float values[4] = {0.f, 0.f, 0.f, 0.f};
        float sum = 0.f;
        for(int c = 0; c < 4; ++c) {
            values[c] = normalizedComponentValue(elem + c*componentBytes,
                                                 compType,
                                                 normalized || compType != 5126);
            sum += values[c];
        }
        if(sum > 0.f) {
            for(float& value : values) {
                value /= sum;
            }
        }
        (*out)[i] = QVector4D(values[0], values[1], values[2], values[3]);
    }
    return true;
}

bool readJointsAccessor(const QJsonObject& root,
                        const QVector<QByteArray>& buffers,
                        const int accessorIndex,
                        QVector<QVector4D>* const out,
                        QString* const error) {
    const char* data = nullptr;
    int stride = 0;
    int count = 0;
    int compType = 0;
    QString type;
    if(!accessorByteInfo(root, buffers, accessorIndex,
                         &data, &stride, &count, &compType, nullptr, &type, error)) {
        return false;
    }
    if(type != QStringLiteral("VEC4")) {
        *error = QStringLiteral("Only VEC4 joint accessors are supported.");
        return false;
    }
    if(compType != 5121 && compType != 5123) {
        *error = QStringLiteral("Unsupported joint component type.");
        return false;
    }

    const int componentBytes = componentSize(compType);
    out->resize(count);
    for(int i = 0; i < count; ++i) {
        const char* const elem = data + i*stride;
        float values[4] = {0.f, 0.f, 0.f, 0.f};
        for(int c = 0; c < 4; ++c) {
            values[c] = normalizedComponentValue(elem + c*componentBytes,
                                                 compType,
                                                 false);
        }
        (*out)[i] = QVector4D(values[0], values[1], values[2], values[3]);
    }
    return true;
}

bool readMat4Accessor(const QJsonObject& root,
                      const QVector<QByteArray>& buffers,
                      const int accessorIndex,
                      QVector<Mat4>* const out,
                      QString* const error) {
    const char* data = nullptr;
    int stride = 0;
    int count = 0;
    int compType = 0;
    QString type;
    if(!accessorByteInfo(root, buffers, accessorIndex,
                         &data, &stride, &count, &compType, nullptr, &type, error)) {
        return false;
    }
    if(compType != 5126 || type != QStringLiteral("MAT4")) {
        *error = QStringLiteral("Only float MAT4 accessors are supported.");
        return false;
    }

    out->resize(count);
    for(int i = 0; i < count; ++i) {
        std::memcpy((*out)[i].m, data + i*stride, sizeof(float) * 16);
    }
    return true;
}

bool readIndicesAccessor(const QJsonObject& root,
                         const QVector<QByteArray>& buffers,
                         const int accessorIndex,
                         QVector<quint32>* const out,
                         QString* const error) {
    const char* data = nullptr;
    int stride = 0;
    int count = 0;
    int compType = 0;
    QString type;
    if(!accessorByteInfo(root, buffers, accessorIndex,
                         &data, &stride, &count, &compType, nullptr, &type, error)) {
        return false;
    }
    if(type != QStringLiteral("SCALAR")) {
        *error = QStringLiteral("Only scalar index accessors are supported.");
        return false;
    }

    out->resize(count);
    for(int i = 0; i < count; ++i) {
        const char* elem = data + i*stride;
        quint32 index = 0;
        switch(compType) {
        case 5121:
            index = quint8(*reinterpret_cast<const quint8*>(elem));
            break;
        case 5123: {
            quint16 tmp = 0;
            std::memcpy(&tmp, elem, sizeof(tmp));
            index = tmp;
            break;
        }
        case 5125:
            std::memcpy(&index, elem, sizeof(index));
            break;
        default:
            *error = QStringLiteral("Unsupported index component type.");
            return false;
        }
        (*out)[i] = index;
    }
    return true;
}

bool loadImageFromBytes(const QByteArray& bytes,
                        QImage* const out,
                        QString* const error) {
    if(bytes.isEmpty()) {
        *error = QStringLiteral("Image payload is empty.");
        return false;
    }
    QBuffer buffer;
    buffer.setData(bytes);
    if(!buffer.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("Unable to open image buffer.");
        return false;
    }
    QImage image;
    if(!image.load(&buffer, nullptr)) {
        const auto data = SkData::MakeWithoutCopy(bytes.constData(),
                                                  static_cast<size_t>(bytes.size()));
        const auto skImage = data ? SkImage::MakeFromEncoded(data) : nullptr;
        if(!skImage) {
            *error = QStringLiteral("Unable to decode image payload.");
            return false;
        }

        const auto rasterImage = skImage->makeRasterImage();
        const auto imageInfo = SkImageInfo::Make(skImage->width(),
                                                 skImage->height(),
                                                 kRGBA_8888_SkColorType,
                                                 kUnpremul_SkAlphaType);
        QImage decoded(skImage->width(), skImage->height(), QImage::Format_RGBA8888);
        if(decoded.isNull() ||
           !rasterImage ||
           !rasterImage->readPixels(imageInfo,
                                    decoded.bits(),
                                    static_cast<size_t>(decoded.bytesPerLine()),
                                    0, 0)) {
            *error = QStringLiteral("Unable to decode image payload.");
            return false;
        }
        image = decoded;
    }
    *out = image.convertToFormat(QImage::Format_RGBA8888);
    return !out->isNull();
}

bool parseImages(const QJsonObject& root,
                 const QVector<QByteArray>& buffers,
                 const QString& sourcePath,
                 GltfSceneDataPrivate* const out,
                 QString* const error) {
    const auto images = root.value(QStringLiteral("images")).toArray();
    const auto bufferViews = root.value(QStringLiteral("bufferViews")).toArray();
    const QDir baseDir = QFileInfo(sourcePath).dir();

    out->fImages.clear();
    out->fImages.reserve(images.size());
    for(int imageIndex = 0; imageIndex < images.size(); ++imageIndex) {
        const auto imageObject = images.at(imageIndex).toObject();
        QByteArray bytes;
        const QString uri = imageObject.value(QStringLiteral("uri")).toString();
        if(!uri.isEmpty()) {
            if(uri.startsWith(QStringLiteral("data:"), Qt::CaseInsensitive)) {
                if(!decodeDataUri(uri, &bytes)) {
                    *error = QStringLiteral("Failed to decode image data URI.");
                    return false;
                }
            } else {
                QFile file(baseDir.filePath(uri));
                if(!file.open(QIODevice::ReadOnly)) {
                    *error = QStringLiteral("Unable to open external image %1.").arg(uri);
                    return false;
                }
                bytes = file.readAll();
            }
        } else {
            const int viewIndex = imageObject.value(QStringLiteral("bufferView")).toInt(-1);
            if(viewIndex < 0 || viewIndex >= bufferViews.size()) {
                *error = QStringLiteral("Image bufferView index out of range.");
                return false;
            }
            const auto view = bufferViews.at(viewIndex).toObject();
            const int bufferIndex = view.value(QStringLiteral("buffer")).toInt(-1);
            if(bufferIndex < 0 || bufferIndex >= buffers.size()) {
                *error = QStringLiteral("Image buffer index out of range.");
                return false;
            }
            const int viewOffset = view.value(QStringLiteral("byteOffset")).toInt();
            const int byteLength = view.value(QStringLiteral("byteLength")).toInt();
            const QByteArray& buffer = buffers.at(bufferIndex);
            if(viewOffset < 0 || byteLength < 0 || viewOffset + byteLength > buffer.size()) {
                *error = QStringLiteral("Image bufferView exceeds buffer bounds.");
                return false;
            }
            bytes = QByteArray(buffer.constData() + viewOffset, byteLength);
        }

        QImage image;
        if(!loadImageFromBytes(bytes, &image, error)) {
            if(error && error->isEmpty()) {
                *error = QStringLiteral("Unable to decode image %1.").arg(imageIndex);
            }
            return false;
        }
        out->fImages.append(image);
    }
    return true;
}

void parseTextures(const QJsonObject& root,
                   GltfSceneDataPrivate* const out) {
    const auto textures = root.value(QStringLiteral("textures")).toArray();
    out->fTextures.resize(textures.size());
    for(int textureIndex = 0; textureIndex < textures.size(); ++textureIndex) {
        const auto textureObject = textures.at(textureIndex).toObject();
        int imageIndex = textureObject.value(QStringLiteral("source")).toInt(-1);
        if(imageIndex < 0) {
            const auto extensions = textureObject.value(QStringLiteral("extensions")).toObject();
            const auto webpExtension = extensions.value(QStringLiteral("EXT_texture_webp")).toObject();
            imageIndex = webpExtension.value(QStringLiteral("source")).toInt(-1);
        }
        out->fTextures[textureIndex].fImageIndex = imageIndex;
    }
}

void parseMaterials(const QJsonObject& root,
                    GltfSceneDataPrivate* const out) {
    const auto materials = root.value(QStringLiteral("materials")).toArray();
    out->fMaterials.resize(materials.size());
    for(int materialIndex = 0; materialIndex < materials.size(); ++materialIndex) {
        const auto materialObject = materials.at(materialIndex).toObject();
        auto& material = out->fMaterials[materialIndex];

        const auto pbr = materialObject.value(QStringLiteral("pbrMetallicRoughness")).toObject();
        const auto baseColorFactor = pbr.value(QStringLiteral("baseColorFactor")).toArray();
        if(baseColorFactor.size() == 4) {
            material.fBaseColorFactor = QVector4D(float(baseColorFactor.at(0).toDouble(1.0)),
                                                  float(baseColorFactor.at(1).toDouble(1.0)),
                                                  float(baseColorFactor.at(2).toDouble(1.0)),
                                                  float(baseColorFactor.at(3).toDouble(1.0)));
        }

        const auto baseColorTexture = pbr.value(QStringLiteral("baseColorTexture")).toObject();
        const int textureIndex = baseColorTexture.value(QStringLiteral("index")).toInt(-1);
        if(textureIndex >= 0 && textureIndex < out->fTextures.size()) {
            const int imageIndex = out->fTextures.at(textureIndex).fImageIndex;
            if(imageIndex >= 0 && imageIndex < out->fImages.size()) {
                material.fBaseColorTexture = imageIndex;
            }
        }
        material.fTexCoordSet = qMax(0, baseColorTexture.value(QStringLiteral("texCoord")).toInt(0));
        const auto textureExtensions = baseColorTexture.value(QStringLiteral("extensions")).toObject();
        const auto textureTransform = textureExtensions.value(QStringLiteral("KHR_texture_transform")).toObject();
        if(!textureTransform.isEmpty()) {
            material.fTexCoordSet = qMax(0, textureTransform.value(QStringLiteral("texCoord"))
                                         .toInt(material.fTexCoordSet));
            const auto offset = textureTransform.value(QStringLiteral("offset")).toArray();
            if(offset.size() == 2) {
                material.fTexCoordOffset = QVector2D(float(offset.at(0).toDouble()),
                                                     float(offset.at(1).toDouble()));
            }
            const auto scale = textureTransform.value(QStringLiteral("scale")).toArray();
            if(scale.size() == 2) {
                material.fTexCoordScale = QVector2D(float(scale.at(0).toDouble(1.0)),
                                                    float(scale.at(1).toDouble(1.0)));
            }
            material.fTexCoordRotation =
                    float(textureTransform.value(QStringLiteral("rotation")).toDouble(0.0));
        }

        const QString alphaMode = materialObject.value(QStringLiteral("alphaMode"))
                .toString(QStringLiteral("OPAQUE"));
        material.fAlphaBlend = alphaMode.compare(QStringLiteral("BLEND"),
                                                 Qt::CaseInsensitive) == 0;
        material.fAlphaMask = alphaMode.compare(QStringLiteral("MASK"),
                                                Qt::CaseInsensitive) == 0;
        material.fAlphaCutoff = float(materialObject.value(QStringLiteral("alphaCutoff"))
                                      .toDouble(0.5));
    }
}

void updateBounds(GltfSceneFrame* const frame,
                  const QVector3D& point) {
    if(frame->fVertices.isEmpty()) {
        frame->fBoundsMin = point;
        frame->fBoundsMax = point;
    } else {
        frame->fBoundsMin.setX(qMin(frame->fBoundsMin.x(), point.x()));
        frame->fBoundsMin.setY(qMin(frame->fBoundsMin.y(), point.y()));
        frame->fBoundsMin.setZ(qMin(frame->fBoundsMin.z(), point.z()));
        frame->fBoundsMax.setX(qMax(frame->fBoundsMax.x(), point.x()));
        frame->fBoundsMax.setY(qMax(frame->fBoundsMax.y(), point.y()));
        frame->fBoundsMax.setZ(qMax(frame->fBoundsMax.z(), point.z()));
    }
}

int morphTargetCountForMesh(const MeshData& mesh) {
    if(!mesh.fDefaultWeights.isEmpty()) {
        return mesh.fDefaultWeights.size();
    }
    for(const auto& primitive : mesh.fPrimitives) {
        if(!primitive.fVertices.isEmpty()) {
            return primitive.fVertices.first().fMorphPositionDeltas.size();
        }
    }
    return 0;
}

bool parseMeshes(const QJsonObject& root,
                 const QVector<QByteArray>& buffers,
                 GltfSceneDataPrivate* const out,
                 QString* const error) {
    const auto meshes = root.value(QStringLiteral("meshes")).toArray();
    out->fMeshes.resize(meshes.size());
    for(int meshIndex = 0; meshIndex < meshes.size(); ++meshIndex) {
        const auto meshObject = meshes.at(meshIndex).toObject();
        const auto primitives = meshObject.value(QStringLiteral("primitives")).toArray();
        auto& mesh = out->fMeshes[meshIndex];
        const auto meshWeights = meshObject.value(QStringLiteral("weights")).toArray();
        mesh.fDefaultWeights.reserve(meshWeights.size());
        for(const auto& weightValue : meshWeights) {
            mesh.fDefaultWeights.append(float(weightValue.toDouble()));
        }
        for(const auto& primitiveValue : primitives) {
            const auto primitive = primitiveValue.toObject();
            if(primitive.value(QStringLiteral("mode")).toInt(4) != 4) {
                continue;
            }

            const auto attrs = primitive.value(QStringLiteral("attributes")).toObject();
            const int posAccessor = attrs.value(QStringLiteral("POSITION")).toInt(-1);
            if(posAccessor < 0) {
                continue;
            }

            QVector<QVector4D> positions;
            if(!readVecAccessor(root, buffers, posAccessor, &positions, error)) {
                return false;
            }

            QVector<QVector4D> normals;
            const int normalAccessor = attrs.value(QStringLiteral("NORMAL")).toInt(-1);
            const bool hasNormals = normalAccessor >= 0;
            if(hasNormals && !readVecAccessor(root, buffers, normalAccessor, &normals, error)) {
                return false;
            }

            QVector<QVector2D> texCoords;
            const int texCoordAccessor = attrs.value(QStringLiteral("TEXCOORD_0")).toInt(-1);
            const bool hasTexCoords = texCoordAccessor >= 0;
            if(hasTexCoords &&
               !readVec2Accessor(root, buffers, texCoordAccessor, &texCoords, error)) {
                return false;
            }

            QVector<QVector2D> texCoords1;
            const int texCoord1Accessor = attrs.value(QStringLiteral("TEXCOORD_1")).toInt(-1);
            const bool hasTexCoords1 = texCoord1Accessor >= 0;
            if(hasTexCoords1 &&
               !readVec2Accessor(root, buffers, texCoord1Accessor, &texCoords1, error)) {
                return false;
            }

            QVector<QVector4D> joints;
            QVector<QVector4D> weights;
            const int jointsAccessor = attrs.value(QStringLiteral("JOINTS_0")).toInt(-1);
            const int weightsAccessor = attrs.value(QStringLiteral("WEIGHTS_0")).toInt(-1);
            const bool hasSkinning = jointsAccessor >= 0 && weightsAccessor >= 0;
            if(hasSkinning) {
                if(!readJointsAccessor(root, buffers, jointsAccessor, &joints, error) ||
                   !readWeightsAccessor(root, buffers, weightsAccessor, &weights, error)) {
                    return false;
                }
            }

            const auto targets = primitive.value(QStringLiteral("targets")).toArray();
            QVector<QVector<QVector4D>> morphPositions;
            QVector<QVector<QVector4D>> morphNormals;
            morphPositions.resize(targets.size());
            morphNormals.resize(targets.size());
            for(int targetIndex = 0; targetIndex < targets.size(); ++targetIndex) {
                const auto target = targets.at(targetIndex).toObject();
                const int morphPosAccessor = target.value(QStringLiteral("POSITION")).toInt(-1);
                if(morphPosAccessor >= 0 &&
                   !readVecAccessor(root, buffers, morphPosAccessor,
                                    &morphPositions[targetIndex], error)) {
                    return false;
                }
                const int morphNormalAccessor = target.value(QStringLiteral("NORMAL")).toInt(-1);
                if(morphNormalAccessor >= 0 &&
                   !readVecAccessor(root, buffers, morphNormalAccessor,
                                    &morphNormals[targetIndex], error)) {
                    return false;
                }
            }

            QVector<quint32> indices;
            if(primitive.contains(QStringLiteral("indices")) &&
               !readIndicesAccessor(root, buffers,
                                    primitive.value(QStringLiteral("indices")).toInt(-1),
                                    &indices, error)) {
                return false;
            }

            const int indexCount = indices.isEmpty() ? positions.size() : indices.size();
            if(indexCount < 3) {
                continue;
            }

            MeshPrimitive meshPrimitive;
            meshPrimitive.fMaterialIndex = primitive.value(QStringLiteral("material")).toInt(-1);
            meshPrimitive.fVertices.reserve((indexCount / 3) * 3);
            for(int tri = 0; tri < indexCount / 3; ++tri) {
                const int idx0 = tri*3;
                const int idx1 = idx0 + 1;
                const int idx2 = idx0 + 2;
                const quint32 i0 = indices.isEmpty() ? quint32(idx0) : indices.at(idx0);
                const quint32 i1 = indices.isEmpty() ? quint32(idx1) : indices.at(idx1);
                const quint32 i2 = indices.isEmpty() ? quint32(idx2) : indices.at(idx2);
                if(i0 >= quint32(positions.size()) ||
                   i1 >= quint32(positions.size()) ||
                   i2 >= quint32(positions.size())) {
                    continue;
                }

                const QVector3D p0 = positions.at(int(i0)).toVector3D();
                const QVector3D p1 = positions.at(int(i1)).toVector3D();
                const QVector3D p2 = positions.at(int(i2)).toVector3D();

                QVector3D n0;
                QVector3D n1;
                QVector3D n2;
                if(hasNormals &&
                   i0 < quint32(normals.size()) &&
                   i1 < quint32(normals.size()) &&
                   i2 < quint32(normals.size())) {
                    n0 = normals.at(int(i0)).toVector3D().normalized();
                    n1 = normals.at(int(i1)).toVector3D().normalized();
                    n2 = normals.at(int(i2)).toVector3D().normalized();
                } else {
                    const QVector3D faceNormal = QVector3D::crossProduct(p1 - p0, p2 - p0).normalized();
                    n0 = faceNormal;
                    n1 = faceNormal;
                    n2 = faceNormal;
                }

                RawMeshVertex v0;
                v0.fPosition = p0;
                v0.fNormal = n0;
                RawMeshVertex v1;
                v1.fPosition = p1;
                v1.fNormal = n1;
                RawMeshVertex v2;
                v2.fPosition = p2;
                v2.fNormal = n2;
                if(hasTexCoords &&
                   i0 < quint32(texCoords.size()) &&
                   i1 < quint32(texCoords.size()) &&
                   i2 < quint32(texCoords.size())) {
                    v0.fTexCoord0 = texCoords.at(int(i0));
                    v1.fTexCoord0 = texCoords.at(int(i1));
                    v2.fTexCoord0 = texCoords.at(int(i2));
                }
                if(hasTexCoords1 &&
                   i0 < quint32(texCoords1.size()) &&
                   i1 < quint32(texCoords1.size()) &&
                   i2 < quint32(texCoords1.size())) {
                    v0.fTexCoord1 = texCoords1.at(int(i0));
                    v1.fTexCoord1 = texCoords1.at(int(i1));
                    v2.fTexCoord1 = texCoords1.at(int(i2));
                }
                v0.fMorphPositionDeltas.reserve(targets.size());
                v0.fMorphNormalDeltas.reserve(targets.size());
                v1.fMorphPositionDeltas.reserve(targets.size());
                v1.fMorphNormalDeltas.reserve(targets.size());
                v2.fMorphPositionDeltas.reserve(targets.size());
                v2.fMorphNormalDeltas.reserve(targets.size());
                for(int targetIndex = 0; targetIndex < targets.size(); ++targetIndex) {
                    const QVector3D d0 = i0 < quint32(morphPositions[targetIndex].size()) ?
                                morphPositions[targetIndex].at(int(i0)).toVector3D() :
                                QVector3D(0.f, 0.f, 0.f);
                    const QVector3D d1 = i1 < quint32(morphPositions[targetIndex].size()) ?
                                morphPositions[targetIndex].at(int(i1)).toVector3D() :
                                QVector3D(0.f, 0.f, 0.f);
                    const QVector3D d2 = i2 < quint32(morphPositions[targetIndex].size()) ?
                                morphPositions[targetIndex].at(int(i2)).toVector3D() :
                                QVector3D(0.f, 0.f, 0.f);
                    const QVector3D dn0 = i0 < quint32(morphNormals[targetIndex].size()) ?
                                morphNormals[targetIndex].at(int(i0)).toVector3D() :
                                QVector3D(0.f, 0.f, 0.f);
                    const QVector3D dn1 = i1 < quint32(morphNormals[targetIndex].size()) ?
                                morphNormals[targetIndex].at(int(i1)).toVector3D() :
                                QVector3D(0.f, 0.f, 0.f);
                    const QVector3D dn2 = i2 < quint32(morphNormals[targetIndex].size()) ?
                                morphNormals[targetIndex].at(int(i2)).toVector3D() :
                                QVector3D(0.f, 0.f, 0.f);
                    v0.fMorphPositionDeltas.append(d0);
                    v0.fMorphNormalDeltas.append(dn0);
                    v1.fMorphPositionDeltas.append(d1);
                    v1.fMorphNormalDeltas.append(dn1);
                    v2.fMorphPositionDeltas.append(d2);
                    v2.fMorphNormalDeltas.append(dn2);
                }
                if(hasSkinning &&
                   i0 < quint32(joints.size()) && i0 < quint32(weights.size()) &&
                   i1 < quint32(joints.size()) && i1 < quint32(weights.size()) &&
                   i2 < quint32(joints.size()) && i2 < quint32(weights.size())) {
                    const auto assignSkinning = [](RawMeshVertex& vertex,
                                                   const QVector4D& jointIndices,
                                                   const QVector4D& jointWeights) {
                        vertex.fHasSkinning = true;
                        vertex.fJointWeights = jointWeights;
                        vertex.fJointIndices[0] = qMax(0, qRound(jointIndices.x()));
                        vertex.fJointIndices[1] = qMax(0, qRound(jointIndices.y()));
                        vertex.fJointIndices[2] = qMax(0, qRound(jointIndices.z()));
                        vertex.fJointIndices[3] = qMax(0, qRound(jointIndices.w()));
                    };
                    assignSkinning(v0, joints.at(int(i0)), weights.at(int(i0)));
                    assignSkinning(v1, joints.at(int(i1)), weights.at(int(i1)));
                    assignSkinning(v2, joints.at(int(i2)), weights.at(int(i2)));
                }

                meshPrimitive.fVertices.append(v0);
                meshPrimitive.fVertices.append(v1);
                meshPrimitive.fVertices.append(v2);
                if(meshPrimitive.fVertices.size() >= kMaxTriangles * 3) {
                    break;
                }
            }
            if(!meshPrimitive.fVertices.isEmpty()) {
                mesh.fPrimitives.append(meshPrimitive);
            }
        }
    }
    return true;
}

void parseNodes(const QJsonObject& root,
                GltfSceneDataPrivate* const out) {
    const auto nodes = root.value(QStringLiteral("nodes")).toArray();
    out->fNodes.resize(nodes.size());
    for(int i = 0; i < nodes.size(); ++i) {
        const auto node = nodes.at(i).toObject();
        auto& dst = out->fNodes[i];
        dst.fMesh = node.value(QStringLiteral("mesh")).toInt(-1);
        dst.fSkin = node.value(QStringLiteral("skin")).toInt(-1);

        const auto children = node.value(QStringLiteral("children")).toArray();
        dst.fChildren.reserve(children.size());
        for(const auto& child : children) {
            dst.fChildren.append(child.toInt(-1));
        }

        const auto weights = node.value(QStringLiteral("weights")).toArray();
        dst.fWeights.reserve(weights.size());
        for(const auto& weightValue : weights) {
            dst.fWeights.append(float(weightValue.toDouble()));
        }

        const auto matrixArray = node.value(QStringLiteral("matrix")).toArray();
        if(matrixArray.size() == 16) {
            dst.fHasMatrix = true;
            for(int j = 0; j < 16; ++j) {
                dst.fMatrix.m[j] = float(matrixArray.at(j).toDouble(j % 5 == 0 ? 1.0 : 0.0));
            }
            continue;
        }

        const auto translation = node.value(QStringLiteral("translation")).toArray();
        if(translation.size() == 3) {
            dst.fTranslation = QVector3D(float(translation.at(0).toDouble()),
                                         float(translation.at(1).toDouble()),
                                         float(translation.at(2).toDouble()));
        }

        const auto rotation = node.value(QStringLiteral("rotation")).toArray();
        if(rotation.size() == 4) {
            dst.fRotation = QQuaternion(float(rotation.at(3).toDouble()),
                                        float(rotation.at(0).toDouble()),
                                        float(rotation.at(1).toDouble()),
                                        float(rotation.at(2).toDouble()));
        }

        const auto scale = node.value(QStringLiteral("scale")).toArray();
        if(scale.size() == 3) {
            dst.fScale = QVector3D(float(scale.at(0).toDouble(1.0)),
                                   float(scale.at(1).toDouble(1.0)),
                                   float(scale.at(2).toDouble(1.0)));
        }
    }
}

bool parseSkins(const QJsonObject& root,
                const QVector<QByteArray>& buffers,
                GltfSceneDataPrivate* const out,
                QString* const error) {
    const auto skins = root.value(QStringLiteral("skins")).toArray();
    out->fSkins.resize(skins.size());
    for(int i = 0; i < skins.size(); ++i) {
        const auto skin = skins.at(i).toObject();
        auto& dst = out->fSkins[i];

        const auto joints = skin.value(QStringLiteral("joints")).toArray();
        dst.fJointNodes.reserve(joints.size());
        for(const auto& jointValue : joints) {
            dst.fJointNodes.append(jointValue.toInt(-1));
        }

        const int inverseBindAccessor = skin.value(QStringLiteral("inverseBindMatrices")).toInt(-1);
        if(inverseBindAccessor >= 0) {
            if(!readMat4Accessor(root, buffers, inverseBindAccessor,
                                 &dst.fInverseBindMatrices, error)) {
                return false;
            }
        }
        if(dst.fInverseBindMatrices.size() < dst.fJointNodes.size()) {
            dst.fInverseBindMatrices.resize(dst.fJointNodes.size());
        }
    }
    return true;
}

void parseSceneRoots(const QJsonObject& root,
                     GltfSceneDataPrivate* const out) {
    const auto scenes = root.value(QStringLiteral("scenes")).toArray();
    if(scenes.isEmpty()) {
        return;
    }
    const int sceneIndex = qBound(0,
                                  root.value(QStringLiteral("scene")).toInt(0),
                                  scenes.size() - 1);
    const auto roots = scenes.at(sceneIndex).toObject()
            .value(QStringLiteral("nodes")).toArray();
    out->fSceneRoots.reserve(roots.size());
    for(const auto& rootNode : roots) {
        out->fSceneRoots.append(rootNode.toInt(-1));
    }
}

ChannelPath pathFromString(const QString& path) {
    if(path == QStringLiteral("rotation")) {
        return ChannelPath::rotation;
    }
    if(path == QStringLiteral("scale")) {
        return ChannelPath::scale;
    }
    if(path == QStringLiteral("weights")) {
        return ChannelPath::weights;
    }
    return ChannelPath::translation;
}

Interpolation interpolationFromString(const QString& interpolation) {
    return interpolation.compare(QStringLiteral("STEP"), Qt::CaseInsensitive) == 0 ?
                Interpolation::step :
                Interpolation::linear;
}

bool parseAnimations(const QJsonObject& root,
                     const QVector<QByteArray>& buffers,
                     GltfSceneDataPrivate* const out,
                     QVector<GltfAnimationClipInfo>* const clipInfos,
                     QString* const error) {
    const auto animations = root.value(QStringLiteral("animations")).toArray();
    out->fClips.reserve(animations.size());
    clipInfos->reserve(animations.size());
    for(int clipIndex = 0; clipIndex < animations.size(); ++clipIndex) {
        const auto animation = animations.at(clipIndex).toObject();
        AnimationClip clip;
        clip.fName = animation.value(QStringLiteral("name")).toString();
        if(clip.fName.isEmpty()) {
            clip.fName = QStringLiteral("Animation %1").arg(clipIndex + 1);
        }

        const auto samplers = animation.value(QStringLiteral("samplers")).toArray();
        clip.fSamplers.reserve(samplers.size());
        for(const auto& samplerValue : samplers) {
            const auto sampler = samplerValue.toObject();
            const int inputAccessor = sampler.value(QStringLiteral("input")).toInt(-1);
            const int outputAccessor = sampler.value(QStringLiteral("output")).toInt(-1);
            if(inputAccessor < 0 || outputAccessor < 0) {
                clip.fSamplers.append(AnimationSampler{});
                continue;
            }

            AnimationSampler parsed;
            parsed.fInterpolation = interpolationFromString(
                        sampler.value(QStringLiteral("interpolation")).toString());
            if(!readFloatAccessor(root, buffers, inputAccessor, &parsed.fInputs, error)) {
                return false;
            }
            clip.fSamplers.append(parsed);
            clip.fDurationSeconds = qMax(clip.fDurationSeconds,
                                         parsed.fInputs.isEmpty() ? 0.f : parsed.fInputs.last());
        }

        const auto channels = animation.value(QStringLiteral("channels")).toArray();
        clip.fChannels.reserve(channels.size());
        for(const auto& channelValue : channels) {
            const auto channel = channelValue.toObject();
            const auto target = channel.value(QStringLiteral("target")).toObject();
            const QString path = target.value(QStringLiteral("path")).toString();
            if(path != QStringLiteral("translation") &&
               path != QStringLiteral("rotation") &&
               path != QStringLiteral("scale") &&
               path != QStringLiteral("weights")) {
                continue;
            }
            AnimationChannel parsed;
            parsed.fNode = target.value(QStringLiteral("node")).toInt(-1);
            parsed.fSampler = channel.value(QStringLiteral("sampler")).toInt(-1);
            parsed.fPath = pathFromString(path);
            if(parsed.fNode < 0 || parsed.fNode >= out->fNodes.size() ||
               parsed.fSampler < 0 || parsed.fSampler >= clip.fSamplers.size()) {
                continue;
            }
            clip.fSamplers[parsed.fSampler].fPath = parsed.fPath;
            if(parsed.fPath == ChannelPath::weights) {
                const int outputAccessor = samplers.at(parsed.fSampler).toObject()
                        .value(QStringLiteral("output")).toInt(-1);
                const int meshIndex = out->fNodes.at(parsed.fNode).fMesh;
                const int morphTargetCount =
                        meshIndex >= 0 && meshIndex < out->fMeshes.size() ?
                            morphTargetCountForMesh(out->fMeshes.at(meshIndex)) :
                            out->fNodes.at(parsed.fNode).fWeights.size();
                if(outputAccessor < 0 || morphTargetCount <= 0) {
                    continue;
                }
                if(!readWeightKeyframesAccessor(root, buffers, outputAccessor,
                                                morphTargetCount,
                                                &clip.fSamplers[parsed.fSampler].fWeightOutputs,
                                                error)) {
                    return false;
                }
            } else {
                const int outputAccessor = samplers.at(parsed.fSampler).toObject()
                        .value(QStringLiteral("output")).toInt(-1);
                if(outputAccessor < 0 ||
                   !readVecAccessor(root, buffers, outputAccessor,
                                    &clip.fSamplers[parsed.fSampler].fOutputs, error)) {
                    return false;
                }
            }
            clip.fChannels.append(parsed);
        }

        out->fClips.append(clip);
        clipInfos->append({clip.fName, clip.fDurationSeconds});
    }
    return true;
}

QVector4D sampleVectorSampler(const AnimationSampler& sampler,
                              const float timeSeconds) {
    if(sampler.fInputs.isEmpty() || sampler.fOutputs.isEmpty()) {
        return QVector4D();
    }
    if(sampler.fInputs.size() == 1 || sampler.fOutputs.size() == 1) {
        return sampler.fOutputs.first();
    }
    if(timeSeconds <= sampler.fInputs.first()) {
        return sampler.fOutputs.first();
    }
    if(timeSeconds >= sampler.fInputs.last()) {
        return sampler.fOutputs.last();
    }

    const auto begin = sampler.fInputs.constBegin();
    auto upper = std::upper_bound(begin, sampler.fInputs.constEnd(), timeSeconds);
    int right = int(upper - begin);
    int left = qMax(0, right - 1);
    right = qMin(right, sampler.fInputs.size() - 1);
    const float t0 = sampler.fInputs.at(left);
    const float t1 = sampler.fInputs.at(right);
    if(qFuzzyCompare(t0, t1) || sampler.fInterpolation == Interpolation::step) {
        return sampler.fOutputs.at(left);
    }

    const float alpha = qBound(0.f, (timeSeconds - t0) / (t1 - t0), 1.f);
    if(sampler.fPath == ChannelPath::rotation) {
        const auto a = sampler.fOutputs.at(left);
        const auto b = sampler.fOutputs.at(right);
        const QQuaternion q0(a.w(), a.x(), a.y(), a.z());
        const QQuaternion q1(b.w(), b.x(), b.y(), b.z());
        const QQuaternion q = QQuaternion::slerp(q0, q1, alpha).normalized();
        return QVector4D(q.x(), q.y(), q.z(), q.scalar());
    }
    return sampler.fOutputs.at(left) * (1.f - alpha) +
            sampler.fOutputs.at(right) * alpha;
}

QVector<float> sampleWeightSampler(const AnimationSampler& sampler,
                                   const float timeSeconds) {
    if(sampler.fInputs.isEmpty() || sampler.fWeightOutputs.isEmpty()) {
        return {};
    }
    if(sampler.fInputs.size() == 1 || sampler.fWeightOutputs.size() == 1) {
        return sampler.fWeightOutputs.first();
    }
    if(timeSeconds <= sampler.fInputs.first()) {
        return sampler.fWeightOutputs.first();
    }
    if(timeSeconds >= sampler.fInputs.last()) {
        return sampler.fWeightOutputs.last();
    }

    const auto begin = sampler.fInputs.constBegin();
    auto upper = std::upper_bound(begin, sampler.fInputs.constEnd(), timeSeconds);
    int right = int(upper - begin);
    int left = qMax(0, right - 1);
    right = qMin(right, sampler.fInputs.size() - 1);
    const float t0 = sampler.fInputs.at(left);
    const float t1 = sampler.fInputs.at(right);
    if(qFuzzyCompare(t0, t1) || sampler.fInterpolation == Interpolation::step) {
        return sampler.fWeightOutputs.at(left);
    }

    const float alpha = qBound(0.f, (timeSeconds - t0) / (t1 - t0), 1.f);
    const auto& leftWeights = sampler.fWeightOutputs.at(left);
    const auto& rightWeights = sampler.fWeightOutputs.at(right);
    const int count = qMin(leftWeights.size(), rightWeights.size());
    QVector<float> result(count, 0.f);
    for(int i = 0; i < count; ++i) {
        result[i] = leftWeights.at(i) * (1.f - alpha) +
                rightWeights.at(i) * alpha;
    }
    return result;
}

struct AnimatedNodePose {
    QVector3D fTranslation = QVector3D(0.f, 0.f, 0.f);
    QQuaternion fRotation;
    QVector3D fScale = QVector3D(1.f, 1.f, 1.f);
    QVector<float> fWeights;
    bool fHasTranslation = false;
    bool fHasRotation = false;
    bool fHasScale = false;
    bool fHasWeights = false;
};

QVector<AnimatedNodePose> sampleClipPose(const GltfSceneDataPrivate& data,
                                         const int clipIndex,
                                         const float timeSeconds) {
    QVector<AnimatedNodePose> result(data.fNodes.size());
    if(clipIndex < 0 || clipIndex >= data.fClips.size()) {
        return result;
    }

    const auto& clip = data.fClips.at(clipIndex);
    const float clampedTime = qBound(0.f, timeSeconds, clip.fDurationSeconds);
    for(const auto& channel : clip.fChannels) {
        if(channel.fNode < 0 || channel.fNode >= data.fNodes.size() ||
           channel.fSampler < 0 || channel.fSampler >= clip.fSamplers.size()) {
            continue;
        }
        auto& pose = result[channel.fNode];
        const auto value = sampleVectorSampler(clip.fSamplers.at(channel.fSampler),
                                               clampedTime);
        switch(channel.fPath) {
        case ChannelPath::translation:
            pose.fTranslation = value.toVector3D();
            pose.fHasTranslation = true;
            break;
        case ChannelPath::rotation:
            pose.fRotation = QQuaternion(value.w(),
                                         value.x(),
                                         value.y(),
                                         value.z()).normalized();
            pose.fHasRotation = true;
            break;
        case ChannelPath::scale:
            pose.fScale = value.toVector3D();
            pose.fHasScale = true;
            break;
        case ChannelPath::weights:
            pose.fWeights = sampleWeightSampler(clip.fSamplers.at(channel.fSampler),
                                                clampedTime);
            pose.fHasWeights = !pose.fWeights.isEmpty();
            break;
        }
    }
    return result;
}

QVector4D alignedQuaternionVector(const QQuaternion& quaternion,
                                  const QQuaternion& reference) {
    auto aligned = quaternion.normalized();
    if(QQuaternion::dotProduct(aligned, reference) < 0.f) {
        aligned = QQuaternion(-aligned.scalar(),
                              -aligned.x(),
                              -aligned.y(),
                              -aligned.z());
    }
    return QVector4D(aligned.x(), aligned.y(), aligned.z(), aligned.scalar());
}

void buildBasePose(const GltfSceneDataPrivate& data,
                   QVector<QVector3D>* const translations,
                   QVector<QQuaternion>* const rotations,
                   QVector<QVector3D>* const scales,
                   QVector<QVector<float>>* const weights) {
    translations->reserve(data.fNodes.size());
    rotations->reserve(data.fNodes.size());
    scales->reserve(data.fNodes.size());
    weights->reserve(data.fNodes.size());
    for(const auto& node : data.fNodes) {
        translations->append(node.fTranslation);
        rotations->append(node.fRotation);
        scales->append(node.fScale);
        if(!node.fWeights.isEmpty()) {
            weights->append(node.fWeights);
        } else if(node.fMesh >= 0 && node.fMesh < data.fMeshes.size()) {
            const auto& mesh = data.fMeshes.at(node.fMesh);
            auto baseWeights = mesh.fDefaultWeights;
            if(baseWeights.isEmpty()) {
                baseWeights.resize(morphTargetCountForMesh(mesh));
            }
            weights->append(baseWeights);
        } else {
            weights->append(QVector<float>{});
        }
    }
}

void blendLayersIntoPose(const GltfSceneDataPrivate& data,
                         const QVector<GltfAnimationLayer>& layers,
                         QVector<QVector3D>* const translations,
                         QVector<QQuaternion>* const rotations,
                         QVector<QVector3D>* const scales,
                         QVector<QVector<float>>* const weights) {
    buildBasePose(data, translations, rotations, scales, weights);
    if(layers.isEmpty()) {
        return;
    }

    QVector<QVector3D> translationSums(data.fNodes.size(), QVector3D(0.f, 0.f, 0.f));
    QVector<QVector3D> scaleSums(data.fNodes.size(), QVector3D(0.f, 0.f, 0.f));
    QVector<float> translationWeights(data.fNodes.size(), 0.f);
    QVector<float> scaleWeights(data.fNodes.size(), 0.f);
    QVector<QVector4D> rotationSums(data.fNodes.size(), QVector4D(0.f, 0.f, 0.f, 0.f));
    QVector<float> rotationWeights(data.fNodes.size(), 0.f);
    QVector<QQuaternion> rotationReferences(data.fNodes.size());
    QVector<bool> hasRotationReference(data.fNodes.size(), false);
    QVector<QVector<float>> weightSums(data.fNodes.size());
    QVector<float> weightBlendWeights(data.fNodes.size(), 0.f);

    for(const auto& layer : layers) {
        if(layer.fWeight <= 0.f) {
            continue;
        }
        const auto layerPose = sampleClipPose(data, layer.fClipIndex, layer.fTimeSeconds);
        for(int nodeIndex = 0; nodeIndex < layerPose.size(); ++nodeIndex) {
            const auto& pose = layerPose.at(nodeIndex);
            if(pose.fHasTranslation) {
                translationSums[nodeIndex] += pose.fTranslation * layer.fWeight;
                translationWeights[nodeIndex] += layer.fWeight;
            }
            if(pose.fHasScale) {
                scaleSums[nodeIndex] += pose.fScale * layer.fWeight;
                scaleWeights[nodeIndex] += layer.fWeight;
            }
            if(pose.fHasRotation) {
                if(!hasRotationReference[nodeIndex]) {
                    rotationReferences[nodeIndex] = pose.fRotation.normalized();
                    hasRotationReference[nodeIndex] = true;
                }
                rotationSums[nodeIndex] += alignedQuaternionVector(
                            pose.fRotation,
                            rotationReferences.at(nodeIndex)) * layer.fWeight;
                rotationWeights[nodeIndex] += layer.fWeight;
            }
            if(pose.fHasWeights) {
                if(weightSums[nodeIndex].size() < pose.fWeights.size()) {
                    weightSums[nodeIndex].resize(pose.fWeights.size());
                }
                for(int i = 0; i < pose.fWeights.size(); ++i) {
                    weightSums[nodeIndex][i] += pose.fWeights.at(i) * layer.fWeight;
                }
                weightBlendWeights[nodeIndex] += layer.fWeight;
            }
        }
    }

    for(int nodeIndex = 0; nodeIndex < data.fNodes.size(); ++nodeIndex) {
        if(translationWeights.at(nodeIndex) > 0.f) {
            const float baseWeight = qMax(0.f, 1.f - translationWeights.at(nodeIndex));
            const QVector3D sum = translationSums.at(nodeIndex) +
                    (*translations)[nodeIndex] * baseWeight;
            const float norm = translationWeights.at(nodeIndex) + baseWeight;
            if(norm > 0.f) {
                (*translations)[nodeIndex] = sum / norm;
            }
        }

        if(scaleWeights.at(nodeIndex) > 0.f) {
            const float baseWeight = qMax(0.f, 1.f - scaleWeights.at(nodeIndex));
            const QVector3D sum = scaleSums.at(nodeIndex) +
                    (*scales)[nodeIndex] * baseWeight;
            const float norm = scaleWeights.at(nodeIndex) + baseWeight;
            if(norm > 0.f) {
                (*scales)[nodeIndex] = sum / norm;
            }
        }

        if(rotationWeights.at(nodeIndex) > 0.f) {
            QVector4D sum = rotationSums.at(nodeIndex);
            float norm = rotationWeights.at(nodeIndex);
            const float baseWeight = qMax(0.f, 1.f - rotationWeights.at(nodeIndex));
            if(baseWeight > 0.f) {
                const QQuaternion reference = hasRotationReference.at(nodeIndex) ?
                            rotationReferences.at(nodeIndex) :
                            (*rotations)[nodeIndex];
                sum += alignedQuaternionVector((*rotations)[nodeIndex], reference) * baseWeight;
                norm += baseWeight;
            }
            if(norm > 0.f) {
                const QVector4D normalized = sum / norm;
                QQuaternion blended(normalized.w(),
                                    normalized.x(),
                                    normalized.y(),
                                    normalized.z());
                if(!qFuzzyIsNull(blended.lengthSquared())) {
                    (*rotations)[nodeIndex] = blended.normalized();
                }
            }
        }

        if(weightBlendWeights.at(nodeIndex) > 0.f) {
            const float baseWeight = qMax(0.f, 1.f - weightBlendWeights.at(nodeIndex));
            const int count = qMax(weightSums.at(nodeIndex).size(),
                                   (*weights)[nodeIndex].size());
            QVector<float> blended(count, 0.f);
            for(int i = 0; i < count; ++i) {
                const float layerValue = i < weightSums.at(nodeIndex).size() ?
                            weightSums.at(nodeIndex).at(i) : 0.f;
                const float baseValue = i < (*weights)[nodeIndex].size() ?
                            (*weights)[nodeIndex].at(i) : 0.f;
                const float norm = weightBlendWeights.at(nodeIndex) + baseWeight;
                blended[i] = norm > 0.f ?
                            (layerValue + baseValue * baseWeight) / norm :
                            0.f;
            }
            (*weights)[nodeIndex] = blended;
        }
    }
}

Mat4 nodeLocalMatrix(const NodeData& node,
                     const QVector<QVector3D>& translations,
                     const QVector<QQuaternion>& rotations,
                     const QVector<QVector3D>& scales,
                     const int nodeIndex) {
    return node.fHasMatrix ?
                node.fMatrix :
                trsMatrix(translations.at(nodeIndex),
                          rotations.at(nodeIndex),
                          scales.at(nodeIndex));
}

void collectGlobalNodeMatrices(const GltfSceneDataPrivate& data,
                               const int nodeIndex,
                               const Mat4& parent,
                               const QVector<QVector3D>& translations,
                               const QVector<QQuaternion>& rotations,
                               const QVector<QVector3D>& scales,
                               QVector<Mat4>* const globals) {
    if(nodeIndex < 0 || nodeIndex >= data.fNodes.size()) {
        return;
    }
    const auto& node = data.fNodes.at(nodeIndex);
    const Mat4 world = multiply(parent,
                                nodeLocalMatrix(node, translations, rotations,
                                                scales, nodeIndex));
    (*globals)[nodeIndex] = world;
    for(const int child : node.fChildren) {
        collectGlobalNodeMatrices(data, child, world,
                                  translations, rotations, scales, globals);
    }
}

QVector<QVector<Mat4>> buildSkinMatrices(const GltfSceneDataPrivate& data,
                                         const QVector<Mat4>& globalNodeMatrices) {
    QVector<QVector<Mat4>> result;
    result.resize(data.fSkins.size());
    for(int skinIndex = 0; skinIndex < data.fSkins.size(); ++skinIndex) {
        const auto& skin = data.fSkins.at(skinIndex);
        auto& palette = result[skinIndex];
        palette.reserve(skin.fJointNodes.size());
        for(int jointIndex = 0; jointIndex < skin.fJointNodes.size(); ++jointIndex) {
            const int jointNode = skin.fJointNodes.at(jointIndex);
            if(jointNode < 0 || jointNode >= globalNodeMatrices.size()) {
                palette.append(Mat4{});
                continue;
            }
            const Mat4 inverseBind = jointIndex < skin.fInverseBindMatrices.size() ?
                        skin.fInverseBindMatrices.at(jointIndex) :
                        Mat4{};
            palette.append(multiply(globalNodeMatrices.at(jointNode), inverseBind));
        }
    }
    return result;
}

QVector3D applySkinningToPoint(const RawMeshVertex& vertex,
                               const QVector<Mat4>& jointPalette) {
    Mat4 skinMatrix = zeroMatrix();
    bool usedSkin = false;
    const float weights[4] = {
        vertex.fJointWeights.x(),
        vertex.fJointWeights.y(),
        vertex.fJointWeights.z(),
        vertex.fJointWeights.w()
    };
    for(int i = 0; i < 4; ++i) {
        const float weight = weights[i];
        const int jointIndex = vertex.fJointIndices[i];
        if(weight <= 0.f || jointIndex < 0 || jointIndex >= jointPalette.size()) {
            continue;
        }
        skinMatrix = add(skinMatrix, multiplyScalar(jointPalette.at(jointIndex), weight));
        usedSkin = true;
    }
    return usedSkin ?
                transformPoint(skinMatrix, vertex.fPosition) :
                vertex.fPosition;
}

QVector3D applySkinningToNormal(const RawMeshVertex& vertex,
                                const QVector<Mat4>& jointPalette) {
    Mat4 skinMatrix = zeroMatrix();
    bool usedSkin = false;
    const float weights[4] = {
        vertex.fJointWeights.x(),
        vertex.fJointWeights.y(),
        vertex.fJointWeights.z(),
        vertex.fJointWeights.w()
    };
    for(int i = 0; i < 4; ++i) {
        const float weight = weights[i];
        const int jointIndex = vertex.fJointIndices[i];
        if(weight <= 0.f || jointIndex < 0 || jointIndex >= jointPalette.size()) {
            continue;
        }
        skinMatrix = add(skinMatrix, multiplyScalar(jointPalette.at(jointIndex), weight));
        usedSkin = true;
    }
    return usedSkin ?
                transformVector(skinMatrix, vertex.fNormal) :
                vertex.fNormal;
}

QVector3D applyMorphToPosition(const RawMeshVertex& vertex,
                               const QVector<float>& weights) {
    QVector3D result = vertex.fPosition;
    const int count = qMin(vertex.fMorphPositionDeltas.size(), weights.size());
    for(int i = 0; i < count; ++i) {
        result += vertex.fMorphPositionDeltas.at(i) * weights.at(i);
    }
    return result;
}

QVector3D applyMorphToNormal(const RawMeshVertex& vertex,
                             const QVector<float>& weights) {
    QVector3D result = vertex.fNormal;
    const int count = qMin(vertex.fMorphNormalDeltas.size(), weights.size());
    for(int i = 0; i < count; ++i) {
        result += vertex.fMorphNormalDeltas.at(i) * weights.at(i);
    }
    return result.normalized();
}

GltfMaterialInfo materialInfoForPrimitive(const GltfSceneDataPrivate& data,
                                          const MeshPrimitive& primitive) {
    GltfMaterialInfo material;
    if(primitive.fMaterialIndex < 0 || primitive.fMaterialIndex >= data.fMaterials.size()) {
        return material;
    }
    const auto& src = data.fMaterials.at(primitive.fMaterialIndex);
    material.fBaseColorFactor = src.fBaseColorFactor;
    material.fBaseColorTexture = src.fBaseColorTexture;
    material.fTexCoordSet = src.fTexCoordSet;
    material.fTexCoordOffset = src.fTexCoordOffset;
    material.fTexCoordScale = src.fTexCoordScale;
    material.fTexCoordRotation = src.fTexCoordRotation;
    material.fAlphaCutoff = src.fAlphaCutoff;
    material.fAlphaBlend = src.fAlphaBlend;
    material.fAlphaMask = src.fAlphaMask;
    return material;
}

void appendNodeGeometry(const GltfSceneDataPrivate& data,
                        const int nodeIndex,
                        const QVector<Mat4>& globalNodeMatrices,
                        const QVector<QVector<Mat4>>& skinMatrices,
                        const QVector<QVector<float>>& nodeWeights,
                        GltfSceneFrame* const frame) {
    if(nodeIndex < 0 || nodeIndex >= data.fNodes.size() ||
       frame->fTriangleCount >= kMaxTriangles) {
        return;
    }

    const auto& node = data.fNodes.at(nodeIndex);
    const Mat4 world = globalNodeMatrices.at(nodeIndex);

    if(node.fMesh >= 0 && node.fMesh < data.fMeshes.size()) {
        const auto& mesh = data.fMeshes.at(node.fMesh);
        const bool useSkin = node.fSkin >= 0 && node.fSkin < skinMatrices.size();
        const auto* const jointPalette = useSkin ?
                    &skinMatrices.at(node.fSkin) :
                    nullptr;
        const QVector<float>& morphWeights = nodeIndex < nodeWeights.size() ?
                    nodeWeights.at(nodeIndex) :
                    mesh.fDefaultWeights;
        for(const auto& primitive : mesh.fPrimitives) {
            GltfSceneBatch batch;
            batch.fVertexOffset = frame->fVertices.size();
            batch.fMaterial = materialInfoForPrimitive(data, primitive);
            for(const auto& vertex : primitive.fVertices) {
                if(frame->fTriangleCount >= kMaxTriangles) {
                    break;
                }
                const QVector3D morphedPosition = applyMorphToPosition(vertex, morphWeights);
                const QVector3D morphedNormal = applyMorphToNormal(vertex, morphWeights);
                RawMeshVertex morphedVertex = vertex;
                morphedVertex.fPosition = morphedPosition;
                morphedVertex.fNormal = morphedNormal;
                const QVector3D position = useSkin && vertex.fHasSkinning ?
                            applySkinningToPoint(morphedVertex, *jointPalette) :
                            transformPoint(world, morphedPosition);
                const QVector3D normal = useSkin && vertex.fHasSkinning ?
                            applySkinningToNormal(morphedVertex, *jointPalette) :
                            transformVector(world, morphedNormal);
                frame->fVertices.append({position,
                                        normal,
                                        vertex.fTexCoord0,
                                        vertex.fTexCoord1});
                updateBounds(frame, position);
                if(frame->fVertices.size() % 3 == 0) {
                    frame->fTriangleCount++;
                }
            }
            batch.fVertexCount = frame->fVertices.size() - batch.fVertexOffset;
            if(batch.fVertexCount > 0) {
                frame->fBatches.append(batch);
            }
        }
    }

    for(const int child : node.fChildren) {
        appendNodeGeometry(data, child, globalNodeMatrices,
                           skinMatrices, nodeWeights, frame);
    }
}

}

GltfSceneData GltfSceneReader::readFromFile(const QString &path) {
    GltfSceneData result;

    QJsonDocument json;
    QByteArray glbBin;
    QString error;
    if(!loadJsonDocument(path, &json, &glbBin, &error)) {
        result.fError = error;
        return result;
    }

    const auto root = json.object();
    QVector<QByteArray> buffers;
    if(!loadBuffers(root, path, glbBin, &buffers, &error)) {
        result.fError = error;
        return result;
    }

    auto data = std::make_shared<GltfSceneDataPrivate>();
    if(!parseImages(root, buffers, path, data.get(), &error)) {
        result.fError = error;
        return result;
    }
    parseTextures(root, data.get());
    parseMaterials(root, data.get());
    if(!parseMeshes(root, buffers, data.get(), &error)) {
        result.fError = error;
        return result;
    }
    parseNodes(root, data.get());
    if(!parseSkins(root, buffers, data.get(), &error)) {
        result.fError = error;
        return result;
    }
    parseSceneRoots(root, data.get());
    if(!parseAnimations(root, buffers, data.get(), &result.fAnimations, &error)) {
        result.fError = error;
        return result;
    }
    if(data->fSceneRoots.isEmpty() || data->fNodes.isEmpty()) {
        result.fError = QStringLiteral("No scenes or nodes found.");
        return result;
    }

    result.fData = data;
    const auto preview = sampleFrame(result, -1, 0.f);
    if(!preview.fValid || preview.empty()) {
        result.fError = preview.fError.isEmpty() ?
                    QStringLiteral("No renderable TRIANGLES mesh primitives found.") :
                    preview.fError;
        result.fData.reset();
        return result;
    }

    result.fValid = true;
    result.fBoundsMin = preview.fBoundsMin;
    result.fBoundsMax = preview.fBoundsMax;
    result.fTriangleCount = preview.fTriangleCount;
    return result;
}

GltfSceneFrame GltfSceneReader::sampleFrame(const GltfSceneData &scene,
                                            const int clipIndex,
                                            const float timeSeconds) {
    QVector<GltfAnimationLayer> layers;
    if(clipIndex >= 0) {
        layers.append({clipIndex, timeSeconds, 1.f});
    }
    return sampleFrame(scene, layers);
}

GltfSceneFrame GltfSceneReader::sampleFrame(
        const GltfSceneData &scene,
        const QVector<GltfAnimationLayer>& layers) {
    GltfSceneFrame frame;
    if(!scene.fData) {
        frame.fError = scene.fError.isEmpty() ?
                    QStringLiteral("Scene data unavailable.") :
                    scene.fError;
        return frame;
    }

    const auto& data = *scene.fData;
    frame.fTextures = data.fImages;
    frame.fReferenceBoundsMin = scene.fBoundsMin;
    frame.fReferenceBoundsMax = scene.fBoundsMax;
    QVector<QVector3D> translations;
    QVector<QQuaternion> rotations;
    QVector<QVector3D> scales;
    QVector<QVector<float>> weights;
    blendLayersIntoPose(data, layers, &translations, &rotations, &scales, &weights);

    QVector<Mat4> globalNodeMatrices(data.fNodes.size(), Mat4{});
    for(const int rootNode : data.fSceneRoots) {
        collectGlobalNodeMatrices(data, rootNode, Mat4{},
                                  translations, rotations, scales,
                                  &globalNodeMatrices);
    }
    const auto skinMatrices = buildSkinMatrices(data, globalNodeMatrices);

    for(const int rootNode : data.fSceneRoots) {
        appendNodeGeometry(data, rootNode, globalNodeMatrices,
                           skinMatrices, weights, &frame);
    }

    if(frame.empty()) {
        frame.fError = QStringLiteral("No renderable TRIANGLES mesh primitives found.");
        return frame;
    }
    frame.fValid = true;
    return frame;
}
