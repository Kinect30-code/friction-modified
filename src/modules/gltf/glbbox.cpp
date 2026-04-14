#include "glbbox.h"

#include <QFileInfo>

#include "../../core/CacheHandlers/imagecachecontainer.h"
#include "../../core/Animators/customproperties.h"
#include "../../core/FileCacheHandlers/animationcachehandler.h"
#include "../../core/GUI/edialogs.h"
#include "../../core/canvas.h"
#include "../../core/fileshandler.h"
#include "../../core/pluginmanager.h"
#include "../../core/XML/xmlexporthelpers.h"

#include "gltfrenderer.h"

class GlbPreviewImageContainer final : public ImageCacheContainer {
    e_OBJECT
public:
    explicit GlbPreviewImageContainer(const sk_sp<SkImage>& image)
        : ImageCacheContainer(image, FrameRange::EMINMAX, nullptr) {}
};

class GlbPreviewFrameHandler final : public AnimationFrameHandler {
    e_OBJECT
public:
    using ReloadFunc = std::function<void()>;
    using RenderFunc = std::function<sk_sp<SkImage>(int)>;
    using FrameCountFunc = std::function<int()>;
    using SourceFpsFunc = std::function<qreal()>;
    GlbPreviewFrameHandler(const ReloadFunc& reloadFunc,
                           const RenderFunc& renderFunc,
                           const FrameCountFunc& frameCountFunc,
                           const SourceFpsFunc& sourceFpsFunc)
        : mReloadFunc(reloadFunc)
        , mRenderFunc(renderFunc)
        , mFrameCountFunc(frameCountFunc)
        , mSourceFpsFunc(sourceFpsFunc) {}

    ImageCacheContainer* getFrameAtFrame(const int relFrame) override {
        return ensureFrame(relFrame);
    }

    ImageCacheContainer* getFrameAtOrBeforeFrame(const int relFrame) override {
        return ensureFrame(relFrame);
    }

    eTask* scheduleFrameLoad(const int frame) override {
        Q_UNUSED(frame)
        return nullptr;
    }

    int getFrameCount() const override {
        return mFrameCountFunc ? qMax(1, mFrameCountFunc()) : 1;
    }

    qreal getSourceFps() const override {
        return mSourceFpsFunc ? mSourceFpsFunc() : 30.0;
    }

    void reload() override {
        if(mReloadFunc) {
            mReloadFunc();
        }
    }

    void invalidate() {
        mContainers.clear();
    }
private:
    ImageCacheContainer* ensureFrame(const int relFrame) {
        const int frame = qMax(0, relFrame);
        auto it = mContainers.find(frame);
        if(it != mContainers.end()) {
            return it.value().get();
        }
        const auto image = mRenderFunc ? mRenderFunc(frame) : sk_sp<SkImage>(nullptr);
        auto container = GlbPreviewImageContainer::sCreate<GlbPreviewImageContainer>(image);
        mContainers.insert(frame, container);
        return container.get();
    }

    ReloadFunc mReloadFunc;
    RenderFunc mRenderFunc;
    FrameCountFunc mFrameCountFunc;
    SourceFpsFunc mSourceFpsFunc;
    QHash<int, stdsptr<GlbPreviewImageContainer>> mContainers;
};

