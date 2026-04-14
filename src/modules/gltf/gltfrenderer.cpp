#include "gltfrenderer.h"

#include <QImage>
#include <QMatrix4x4>
#include <QMutex>
#include <QMutexLocker>
#include <QHash>
#include <QVector>
#include <QtGlobal>

#include "../../core/Private/Tasks/offscreenqgl33c.h"
#include "../../core/glhelpers.h"
#include "../../core/skia/skqtconversions.h"

namespace {

struct GlVertex {
    float x;
    float y;
    float z;
    float nx;
    float ny;
    float nz;
    float u0;
    float v0;
    float u1;
    float v1;
};

class PreviewRendererState {
public:
    ~PreviewRendererState() {
        if(!mContextInitialized) {
            return;
        }
        mGl.makeCurrent();
        if(mProgram != 0) {
            mGl.glDeleteProgram(mProgram);
        }
        if(mVbo != 0) {
            mGl.glDeleteBuffers(1, &mVbo);
        }
        for(auto textureId : qAsConst(mSourceTextures)) {
            if(textureId != 0) {
                mGl.glDeleteTextures(1, &textureId);
            }
        }
        if(mVao != 0) {
            mGl.glDeleteVertexArrays(1, &mVao);
        }
        if(mColorTexture != 0) {
            mGl.glDeleteTextures(1, &mColorTexture);
        }
        if(mDepthBuffer != 0) {
            mGl.glDeleteRenderbuffers(1, &mDepthBuffer);
        }
        if(mMsaaColorBuffer != 0) {
            mGl.glDeleteRenderbuffers(1, &mMsaaColorBuffer);
        }
        if(mMsaaDepthBuffer != 0) {
            mGl.glDeleteRenderbuffers(1, &mMsaaDepthBuffer);
        }
        if(mFbo != 0) {
            mGl.glDeleteFramebuffers(1, &mFbo);
        }
        if(mMsaaFbo != 0) {
            mGl.glDeleteFramebuffers(1, &mMsaaFbo);
        }
        mGl.doneCurrent();
    }

