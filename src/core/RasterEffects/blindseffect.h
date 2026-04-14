#ifndef BLINDSEFFECT_H
#define BLINDSEFFECT_H

#include "rastereffect.h"

class QrealAnimator;

class BlindsEffect : public RasterEffect {
public:
    BlindsEffect();

    stdsptr<RasterEffectCaller> getEffectCaller(
            const qreal relFrame,
            const qreal resolution,
            const qreal influence,
            BoxRenderData * const data) const override;
private:
    qsptr<QrealAnimator> mCompletion;
    qsptr<QrealAnimator> mDirection;
    qsptr<QrealAnimator> mWidth;
    qsptr<QrealAnimator> mFeather;
};

#endif // BLINDSEFFECT_H