namespace {

void drawTextLine(SkCanvas* const canvas,
                  const QString& text,
                  const SkFont& font,
                  const SkPaint& paint,
                  const SkScalar x,
                  const SkScalar y) {
    const auto utf8 = text.toUtf8();
    canvas->drawString(utf8.constData(), x, y, font, paint);
}

sk_sp<SkImage> createPreviewImage(const QString& filePath,
                                  const GltfMetadata& metadata,
                                  const GltfSceneData& sceneData,
                                  const QString& renderMode,
                                  const QString& animationName,
                                  const bool pluginEnabled,
                                  const QString& statusLine = QString()) {
    constexpr int kWidth = 960;
    constexpr int kHeight = 540;

    auto surface = SkSurface::MakeRasterN32Premul(kWidth, kHeight);
    if(!surface) {
        return nullptr;
    }

    auto* const canvas = surface->getCanvas();
    canvas->clear(SkColorSetRGB(13, 16, 24));

    SkPaint paint;
    paint.setAntiAlias(true);

    paint.setColor(SkColorSetRGB(24, 30, 43));
    canvas->drawRect(SkRect::MakeWH(kWidth, kHeight), paint);

    paint.setColor(SkColorSetRGB(28, 84, 120));
    canvas->drawRect(SkRect::MakeXYWH(0, 0, kWidth, 130), paint);

    paint.setColor(SkColorSetARGB(110, 255, 255, 255));
    canvas->drawCircle(770, 90, 180, paint);
    paint.setColor(SkColorSetARGB(70, 255, 255, 255));
    canvas->drawCircle(860, 10, 110, paint);

    const auto panelRect = SkRect::MakeXYWH(38, 110, 884, 392);
    paint.setColor(SkColorSetARGB(220, 12, 16, 24));
    canvas->drawRoundRect(panelRect, 28, 28, paint);

    SkFont titleFont;
    titleFont.setSize(34);
    titleFont.setEdging(SkFont::Edging::kAntiAlias);

    SkFont bodyFont;
    bodyFont.setSize(20);
    bodyFont.setEdging(SkFont::Edging::kAntiAlias);

    SkFont smallFont;
    smallFont.setSize(17);
    smallFont.setEdging(SkFont::Edging::kAntiAlias);

    SkPaint titlePaint;
    titlePaint.setAntiAlias(true);
    titlePaint.setColor(SK_ColorWHITE);

    SkPaint bodyPaint = titlePaint;
    bodyPaint.setColor(SkColorSetRGB(223, 232, 245));

    SkPaint mutedPaint = titlePaint;
    mutedPaint.setColor(SkColorSetRGB(154, 170, 191));

    drawTextLine(canvas,
                 pluginEnabled ? QStringLiteral("GLB Layer") :
                                 QStringLiteral("GLB Layer Disabled"),
                 titleFont,
                 titlePaint,
                 68,
                 176);
    drawTextLine(canvas,
                 QFileInfo(filePath).fileName(),
                 bodyFont,
                 bodyPaint,
                 68,
                 212);

    int y = 264;
    const auto drawMetaLine = [&](const QString& label, const QString& value) {
        drawTextLine(canvas, label, bodyFont, bodyPaint, 68, y);
        drawTextLine(canvas, value, bodyFont, mutedPaint, 260, y);
        y += 36;
    };

    if(!metadata.fValid) {
        drawMetaLine(QStringLiteral("Status"), metadata.fError.isEmpty() ?
                     QStringLiteral("Metadata unavailable") :
                     metadata.fError);
    } else {
        drawMetaLine(QStringLiteral("Container"),
                     metadata.fBinaryContainer ?
                         QStringLiteral("GLB") :
                         QStringLiteral("glTF"));
        drawMetaLine(QStringLiteral("Render Mode"), renderMode);
        drawMetaLine(QStringLiteral("Selected Clip"), animationName);
        drawMetaLine(QStringLiteral("Animations"),
                     QString::number(metadata.fAnimations.count()));
        drawMetaLine(QStringLiteral("Static Mesh"),
                     sceneData.fValid ?
                         QStringLiteral("%1 triangles").arg(sceneData.fTriangleCount) :
                         QStringLiteral("Unavailable"));
        drawMetaLine(QStringLiteral("Meshes"),
                     QString::number(metadata.fMeshCount));
        drawMetaLine(QStringLiteral("Nodes"),
                     QString::number(metadata.fNodeCount));
        drawMetaLine(QStringLiteral("Materials"),
                     QString::number(metadata.fMaterialCount));
        if(!metadata.fVersion.isEmpty()) {
            drawMetaLine(QStringLiteral("glTF Version"), metadata.fVersion);
        }
        if(!metadata.fGenerator.isEmpty()) {
            drawMetaLine(QStringLiteral("Generator"), metadata.fGenerator);
        }
    }

    paint.setColor(SkColorSetARGB(255, 92, 209, 186));
    canvas->drawRect(SkRect::MakeXYWH(632, 204, 240, 6), paint);
    paint.setColor(SkColorSetARGB(255, 255, 195, 86));
    canvas->drawRect(SkRect::MakeXYWH(632, 240, 180, 6), paint);
    paint.setColor(SkColorSetARGB(255, 117, 150, 255));
    canvas->drawRect(SkRect::MakeXYWH(632, 276, 210, 6), paint);

    drawTextLine(canvas,
                 !statusLine.isEmpty() ? statusLine :
                 (pluginEnabled ?
                      QStringLiteral("GLB renderer fallback preview") :
                      QStringLiteral("Enable the GLB Viewer plugin to activate imports")),
                 smallFont,
                 mutedPaint,
                 68,
                 472);

    return surface->makeImageSnapshot();
}

}