    bool initialize(QString* const error) {
        if(mInitialized) {
            return true;
        }
        if(!mContextInitialized) {
            mGl.initialize();
            mContextInitialized = true;
        }
        mGl.makeCurrent();

        gIniProgram(&mGl, mProgram,
                    QStringLiteral(":/shaders/gltfpreview.vert"),
                    QStringLiteral(":/shaders/gltfpreview.frag"));

        mGl.glGenVertexArrays(1, &mVao);
        mGl.glGenBuffers(1, &mVbo);
        mGl.glBindVertexArray(mVao);
        mGl.glBindBuffer(GL_ARRAY_BUFFER, mVbo);
        mGl.glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                                  sizeof(GlVertex), BUFFER_OFFSET(0));
        mGl.glEnableVertexAttribArray(0);
        mGl.glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
                                  sizeof(GlVertex), BUFFER_OFFSET(3 * sizeof(float)));
        mGl.glEnableVertexAttribArray(1);
        mGl.glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE,
                                  sizeof(GlVertex), BUFFER_OFFSET(6 * sizeof(float)));
        mGl.glEnableVertexAttribArray(2);
        mGl.glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE,
                                  sizeof(GlVertex), BUFFER_OFFSET(8 * sizeof(float)));
        mGl.glEnableVertexAttribArray(3);

        mGl.glGenFramebuffers(1, &mFbo);
        mGl.glGenFramebuffers(1, &mMsaaFbo);
        mGl.glGenTextures(1, &mColorTexture);
        mGl.glGenRenderbuffers(1, &mDepthBuffer);
        mGl.glGenRenderbuffers(1, &mMsaaColorBuffer);
        mGl.glGenRenderbuffers(1, &mMsaaDepthBuffer);
        GLint maxSamples = 0;
        mGl.glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
        mMsaaSamples = qBound(1, qMin(4, int(maxSamples)), 4);
        mInitialized = true;
        if(error) {
            error->clear();
        }
        return true;
    }

    sk_sp<SkImage> render(const GltfSceneFrame& scene,
                          const QString& renderMode,
                          const GltfRenderer::RenderTransform& transform,
                          const QSize& size,
                          QString* const error) {
        if(!initialize(error)) {
            return nullptr;
        }
        if(scene.empty()) {
            if(error) {
                *error = QStringLiteral("GLB frame has no triangles to render.");
            }
            return nullptr;
        }

        const QSize renderSize = size.isValid() ? size : QSize(960, 540);
        mGl.makeCurrent();
        if(!ensureFramebuffer(renderSize, error)) {
            mGl.doneCurrent();
            return nullptr;
        }

        QVector<GlVertex> vertices;
        vertices.reserve(scene.fVertices.size());
        for(const auto& vertex : scene.fVertices) {
            vertices.append({vertex.fPosition.x(),
                             vertex.fPosition.y(),
                             vertex.fPosition.z(),
                             vertex.fNormal.x(),
                             vertex.fNormal.y(),
                             vertex.fNormal.z(),
                             vertex.fTexCoord0.x(),
                             vertex.fTexCoord0.y(),
                             vertex.fTexCoord1.x(),
                             vertex.fTexCoord1.y()});
        }

        const GLuint drawFbo = mMsaaSamples > 1 ? mMsaaFbo : mFbo;
        mGl.glBindFramebuffer(GL_FRAMEBUFFER, drawFbo);
        mGl.glViewport(0, 0, renderSize.width(), renderSize.height());
        mGl.glEnable(GL_DEPTH_TEST);
        mGl.glDisable(GL_BLEND);
        mGl.glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        mGl.glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if(mMsaaSamples > 1) {
            mGl.glEnable(GL_MULTISAMPLE);
        }

        mGl.glUseProgram(mProgram);
        mGl.glBindVertexArray(mVao);
        mGl.glBindBuffer(GL_ARRAY_BUFFER, mVbo);
        mGl.glBufferData(GL_ARRAY_BUFFER,
                         vertices.size() * int(sizeof(GlVertex)),
                         vertices.constData(),
                         GL_DYNAMIC_DRAW);

        const QVector3D boundsMin = scene.fReferenceBoundsMax != scene.fReferenceBoundsMin ?
                    scene.fReferenceBoundsMin :
                    scene.fBoundsMin;
        const QVector3D boundsMax = scene.fReferenceBoundsMax != scene.fReferenceBoundsMin ?
                    scene.fReferenceBoundsMax :
                    scene.fBoundsMax;
        const QVector3D center = (boundsMin + boundsMax) * 0.5f;
        const QVector3D extent = boundsMax - boundsMin;
        const float maxExtent = qMax(0.001f,
                                     qMax(extent.x(),
                                          qMax(extent.y(), extent.z())));
        const float scale = 2.1f / maxExtent;

        QMatrix4x4 model;
        model.translate(transform.fTranslation);
        model.rotate(transform.fRotationEuler.z(), 0.f, 0.f, 1.f);
        model.rotate(transform.fRotationEuler.y(), 0.f, 1.f, 0.f);
        model.rotate(transform.fRotationEuler.x(), 1.f, 0.f, 0.f);
        model.scale(transform.fScale);
        model.scale(scale);
        model.translate(-center);

        QMatrix4x4 view;
        view.lookAt(QVector3D(0.f, 0.f, 4.0f),
                    QVector3D(0.f, 0.f, 0.f),
                    QVector3D(0.f, 1.f, 0.f));

        QMatrix4x4 projection;
        projection.perspective(35.f,
                               float(renderSize.width()) / qMax(1, renderSize.height()),
                               0.1f,
                               100.f);

        const QMatrix4x4 mv = view * model;
        const QMatrix4x4 mvp = projection * mv;
        const QMatrix3x3 normalMatrix = mv.normalMatrix();

        mGl.glUniformMatrix4fv(mGl.glGetUniformLocation(mProgram, "uMvp"),
                               1, GL_FALSE, mvp.constData());
        mGl.glUniformMatrix4fv(mGl.glGetUniformLocation(mProgram, "uModelView"),
                               1, GL_FALSE, mv.constData());
        mGl.glUniformMatrix3fv(mGl.glGetUniformLocation(mProgram, "uNormalMatrix"),
                               1, GL_FALSE, normalMatrix.constData());
        mGl.glUniform1i(mGl.glGetUniformLocation(mProgram, "uRenderMode"),
                        renderMode.compare(QStringLiteral("Matcap"),
                                           Qt::CaseInsensitive) == 0 ? 1 : 0);
        mGl.glUniform1i(mGl.glGetUniformLocation(mProgram, "uBaseColorTexture"), 0);

        const auto drawBatch = [&](const GltfSceneBatch& batch, const bool transparentPass) {
            if(batch.fVertexCount <= 0) {
                return;
            }
            const bool alphaBlend = batch.fMaterial.fAlphaBlend;
            if(alphaBlend != transparentPass) {
                return;
            }

            const bool hasTexture = batch.fMaterial.fBaseColorTexture >= 0 &&
                    batch.fMaterial.fBaseColorTexture < scene.fTextures.size() &&
                    !scene.fTextures.at(batch.fMaterial.fBaseColorTexture).isNull();
            if(alphaBlend) {
                mGl.glEnable(GL_BLEND);
                mGl.glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                mGl.glDepthMask(GL_FALSE);
            } else {
                mGl.glDisable(GL_BLEND);
                mGl.glDepthMask(GL_TRUE);
            }

            mGl.glUniform4f(mGl.glGetUniformLocation(mProgram, "uBaseColorFactor"),
                            batch.fMaterial.fBaseColorFactor.x(),
                            batch.fMaterial.fBaseColorFactor.y(),
                            batch.fMaterial.fBaseColorFactor.z(),
                            batch.fMaterial.fBaseColorFactor.w());
            mGl.glUniform1i(mGl.glGetUniformLocation(mProgram, "uHasTexture"),
                            hasTexture ? 1 : 0);
            mGl.glUniform1i(mGl.glGetUniformLocation(mProgram, "uAlphaMode"),
                            alphaBlend ? 2 : (batch.fMaterial.fAlphaMask ? 1 : 0));
            mGl.glUniform1f(mGl.glGetUniformLocation(mProgram, "uAlphaCutoff"),
                            batch.fMaterial.fAlphaCutoff);
            mGl.glUniform1i(mGl.glGetUniformLocation(mProgram, "uTexCoordSet"),
                            qBound(0, batch.fMaterial.fTexCoordSet, 1));
            mGl.glUniform2f(mGl.glGetUniformLocation(mProgram, "uTexCoordOffset"),
                            batch.fMaterial.fTexCoordOffset.x(),
                            batch.fMaterial.fTexCoordOffset.y());
            mGl.glUniform2f(mGl.glGetUniformLocation(mProgram, "uTexCoordScale"),
                            batch.fMaterial.fTexCoordScale.x(),
                            batch.fMaterial.fTexCoordScale.y());
            mGl.glUniform1f(mGl.glGetUniformLocation(mProgram, "uTexCoordRotation"),
                            batch.fMaterial.fTexCoordRotation);

            if(hasTexture) {
                GLuint textureId = ensureTexture(scene.fTextures.at(batch.fMaterial.fBaseColorTexture),
                                                 error);
                if(textureId == 0) {
                    return;
                }
                mGl.glActiveTexture(GL_TEXTURE0);
                mGl.glBindTexture(GL_TEXTURE_2D, textureId);
            } else {
                mGl.glActiveTexture(GL_TEXTURE0);
                mGl.glBindTexture(GL_TEXTURE_2D, 0);
            }

            mGl.glDrawArrays(GL_TRIANGLES, batch.fVertexOffset, batch.fVertexCount);
        };

        for(const auto& batch : scene.fBatches) {
            drawBatch(batch, false);
        }
        for(const auto& batch : scene.fBatches) {
            drawBatch(batch, true);
        }
        mGl.glDepthMask(GL_TRUE);
        mGl.glDisable(GL_BLEND);
        if(mMsaaSamples > 1) {
            mGl.glBindFramebuffer(GL_READ_FRAMEBUFFER, mMsaaFbo);
            mGl.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, mFbo);
            mGl.glBlitFramebuffer(0, 0,
                                  renderSize.width(), renderSize.height(),
                                  0, 0,
                                  renderSize.width(), renderSize.height(),
                                  GL_COLOR_BUFFER_BIT,
                                  GL_LINEAR);
            mGl.glBindFramebuffer(GL_FRAMEBUFFER, mFbo);
        }
        const GLenum drawError = mGl.glGetError();
        if(drawError != GL_NO_ERROR) {
            mGl.glBindFramebuffer(GL_FRAMEBUFFER, 0);
            mGl.doneCurrent();
            if(error) {
                *error = QStringLiteral("OpenGL draw failed (0x%1).")
                        .arg(QString::number(drawError, 16));
            }
            return nullptr;
        }
        mGl.glFinish();

        QImage image(renderSize, QImage::Format_RGBA8888);
        mGl.glReadPixels(0, 0,
                         renderSize.width(), renderSize.height(),
                         GL_RGBA, GL_UNSIGNED_BYTE, image.bits());
        mGl.glBindFramebuffer(GL_FRAMEBUFFER, 0);
        mGl.doneCurrent();
        const uchar* const bits = image.constBits();
        const int pixelCount = renderSize.width() * renderSize.height();
        bool hasVisiblePixel = false;
        for(int i = 0; i < pixelCount; ++i) {
            if(bits[i*4 + 3] > 0) {
                hasVisiblePixel = true;
                break;
            }
        }
        if(!hasVisiblePixel) {
            if(error) {
                *error = QStringLiteral(
                            "Rendered frame is fully transparent. "
                            "The model is likely outside the preview frustum.");
            }
            return nullptr;
        }
        if(error) {
            error->clear();
        }
        return toSkImage(image.mirrored());
    }
