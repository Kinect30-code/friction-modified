#ifndef GLBCLIPTRACK_H
#define GLBCLIPTRACK_H

#include "../../core/Animators/boolanimator.h"
#include "../../core/Animators/dynamiccomplexanimator.h"
#include "../../core/Animators/enumanimator.h"
#include "../../core/Animators/qrealanimator.h"
#include "../../core/Animators/staticcomplexanimator.h"

#include "gltfscene.h"

enum class GlbClipLoopMode {
    once = 0,
    loop = 1
};

class GlbClipTrack final : public StaticComplexAnimator {
    Q_OBJECT
    e_OBJECT
public:
    GlbClipTrack();

    void prp_setupTreeViewMenu(PropertyMenu* const menu) override;

    void setClipNames(const QStringList& clipNames);
    void setClipIndex(const int index);

    bool enabled(const qreal relFrame = 0) const;
    int clipIndex(const qreal relFrame = 0) const;
    QString clipName() const;
    qreal weight(const qreal relFrame = 0) const;
    qreal speed(const qreal relFrame = 0) const;
    qreal timeOffsetFrames(const qreal relFrame = 0) const;
    GlbClipLoopMode loopMode(const qreal relFrame = 0) const;

    EnumAnimator* clipAnimator() const { return mClip.get(); }
signals:
    void trackChanged();
private:
    qsptr<BoolAnimator> mEnabled;
    qsptr<EnumAnimator> mClip;
    qsptr<QrealAnimator> mWeight;
    qsptr<QrealAnimator> mSpeed;
    qsptr<QrealAnimator> mTimeOffsetFrames;
    qsptr<EnumAnimator> mLoopMode;
};

class GlbClipTrackList final : public DynamicComplexAnimator<GlbClipTrack> {
    Q_OBJECT
    e_OBJECT
public:
    GlbClipTrackList();

    void prp_setupTreeViewMenu(PropertyMenu* const menu) override;

    void ensurePrimaryTrack();
    void syncClipNames(const QStringList& clipNames);
    void setPrimaryClipIndex(const int clipIndex);

    QString describeSelection() const;
    int frameCountHint(const GltfSceneData& scene, const qreal fps) const;
    QVector<GltfAnimationLayer> buildLayers(const GltfSceneData& scene,
                                            const qreal sampleFrame,
                                            const qreal fps) const;
signals:
    void tracksChanged();
private:
    qsptr<GlbClipTrack> createTrack(const QString& name) const;
    void addTrack();
};

#endif // GLBCLIPTRACK_H