GlbBox::GlbBox()
    : AnimationBox(QStringLiteral("GLB"), eBoxType::glb)
    , mFileHandler(this,
                   [](const QString& path) {
        return FilesHandler::sInstance ?
                    FilesHandler::sInstance->getFileHandler<GlbFileCacheHandler>(path) :
                    nullptr;
    },
                   [this](GlbFileCacheHandler* obj) {
        fileHandlerAfterAssigned(obj);
    },
                   [this](ConnContext& conn, GlbFileCacheHandler* obj) {
        fileHandlerConnector(conn, obj);
    })
{
    mRenderMode = enve::make_shared<EnumAnimator>(
                QStringLiteral("render mode"),
                QStringList() << QStringLiteral("Unlit")
                              << QStringLiteral("Matcap"),
                0);
    ca_addChild(mRenderMode);

    mAnimationSelector = enve::make_shared<EnumAnimator>(
                QStringLiteral("clip"),
                QStringList() << QStringLiteral("No Animations"),
                0);
    mAnimationSelector->SWT_setDisabled(true);
    mAnimationSelector->SWT_hide();
    ca_addChild(mAnimationSelector);

    connect(mRenderMode.get(), &QrealAnimator::baseValueChanged,
            this, [this](const qreal) { rebuildPreview(); });
    connect(mAnimationSelector.get(), &EnumAnimator::valueNamesChanged,
            this, [this](const QStringList&) {
        if(mFrameHandler) mFrameHandler->invalidate();
        animationDataChanged();
        planUpdate(UpdateReason::userChange);
    });
    connect(mAnimationSelector.get(), &QrealAnimator::baseValueChanged,
            this, [this](const qreal) {
        if(mFrameHandler) mFrameHandler->invalidate();
        animationDataChanged();
        planUpdate(UpdateReason::userChange);
    });

    mFrameHandler = enve::make_shared<GlbPreviewFrameHandler>(
                [this]() { reloadMetadata(); },
                [this](const int frame) {
        const bool enabled = PluginManager::isEnabled(PluginFeature::glbViewer);
        const QString renderMode = mRenderMode ?
                    mRenderMode->getCurrentValueName() :
                    QStringLiteral("Unlit");
        const qreal fps = 30.0;
        const QString animationName = mClipTracks ?
                    mClipTracks->describeSelection() :
                    (mAnimationSelector ? mAnimationSelector->getCurrentValueName() :
                                          QStringLiteral("No Animations"));
        const auto layers = mClipTracks ?
                    mClipTracks->buildLayers(mSceneData, frame, fps) :
                    QVector<GltfAnimationLayer>();
        auto sceneFrame = GltfSceneReader::sampleFrame(mSceneData, layers);
        if(enabled && sceneFrame.fValid && !sceneFrame.empty()) {
            QString renderError;
            auto image = GltfRenderer::renderPreview(sceneFrame,
                                                     renderMode,
                                                     currentRenderTransform(frame),
                                                     currentPreviewSize(),
                                                     &renderError);
            if(image) {
                return image;
            }
            sceneFrame.fError = renderError;
        }
        return createPreviewImage(mFilePath,
                                  mMetadata,
                                  mSceneData,
                                  renderMode,
                                  animationName,
                                  enabled,
                                  sceneFrame.fError);
    },
                [this]() {
        if(!mClipTracks) {
            return 1;
        }
        return mClipTracks->frameCountHint(mSceneData, 30.0);
    },
                []() { return 30.0; });
    setAnimationFramesHandler(mFrameHandler);

    mCustomProperties->prp_setName(QStringLiteral("GLB controls"));
    if(mFrameHandler) mFrameHandler->invalidate();
    animationDataChanged();
}

void GlbBox::fileHandlerConnector(ConnContext &conn, GlbFileCacheHandler *obj) {
    if(!obj) {
        return;
    }
    conn << connect(obj, &GlbFileCacheHandler::pathChanged,
                    this, [this, obj](const QString&) {
        fileHandlerAfterAssigned(obj);
    });
    conn << connect(obj, &GlbFileCacheHandler::reloaded,
                    this, [this, obj]() {
        fileHandlerAfterAssigned(obj);
    });
}