private:
    GLuint ensureTexture(const QImage& image, QString* const error) {
        if(image.isNull()) {
            return 0;
        }
        const quint64 key = image.cacheKey();
        const auto existing = mSourceTextures.constFind(key);
        if(existing != mSourceTextures.constEnd()) {
            return existing.value();
        }

        const QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
        if(rgba.isNull()) {
            if(error) {
                *error = QStringLiteral("Failed to convert GLB texture to RGBA.");
            }
            return 0;
        }

        GLuint textureId = 0;
        mGl.glGenTextures(1, &textureId);
        mGl.glBindTexture(GL_TEXTURE_2D, textureId);
        mGl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        mGl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        mGl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        mGl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        mGl.glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        mGl.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                         rgba.width(), rgba.height(),
                         0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.constBits());
        const GLenum texError = mGl.glGetError();
        if(texError != GL_NO_ERROR) {
            if(textureId != 0) {
                mGl.glDeleteTextures(1, &textureId);
            }
            if(error) {
                *error = QStringLiteral("OpenGL texture upload failed (0x%1).")
                        .arg(QString::number(texError, 16));
            }
            return 0;
        }
        mSourceTextures.insert(key, textureId);
        return textureId;
    }

    bool ensureFramebuffer(const QSize& size, QString* const error) {
        if(mFramebufferSize == size) {
            mGl.glBindFramebuffer(GL_FRAMEBUFFER, mFbo);
            if(mGl.glCheckFramebufferStatus(GL_FRAMEBUFFER) ==
               GL_FRAMEBUFFER_COMPLETE) {
                if(mMsaaSamples <= 1) {
                    return true;
                }
                mGl.glBindFramebuffer(GL_FRAMEBUFFER, mMsaaFbo);
                if(mGl.glCheckFramebufferStatus(GL_FRAMEBUFFER) ==
                   GL_FRAMEBUFFER_COMPLETE) {
                    return true;
                }
            }
        }

        mGl.glBindTexture(GL_TEXTURE_2D, mColorTexture);
        mGl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        mGl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        mGl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        mGl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        mGl.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                         size.width(), size.height(),
                         0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

        mGl.glBindRenderbuffer(GL_RENDERBUFFER, mDepthBuffer);
        mGl.glRenderbufferStorage(GL_RENDERBUFFER,
                                  GL_DEPTH24_STENCIL8,
                                  size.width(), size.height());

        mGl.glBindFramebuffer(GL_FRAMEBUFFER, mFbo);
        mGl.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_2D, mColorTexture, 0);
        const GLenum status = mGl.glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if(status != GL_FRAMEBUFFER_COMPLETE) {
            if(error) {
                *error = QStringLiteral("OpenGL framebuffer incomplete (0x%1).")
                        .arg(QString::number(status, 16));
            }
            return false;
        }

        if(mMsaaSamples > 1) {
            mGl.glBindRenderbuffer(GL_RENDERBUFFER, mMsaaColorBuffer);
            mGl.glRenderbufferStorageMultisample(GL_RENDERBUFFER,
                                                 mMsaaSamples,
                                                 GL_RGBA8,
                                                 size.width(),
                                                 size.height());

            mGl.glBindRenderbuffer(GL_RENDERBUFFER, mMsaaDepthBuffer);
            mGl.glRenderbufferStorageMultisample(GL_RENDERBUFFER,
                                                 mMsaaSamples,
                                                 GL_DEPTH24_STENCIL8,
                                                 size.width(),
                                                 size.height());

            mGl.glBindFramebuffer(GL_FRAMEBUFFER, mMsaaFbo);
            mGl.glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                          GL_RENDERBUFFER, mMsaaColorBuffer);
            mGl.glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                          GL_RENDERBUFFER, mMsaaDepthBuffer);
            const GLenum msaaStatus = mGl.glCheckFramebufferStatus(GL_FRAMEBUFFER);
            if(msaaStatus != GL_FRAMEBUFFER_COMPLETE) {
                if(error) {
                    *error = QStringLiteral("OpenGL MSAA framebuffer incomplete (0x%1).")
                            .arg(QString::number(msaaStatus, 16));
                }
                return false;
            }
        } else {
            mGl.glBindFramebuffer(GL_FRAMEBUFFER, mFbo);
            mGl.glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                          GL_RENDERBUFFER, mDepthBuffer);
        }
        mFramebufferSize = size;
        return true;
    }

    OffscreenQGL33c mGl;
    bool mContextInitialized = false;
    bool mInitialized = false;
    GLuint mProgram = 0;
    GLuint mVao = 0;
    GLuint mVbo = 0;
    GLuint mFbo = 0;
    GLuint mMsaaFbo = 0;
    GLuint mColorTexture = 0;
    GLuint mDepthBuffer = 0;
    GLuint mMsaaColorBuffer = 0;
    GLuint mMsaaDepthBuffer = 0;
    int mMsaaSamples = 1;
    QSize mFramebufferSize;
    QHash<quint64, GLuint> mSourceTextures;
};

PreviewRendererState& rendererState() {
    static PreviewRendererState state;
    return state;
}

QMutex& rendererMutex() {
    static QMutex mutex;
    return mutex;
}

}

sk_sp<SkImage> GltfRenderer::renderPreview(const GltfSceneFrame &scene,
                                           const QString &renderMode,
                                           const GltfRenderer::RenderTransform& transform,
                                           const QSize &size,
                                           QString *errorMessage) {
    QMutexLocker locker(&rendererMutex());
    try {
        return rendererState().render(scene, renderMode, transform, size, errorMessage);
    } catch(const std::exception& e) {
        if(errorMessage) {
            *errorMessage = QString::fromUtf8(e.what());
        }
        return nullptr;
    }
}
