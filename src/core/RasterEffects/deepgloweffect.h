#ifndef DEEPGLOWEFFECT_H
#define DEEPGLOWEFFECT_H

#include "rastereffect.h"

class ColorAnimator;
class QrealAnimator;

class CORE_EXPORT DeepGlowEffect : public RasterEffect {
    e_OBJECT
protected:
    DeepGlowEffect();
public:
    stdsptr<RasterEffectCaller> getEffectCaller(
            const qreal relFrame, const qreal resolution,
            const qreal influence, BoxRenderData* const data) const override;
    QMargins getMargin() const override;
    bool forceMargin() const override { return true; }
private:
    qsptr<QrealAnimator> mThreshold;
    qsptr<QrealAnimator> mRadius;
    qsptr<QrealAnimator> mIntensity;
    qsptr<QrealAnimator> mExposure;
    qsptr<ColorAnimator> mColor;
};

#endif // DEEPGLOWEFFECT_H