void GlbBox::fileHandlerAfterAssigned(GlbFileCacheHandler *obj) {
    mFilePath = obj ? obj->path() : QString();
    reloadMetadata();
}

void GlbBox::prp_readPropertyXEV_impl(const QDomElement &ele,
                                      const XevImporter &imp) {
    AnimationBox::prp_readPropertyXEV_impl(ele, imp);
    const QString absSrc = XevExportHelpers::getAbsAndRelFileSrc(ele, imp);
    setFilePathNoRename(absSrc);
}

QDomElement GlbBox::prp_writePropertyXEV_impl(const XevExporter &exp) const {
    auto result = AnimationBox::prp_writePropertyXEV_impl(exp);
    XevExportHelpers::setAbsAndRelFileSrc(mFilePath, result, exp);
    return result;
}

void GlbBox::changeSourceFile() {
    const QString path = eDialogs::openFile(
                QStringLiteral("Change Source"),
                mFilePath,
                QStringLiteral("GLB/glTF Files (*.glb *.gltf)"));
    if(!path.isEmpty()) {
        setFilePath(path);
    }
}

void GlbBox::writeBoundingBox(eWriteStream &dst) const {
    AnimationBox::writeBoundingBox(dst);
    dst.writeFilePath(mFilePath);
}

void GlbBox::readBoundingBox(eReadStream &src) {
    AnimationBox::readBoundingBox(src);
    const QString path = src.readFilePath();
    setFilePathNoRename(path);
}

void GlbBox::setFilePath(const QString &path) {
    setFilePathNoRename(path);
    rename(QFileInfo(path).completeBaseName());
}

QString GlbBox::filePath() const {
    return mFilePath;
}

const GltfMetadata &GlbBox::metadata() const {
    return mMetadata;
}

QSize GlbBox::currentPreviewSize() const {
    const auto* const scene = getParentScene();
    if(scene) {
        const int width = qMax(1, scene->getCanvasWidth());
        const int height = qMax(1, scene->getCanvasHeight());
        return QSize(width, height);
    }
    return QSize(960, 540);
}

void GlbBox::ensureTransformControls() {
    auto ensureScalar = [this](qsptr<QrealAnimator>& slot,
                               const QString& name,
                               const qreal value,
                               const qreal minValue,
                               const qreal maxValue,
                               const qreal step,
                               const int decimals) {
        if(slot) {
            return;
        }
        if(auto* const existing = mCustomProperties->ca_getFirstDescendantWithName<QrealAnimator>(name)) {
            slot = existing->ref<QrealAnimator>();
        } else {
            slot = enve::make_shared<QrealAnimator>(value, minValue, maxValue, step, name);
            slot->setNumberDecimals(decimals);
            mCustomProperties->addProperty(qSharedPointerCast<Animator>(slot));
        }
        connect(slot.get(), &QrealAnimator::baseValueChanged,
                this, [this](const qreal) { rebuildPreview(); });
    };

    ensureScalar(mModelPosX, QStringLiteral("model pos x"), 0.0, -20.0, 20.0, 0.01, 2);
    ensureScalar(mModelPosY, QStringLiteral("model pos y"), 0.0, -20.0, 20.0, 0.01, 2);
    ensureScalar(mModelPosZ, QStringLiteral("model pos z"), 0.0, -20.0, 20.0, 0.01, 2);
    ensureScalar(mModelRotX, QStringLiteral("model rot x"), 0.0, -360.0, 360.0, 0.1, 1);
    ensureScalar(mModelRotY, QStringLiteral("model rot y"), 0.0, -360.0, 360.0, 0.1, 1);
    ensureScalar(mModelRotZ, QStringLiteral("model rot z"), 0.0, -360.0, 360.0, 0.1, 1);
    ensureScalar(mModelScaleX, QStringLiteral("model scale x"), 1.0, 0.001, 100.0, 0.01, 3);
    ensureScalar(mModelScaleY, QStringLiteral("model scale y"), 1.0, 0.001, 100.0, 0.01, 3);
    ensureScalar(mModelScaleZ, QStringLiteral("model scale z"), 1.0, 0.001, 100.0, 0.01, 3);
}

