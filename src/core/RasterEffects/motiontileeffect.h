#ifndef MOTIONTILEEFFECT_H
#define MOTIONTILEEFFECT_H

#include "rastereffect.h"

class QPointFAnimator;
class QrealAnimator;

class MotionTileEffect : public RasterEffect {
public:
    MotionTileEffect();

    stdsptr<RasterEffectCaller> getEffectCaller(
            const qreal relFrame,
            const qreal resolution,
            const qreal influence,
            BoxRenderData * const data) const override;

    bool forceMargin() const override { return true; }
    QMargins getMargin() const override;
private:
    qsptr<QrealAnimator> mOutputWidth;
    qsptr<QrealAnimator> mOutputHeight;
    qsptr<QPointFAnimator> mCenter;
    qsptr<QrealAnimator> mMirrorEdges;
};

#endif // MOTIONTILEEFFECT_H
