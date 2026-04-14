#ifndef PARTICLESYSTEMEFFECT_H
#define PARTICLESYSTEMEFFECT_H

#include "../../core/RasterEffects/rastereffect.h"

class ColorAnimator;
class QPointFAnimator;
class IntAnimator;
class EnumAnimator;
class BoolAnimator;
class QrealAnimator;
class StaticComplexAnimator;
class BoxTargetProperty;
class BoundingBox;
class Gradient;
class ParticleOverLifeAnimator;

class ParticleSystemEffect : public RasterEffect {
public:
    ParticleSystemEffect();

    FrameRange prp_getIdenticalRelRange(const int relFrame) const override;

    stdsptr<RasterEffectCaller> getEffectCaller(
            const qreal relFrame,
            const qreal resolution,
            const qreal influence,
            BoxRenderData * const data) const override;

    bool forceMargin() const override { return true; }
    QMargins getMargin() const override;
private:
    void updateSpriteUiState();

    qsptr<StaticComplexAnimator> mEmitterGroup;
    qsptr<StaticComplexAnimator> mMotionGroup;
    qsptr<StaticComplexAnimator> mOverLifeGroup;
    qsptr<StaticComplexAnimator> mRenderGroup;
    qsptr<StaticComplexAnimator> mSpriteGroup;
    qsptr<StaticComplexAnimator> mSpriteSourceGroup;
    qsptr<StaticComplexAnimator> mSpriteSheetGroup;
    qsptr<StaticComplexAnimator> mSpritePlaybackGroup;
    qsptr<StaticComplexAnimator> mPerfGroup;

    qsptr<QPointFAnimator> mEmitter;
    qsptr<QPointFAnimator> mEmitterSize;
    qsptr<BoxTargetProperty> mEmitterTarget;
    qsptr<QrealAnimator> mBirthRate;
    qsptr<QrealAnimator> mLife;
    qsptr<QrealAnimator> mLifeRandomness;
    qsptr<QrealAnimator> mSpeed;
    qsptr<QrealAnimator> mSpeedRandomness;
    qsptr<QrealAnimator> mDirection;
    qsptr<QrealAnimator> mSpread;
    qsptr<QrealAnimator> mGravity;
    qsptr<QrealAnimator> mDrag;
    qsptr<QrealAnimator> mStartSize;
    qsptr<QrealAnimator> mEndSize;
    qsptr<QrealAnimator> mSizeRandomness;
    qsptr<QrealAnimator> mStartOpacity;
    qsptr<QrealAnimator> mEndOpacity;
    qsptr<QrealAnimator> mOpacityRandomness;
    qsptr<QrealAnimator> mSpeedOverLife;
    qsptr<QrealAnimator> mSoftness;
    qsptr<QrealAnimator> mSizeOverLife;
    qsptr<QrealAnimator> mOpacityOverLife;
    qsptr<BoolAnimator> mAdditive;
    qsptr<BoolAnimator> mRenderSource;
    qsptr<EnumAnimator> mColorMode;
    qsptr<ColorAnimator> mSolidColor;
    qsptr<Gradient> mColorOverLife;
    qsptr<QrealAnimator> mColorRandomness;
    qsptr<BoolAnimator> mUseSprite;
    qsptr<EnumAnimator> mSpriteColorBlendMode;
    qsptr<BoxTargetProperty> mSpriteSource;
    qsptr<EnumAnimator> mSpriteSourceMode;
    qsptr<EnumAnimator> mSpriteSourceTimeMode;
    qsptr<QrealAnimator> mSpriteSourceFrame;
    qsptr<QrealAnimator> mSpriteSourceStartFrame;
    qsptr<QrealAnimator> mSpriteSourceEndFrame;
    qsptr<EnumAnimator> mSpriteSourcePlaybackMode;
    qsptr<QrealAnimator> mSpriteSourceFrameRate;
    qsptr<IntAnimator> mSpriteColumns;
    qsptr<IntAnimator> mSpriteRows;
    qsptr<IntAnimator> mSpriteFrames;
    qsptr<EnumAnimator> mSpriteFrameMode;
    qsptr<EnumAnimator> mSpritePlaybackMode;
    qsptr<ParticleOverLifeAnimator> mSpriteFrameOverLife;
    qsptr<QrealAnimator> mSpriteFrameRate;
    qsptr<BoolAnimator> mSpriteRandomStart;
    qsptr<BoolAnimator> mOrientToMotion;
    qsptr<IntAnimator> mMaxParticles;
    qsptr<IntAnimator> mSeed;
};

#endif // PARTICLESYSTEMEFFECT_H