GltfRenderer::RenderTransform GlbBox::currentRenderTransform(const qreal relFrame) const {
    GltfRenderer::RenderTransform result;
    if(mModelPosX && mModelPosY && mModelPosZ) {
        result.fTranslation = QVector3D(mModelPosX->getEffectiveValue(relFrame),
                                        mModelPosY->getEffectiveValue(relFrame),
                                        mModelPosZ->getEffectiveValue(relFrame));
    }
    if(mModelRotX && mModelRotY && mModelRotZ) {
        result.fRotationEuler = QVector3D(mModelRotX->getEffectiveValue(relFrame),
                                          mModelRotY->getEffectiveValue(relFrame),
                                          mModelRotZ->getEffectiveValue(relFrame));
    }
    if(mModelScaleX && mModelScaleY && mModelScaleZ) {
        result.fScale = QVector3D(mModelScaleX->getEffectiveValue(relFrame),
                                  mModelScaleY->getEffectiveValue(relFrame),
                                  mModelScaleZ->getEffectiveValue(relFrame));
    }
    return result;
}

void GlbBox::ensureClipTrackList() {
    if(!mClipTracks) {
        if(auto* const existing = mCustomProperties->ca_getFirstDescendant<GlbClipTrackList>()) {
            mClipTracks = existing->ref<GlbClipTrackList>();
        } else {
            mClipTracks = enve::make_shared<GlbClipTrackList>();
            mCustomProperties->addProperty(qSharedPointerCast<Animator>(mClipTracks));
        }
    }

    if(!mClipTracksConnected && mClipTracks) {
        connect(mClipTracks.get(), &GlbClipTrackList::tracksChanged,
                this, [this]() {
            if(mAnimationSelector && mClipTracks && mClipTracks->ca_getNumberOfChildren() > 0) {
                mAnimationSelector->setCurrentValue(
                            mClipTracks->getChild(0)->clipAnimator()->getCurrentValue());
            }
            rebuildPreview();
        });
        mClipTracksConnected = true;
    }

    if(mClipTracks) {
        mClipTracks->ensurePrimaryTrack();
    }
    mCustomProperties->SWT_setVisible(true);
}

void GlbBox::setFilePathNoRename(const QString &path) {
    if(path.isEmpty()) {
        mFileHandler.clear();
        return;
    }
    mFileHandler.assign(path);
}

void GlbBox::reloadMetadata() {
    ensureClipTrackList();
    ensureTransformControls();
    if(mFilePath.isEmpty()) {
        mMetadata = {};
        mSceneData = {};
    } else {
        mMetadata = GltfMetadataReader::readFromFile(mFilePath);
        mSceneData = GltfSceneReader::readFromFile(mFilePath);
    }
    refreshAnimationSelector();
    if(mFrameHandler) mFrameHandler->invalidate();
    animationDataChanged();
    prp_afterWholeInfluenceRangeChanged();
    planUpdate(UpdateReason::userChange);
}

void GlbBox::refreshAnimationSelector() {
    ensureClipTrackList();
    ensureTransformControls();
    QStringList names;
    for(const auto& clip : mSceneData.fAnimations) {
        names << clip.fName;
    }
    if(names.isEmpty()) {
        names = mMetadata.animationNames();
    }
    const QString oldName = mAnimationSelector->getCurrentValueName();
    int nextIndex = 0;
    if(!oldName.isEmpty()) {
        const int found = names.indexOf(oldName);
        if(found >= 0) {
            nextIndex = found;
        }
    }

    const QStringList safeNames = names.isEmpty() ?
                QStringList() << QStringLiteral("No Animations") :
                names;
    mAnimationSelector->setValueNames(safeNames);
    mAnimationSelector->setCurrentValue(qBound(0, nextIndex, safeNames.count() - 1));
    mAnimationSelector->SWT_setDisabled(safeNames.count() <= 1);

    if(mClipTracks) {
        mClipTracks->syncClipNames(safeNames);
        mClipTracks->setPrimaryClipIndex(mAnimationSelector->getCurrentValue());
    }
}

void GlbBox::rebuildPreview() {
    if(mFrameHandler) {
        mFrameHandler->invalidate();
    }
    planUpdate(UpdateReason::userChange);
}
