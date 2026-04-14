#ifndef GLBBOX_H
#define GLBBOX_H

#include "../../core/Animators/enumanimator.h"
#include "../../core/Boxes/animationbox.h"
#include "../../core/FileCacheHandlers/filehandlerobjref.h"

#include "glbcliptrack.h"
#include "glbfilecachehandler.h"
#include "glbmetadata.h"
#include "gltfrenderer.h"
#include "gltfscene.h"

class GlbPreviewFrameHandler;

class GlbBox : public AnimationBox {
    e_OBJECT
protected:
    void prp_readPropertyXEV_impl(const QDomElement& ele,
                                  const XevImporter& imp) override;
    QDomElement prp_writePropertyXEV_impl(const XevExporter& exp) const override;
public:
    GlbBox();

    void changeSourceFile() override;

    void writeBoundingBox(eWriteStream& dst) const override;
    void readBoundingBox(eReadStream& src) override;

    void setFilePath(const QString& path);
    QString filePath() const;

    const GltfMetadata& metadata() const;
private:
    void fileHandlerConnector(ConnContext& conn, GlbFileCacheHandler* obj);
    void fileHandlerAfterAssigned(GlbFileCacheHandler* obj);
    void ensureClipTrackList();
    void ensureTransformControls();
    QSize currentPreviewSize() const;
    GltfRenderer::RenderTransform currentRenderTransform(const qreal relFrame) const;
    void setFilePathNoRename(const QString& path);
    void reloadMetadata();
    void rebuildPreview();
    void refreshAnimationSelector();

    QString mFilePath;
    GltfMetadata mMetadata;
    GltfSceneData mSceneData;
    qsptr<EnumAnimator> mRenderMode;
    qsptr<EnumAnimator> mAnimationSelector;
    qsptr<GlbClipTrackList> mClipTracks;
    qsptr<QrealAnimator> mModelPosX;
    qsptr<QrealAnimator> mModelPosY;
    qsptr<QrealAnimator> mModelPosZ;
    qsptr<QrealAnimator> mModelRotX;
    qsptr<QrealAnimator> mModelRotY;
    qsptr<QrealAnimator> mModelRotZ;
    qsptr<QrealAnimator> mModelScaleX;
    qsptr<QrealAnimator> mModelScaleY;
    qsptr<QrealAnimator> mModelScaleZ;
    qsptr<GlbPreviewFrameHandler> mFrameHandler;
    FileHandlerObjRef<GlbFileCacheHandler> mFileHandler;
    bool mClipTracksConnected = false;
};

#endif // GLBBOX_H
