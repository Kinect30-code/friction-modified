#include "particlesystemeffect.h"
#include "particleoverlifeanimator.h"

#include "../../core/Animators/coloranimator.h"
#include "../../core/Animators/boolanimator.h"
#include "../../core/Animators/enumanimator.h"
#include "../../core/Animators/gradient.h"
#include "../../core/Animators/intanimator.h"
#include "../../core/Animators/qpointfanimator.h"
#include "../../core/Animators/qrealanimator.h"
#include "../../core/Animators/staticcomplexanimator.h"
#include "../../core/Boxes/boundingbox.h"
#include "../../core/Boxes/boxrenderdata.h"
#include "../../core/Properties/boxtargetproperty.h"
#include "../../core/canvas.h"
#include "../../core/appsupport.h"
#include "../../core/pluginmanager.h"
#include "../../core/skia/skiahelpers.h"
#include "../../core/swt_abstraction.h"

#include <QtMath>
#include <QColor>
#include <cstring>
#include <cstdint>

namespace {

class ParticleBoolAnimator final : public BoolAnimator {
public:
    explicit ParticleBoolAnimator(const QString& name)
        : BoolAnimator(name)
    {}
};

class ParticleUiGroupAnimator : public StaticComplexAnimator {
public:
    explicit ParticleUiGroupAnimator(const QString &name,
                                     const bool expandedByDefault = false)
        : StaticComplexAnimator(name)
        , mExpanded(expandedByDefault)
    {}

    void SWT_setupAbstraction(SWT_Abstraction *abstraction,
                              const UpdateFuncs &updateFuncs,
                              const int visiblePartWidgetId) override
    {
        StaticComplexAnimator::SWT_setupAbstraction(
                    abstraction, updateFuncs, visiblePartWidgetId);
        abstraction->setContentVisible(mExpanded);
    }

    void SWT_abstractionContentVisibilityChanged(const int visiblePartWidgetId,
                                                 const bool visible) override
    {
        Q_UNUSED(visiblePartWidgetId)
        mExpanded = visible;
    }

private:
    bool mExpanded = false;
};

class SpriteSourceTimeMode final : public EnumAnimator {
public:
    enum Value {
        compTime = 0,
        stillFrame = 1,
        localPlayback = 2
    };

    SpriteSourceTimeMode()
        : EnumAnimator("source time",
                       QStringList() << "comp time"
                                     << "still frame"
                                     << "local playback",
                       compTime) {}
};

class SpriteSourceMode final : public EnumAnimator {
public:
    enum Value {
        singleSprite = 0,
        spriteSheet = 1
    };

    SpriteSourceMode()
        : EnumAnimator("source mode",
                       QStringList() << "single sprite"
                                     << "sprite sheet",
                       singleSprite) {}
};

class SpriteFrameMode final : public EnumAnimator {
public:
    enum Value {
        overLife = 0,
        frameRate = 1
    };

    SpriteFrameMode()
        : EnumAnimator("frame mode",
                       QStringList() << "over life"
                                     << "frame rate",
                       overLife) {}
};

class SpritePlaybackMode final : public EnumAnimator {
public:
    enum Value {
        once = 0,
        loop = 1,
        pingPong = 2
    };

    SpritePlaybackMode()
        : EnumAnimator("playback mode",
                       QStringList() << "once"
                                     << "loop"
                                     << "ping pong",
                       once) {}
};

class SpriteSourcePlaybackMode final : public EnumAnimator {
public:
    enum Value {
        once = 0,
        loop = 1,
        pingPong = 2
    };

    SpriteSourcePlaybackMode()
        : EnumAnimator("source playback",
                       QStringList() << "once"
                                     << "loop"
                                     << "ping pong",
                       once) {}
};

class ParticleColorMode final : public EnumAnimator {
public:
    enum Value {
        solid = 0,
        gradient = 1
    };

    ParticleColorMode()
        : EnumAnimator("color mode",
                       QStringList() << "solid"
                                     << "gradient",
                       solid) {}
};

class SpriteColorBlendMode final : public EnumAnimator {
public:
    enum Value {
        normal = 0,
        multiply = 1,
        overlay = 2,
        add = 3
    };

    SpriteColorBlendMode()
        : EnumAnimator("color blend",
                       QStringList() << "normal"
                                     << "multiply"
                                     << "overlay"
                                     << "add",
                       normal) {}
};

struct ParticleColor {
    qreal r;
    qreal g;
    qreal b;
};

struct SpriteFrameRect {
    int left;
    int top;
    int width;
    int height;
};

qreal hash01(const int index, const uint32_t salt)
{
    uint32_t x = static_cast<uint32_t>(index) * 747796405u +
                 salt * 2891336453u + 0x9e3779b9u;
    x = (x >> ((x >> 28u) + 4u)) ^ x;
    x *= 277803737u;
    x = (x >> 22u) ^ x;
    return static_cast<qreal>(x & 0x00ffffffu) /
           static_cast<qreal>(0x01000000u);
}

qreal lerp(const qreal a, const qreal b, const qreal t)
{
    return a + (b - a) * t;
}

qreal centeredRandom(const qreal random01)
{
    return (random01 * 2.) - 1.;
}

qreal applyRandomness(const qreal base,
                      const qreal randomnessPct,
                      const qreal random01,
                      const qreal minimum)
{
    const qreal factor = 1. + centeredRandom(random01) * randomnessPct * 0.01;
    return qMax(minimum, base * factor);
}

ParticleColor toParticleColor(const QColor& color)
{
    return {
        qBound<qreal>(0., color.redF(), 1.),
        qBound<qreal>(0., color.greenF(), 1.),
        qBound<qreal>(0., color.blueF(), 1.)
    };
}

ParticleColor mixColor(const ParticleColor& a,
                       const ParticleColor& b,
                       const qreal t)
{
    return {
        lerp(a.r, b.r, t),
        lerp(a.g, b.g, t),
        lerp(a.b, b.b, t)
    };
}

qreal overlayBlendChannel(const qreal base, const qreal blend)
{
    if (base < 0.5) {
        return 2. * base * blend;
    }
    return 1. - 2. * (1. - base) * (1. - blend);
}

ParticleColor applyColorVariance(const ParticleColor& color,
                                 const qreal amountPct,
                                 const qreal randR,
                                 const qreal randG,
                                 const qreal randB)
{
    const qreal amount = amountPct * 0.01;
    return {
        qBound<qreal>(0., color.r + centeredRandom(randR) * amount, 1.),
        qBound<qreal>(0., color.g + centeredRandom(randG) * amount, 1.),
        qBound<qreal>(0., color.b + centeredRandom(randB) * amount, 1.)
    };
}

ParticleColor sampleGradientColor(Gradient * const gradient, const qreal t)
{
    if(!gradient) return {1., 1., 1.};
    const auto stops = gradient->getQGradientStops(qBound<qreal>(0., t, 1.) * 100.);
    if(stops.isEmpty()) return {1., 1., 1.};

    const qreal pos = qBound<qreal>(0., t, 1.);
    if(pos <= stops.first().first) return toParticleColor(stops.first().second);
    if(pos >= stops.last().first) return toParticleColor(stops.last().second);

    for(int i = 1; i < stops.size(); ++i) {
        const auto& prev = stops.at(i - 1);
        const auto& next = stops.at(i);
        if(pos <= next.first) {
            const qreal span = qMax<qreal>(0.0001, next.first - prev.first);
            const qreal localT = (pos - prev.first) / span;
            return mixColor(toParticleColor(prev.second),
                            toParticleColor(next.second),
                            localT);
        }
    }

    return toParticleColor(stops.last().second);
}

qreal particleTravel(const qreal speed,
                     const qreal speedRandomness,
                     const qreal life,
                     const qreal lifeRandomness,
                     const qreal gravity,
                     const qreal startSize,
                     const qreal endSize)
{
    const qreal maxSpeed = qAbs(speed) * (1. + speedRandomness * 0.01);
    const qreal maxLife = life * (1. + lifeRandomness * 0.01);
    const qreal maxSize = qMax(startSize, endSize);
    return maxSpeed * maxLife +
           0.5 * qAbs(gravity) * maxLife * maxLife +
           maxSize * 2.;
}

qreal sampleOverLifeCurve(QrealAnimator * const curve, const qreal t)
{
    if(!curve) return 1.;
    return curve->getEffectiveValue(qBound<qreal>(0., t, 1.) * 100.);
}

int applyPlaybackMode(const int frame,
                      const int availableFrames,
                      const int playbackMode)
{
    if (availableFrames <= 1) {
        return 0;
    }

    if (playbackMode == SpritePlaybackMode::loop) {
        return ((frame % availableFrames) + availableFrames) % availableFrames;
    }

    if (playbackMode == SpritePlaybackMode::pingPong) {
        if (availableFrames == 2) {
            return qAbs(frame % 2);
        }
        const int period = (availableFrames * 2) - 2;
        const int wrapped = ((frame % period) + period) % period;
        return wrapped < availableFrames ? wrapped : period - wrapped;
    }

    return qBound(0, frame, availableFrames - 1);
}

qreal applyPlaybackModeF(const qreal frame,
                         const qreal startFrame,
                         const qreal endFrame,
                         const int playbackMode)
{
    if (endFrame <= startFrame) {
        return frame;
    }

    const qreal span = endFrame - startFrame;
    const qreal local = frame - startFrame;

    if (playbackMode == SpriteSourcePlaybackMode::loop) {
        qreal wrapped = std::fmod(local, span);
        if (wrapped < 0.) {
            wrapped += span;
        }
        return startFrame + wrapped;
    }

    if (playbackMode == SpriteSourcePlaybackMode::pingPong) {
        const qreal period = span * 2.;
        qreal wrapped = std::fmod(local, period);
        if (wrapped < 0.) {
            wrapped += period;
        }
        return wrapped <= span ?
                    startFrame + wrapped :
                    endFrame - (wrapped - span);
    }

    return qBound(startFrame, frame, endFrame);
}

void setPropertyVisible(Property * const property, const bool visible)
{
    if (property) {
        property->SWT_setVisible(visible);
    }
}

SpriteFrameRect getSpriteFrameRect(const int frameIndex,
                                   const int columns,
                                   const int rows,
                                   const int totalWidth,
                                   const int totalHeight)
{
    const int safeColumns = qMax(1, columns);
    const int safeRows = qMax(1, rows);
    const int cellWidth = qMax(1, totalWidth / safeColumns);
    const int cellHeight = qMax(1, totalHeight / safeRows);
    const int totalFrames = safeColumns * safeRows;
    const int safeFrame = qBound(0, frameIndex, totalFrames - 1);
    const int col = safeFrame % safeColumns;
    const int row = safeFrame / safeColumns;
    return {
        col * cellWidth,
        row * cellHeight,
        cellWidth,
        cellHeight
    };
}

class ParticleSystemCaller : public RasterEffectCaller {
public:
    ParticleSystemCaller(const qreal relFrame,
                         const qreal fps,
                         BoundingBox * const emitterTarget,
                         const QPointF &emitterPct,
                         const QPointF &emitterSizePct,
                         const qreal birthRate,
                         const qreal life,
                         const qreal lifeRandomness,
                         const qreal speed,
                         const qreal speedRandomness,
                         const qreal direction,
                         const qreal spread,
                         const qreal gravity,
                         const qreal drag,
                         const qreal startSize,
                         const qreal endSize,
                         const qreal sizeRandomness,
                         const qreal startOpacity,
                         const qreal endOpacity,
                         const qreal opacityRandomness,
                         const qreal softness,
                         const int colorMode,
                         const ParticleColor &solidColor,
                         Gradient * const colorOverLife,
                         const qreal colorRandomness,
                         QrealAnimator * const speedOverLife,
                         QrealAnimator * const sizeOverLife,
                         QrealAnimator * const opacityOverLife,
                         const bool renderSource,
                         const bool useSprite,
                         const int spriteColorBlendMode,
                         const stdsptr<BoxRenderData>& spriteData,
                         const int spriteSourceMode,
                         const int spriteColumns,
                         const int spriteRows,
                         const int spriteFrames,
                         const qreal spriteFrameMode,
                         const int spritePlaybackMode,
                         QrealAnimator * const spriteFrameOverLife,
                         const qreal spriteFrameRate,
                         const bool spriteRandomStart,
                         const bool orientToMotion,
                         const int maxParticles,
                         const int seed,
                         const bool additive)
        : RasterEffectCaller(HardwareSupport::gpuPreffered, true)
        , mRelFrame(relFrame)
        , mFps(qMax<qreal>(1., fps))
        , mEmitterTarget(emitterTarget)
        , mEmitterPct(emitterPct)
        , mEmitterSizePct(emitterSizePct)
        , mBirthRate(qMax<qreal>(0., birthRate))
        , mLife(qMax<qreal>(0.01, life))
        , mLifeRandomness(qMax<qreal>(0., lifeRandomness))
        , mSpeed(speed)
        , mSpeedRandomness(qMax<qreal>(0., speedRandomness))
        , mDirection(direction)
        , mSpread(qMax<qreal>(0., spread))
        , mGravity(gravity)
        , mDrag(qBound<qreal>(0., drag, 1000.))
        , mStartSize(qMax<qreal>(0.1, startSize))
        , mEndSize(qMax<qreal>(0.1, endSize))
        , mSizeRandomness(qMax<qreal>(0., sizeRandomness))
        , mStartOpacity(qBound<qreal>(0., startOpacity, 1.))
        , mEndOpacity(qBound<qreal>(0., endOpacity, 1.))
        , mOpacityRandomness(qMax<qreal>(0., opacityRandomness))
        , mSoftness(qBound<qreal>(0.05, softness, 1.))
        , mColorMode(colorMode)
        , mSolidColor(solidColor)
        , mColorOverLife(colorOverLife)
        , mColorRandomness(qMax<qreal>(0., colorRandomness))
        , mSpeedOverLife(speedOverLife)
        , mSizeOverLife(sizeOverLife)
        , mOpacityOverLife(opacityOverLife)
        , mRenderSource(renderSource)
        , mUseSprite(useSprite)
        , mSpriteColorBlendMode(spriteColorBlendMode)
        , mSpriteData(spriteData)
        , mSpriteSourceMode(spriteSourceMode)
        , mSpriteColumns(qMax(1, spriteColumns))
        , mSpriteRows(qMax(1, spriteRows))
        , mSpriteFrames(qMax(1, spriteFrames))
        , mSpriteFrameMode(qBound<qreal>(0., spriteFrameMode, 1.))
        , mSpritePlaybackMode(spritePlaybackMode)
        , mSpriteFrameOverLife(spriteFrameOverLife)
        , mSpriteFrameRate(qMax<qreal>(0., spriteFrameRate))
        , mSpriteRandomStart(spriteRandomStart)
        , mOrientToMotion(orientToMotion)
        , mMaxParticles(qMax(1, maxParticles))
        , mSeed(static_cast<uint32_t>(seed))
        , mAdditive(additive)
    {}

    void processGpu(QGL33 * const gl,
                    GpuRenderTools &renderTools) override
    {
        Q_UNUSED(gl)
        renderTools.switchToSkia();
        const auto srcImage = renderTools.requestSrcTextureImageWrapper();
        const auto srcRaster = srcImage ? srcImage->makeRasterImage() : nullptr;
        if(!srcRaster) {
            return;
        }

        SkPixmap srcPixmap;
        if(!srcRaster->peekPixels(&srcPixmap)) {
            return;
        }

        sk_sp<SkImage> spriteRaster = nullptr;
        SkPixmap spritePixmap;
        const SkPixmap* spritePixmapPtr = nullptr;
        if (const auto spriteImage = mSpriteData ? mSpriteData->fRenderedImage : nullptr) {
            spriteRaster = spriteImage->makeRasterImage();
            if (spriteRaster && spriteRaster->peekPixels(&spritePixmap)) {
                spritePixmapPtr = &spritePixmap;
            }
        }

        SkBitmap dstBitmap;
        dstBitmap.allocPixels(srcPixmap.info());
        renderParticles(srcPixmap,
                        dstBitmap,
                        SkIRect::MakeWH(srcPixmap.width(), srcPixmap.height()),
                        renderTools.fGlobalRect.topLeft(),
                        spritePixmapPtr);

        renderTools.requestTargetCanvas()->clear(SK_ColorTRANSPARENT);
        const auto resultImage = SkiaHelpers::transferDataToSkImage(dstBitmap);
        if(resultImage) {
            renderTools.requestTargetCanvas()->drawImage(resultImage, 0, 0);
            renderTools.requestTargetCanvas()->flush();
            renderTools.swapTextures();
        }
    }

    void processCpu(CpuRenderTools& renderTools,
                    const CpuRenderData& data) override
    {
        SkPixmap srcPixmap;
        if(!renderTools.fSrcBtmp.peekPixels(&srcPixmap)) {
            return;
        }

        sk_sp<SkImage> spriteRaster = nullptr;
        SkPixmap spritePixmap;
        const SkPixmap* spritePixmapPtr = nullptr;
        if (const auto spriteImage = mSpriteData ? mSpriteData->fRenderedImage : nullptr) {
            spriteRaster = spriteImage->makeRasterImage();
            if (spriteRaster && spriteRaster->peekPixels(&spritePixmap)) {
                spritePixmapPtr = &spritePixmap;
            }
        }

        renderParticles(srcPixmap, renderTools.fDstBtmp, data.fTexTile, data.fPos, spritePixmapPtr);
    }
protected:
    QMargins getMargin(const SkIRect &srcRect) override
    {
        Q_UNUSED(srcRect)
        const int margin = qCeil(particleTravel(mSpeed,
                                                mSpeedRandomness,
                                                mLife,
                                                mLifeRandomness,
                                                mGravity,
                                                mStartSize,
                                                mEndSize));
        return QMargins(margin, margin, margin, margin);
    }
private:
    void renderParticles(const SkPixmap& src,
                         SkBitmap& dst,
                         const SkIRect& texTile,
                         const QPoint& dataPos,
                         const SkPixmap* const externalSprite) const
    {
        const int xMin = texTile.left();
        const int xMax = texTile.right();
        const int yMin = texTile.top();
        const int yMax = texTile.bottom();
        const int tileWidth = qMax(0, xMax - xMin);

        for (int yi = yMin; yi < yMax; ++yi) {
            auto *dstPx = static_cast<uchar*>(dst.getAddr(0, yi - yMin));
            if (mRenderSource) {
                const auto *srcPx = static_cast<const uchar*>(src.addr(xMin, yi));
                std::memcpy(dstPx, srcPx, static_cast<size_t>(tileWidth) * 4u);
            } else {
                std::memset(dstPx, 0, static_cast<size_t>(tileWidth) * 4u);
            }
        }

        const int srcLeft = fSrcRect.left() - dataPos.x();
        const int srcTop = fSrcRect.top() - dataPos.y();
        const int srcWidth = fSrcRect.width();
        const int srcHeight = fSrcRect.height();

        const qreal emitterHalfWidth =
                qAbs(srcWidth * mEmitterSizePct.x() * 0.01) * 0.5;
        const qreal emitterHalfHeight =
                qAbs(srcHeight * mEmitterSizePct.y() * 0.01) * 0.5;
        const qreal currentTime = mRelFrame / mFps;

        const qreal maxLife = mLife * (1. + mLifeRandomness * 0.01);
        const int firstIndex = qMax(0, qFloor((currentTime - maxLife) * mBirthRate));
        const int lastIndex = qMax(firstIndex, qCeil(currentTime * mBirthRate));
        const int startIndex = qMax(firstIndex, lastIndex - mMaxParticles);

        for (int particleIndex = startIndex; particleIndex < lastIndex; ++particleIndex) {
            const qreal emissionTime = particleIndex / qMax<qreal>(1., mBirthRate);
            const qreal age = currentTime - emissionTime;
            if (age < 0.) continue;

            const qreal randAngle = hash01(particleIndex, mSeed + 11u);
            const qreal randSpeed = hash01(particleIndex, mSeed + 23u);
            const qreal randLife = hash01(particleIndex, mSeed + 31u);
            const qreal randSize = hash01(particleIndex, mSeed + 37u);
            const qreal randAlpha = hash01(particleIndex, mSeed + 51u);
            const qreal randOffsetX = hash01(particleIndex, mSeed + 67u);
            const qreal randOffsetY = hash01(particleIndex, mSeed + 79u);
            const qreal randColorR = hash01(particleIndex, mSeed + 97u);
            const qreal randColorG = hash01(particleIndex, mSeed + 109u);
            const qreal randColorB = hash01(particleIndex, mSeed + 131u);

            const qreal particleLife = applyRandomness(mLife, mLifeRandomness, randLife, 0.01);
            if (age > particleLife) continue;

            const qreal t = qBound<qreal>(0., age / particleLife, 1.);
            const qreal direction = qDegreesToRadians(mDirection + (randAngle - 0.5) * mSpread);
            const qreal speed = applyRandomness(mSpeed, mSpeedRandomness, randSpeed, 0.);
            const qreal speedCurve = qMax<qreal>(0., sampleOverLifeCurve(mSpeedOverLife, t));
            const qreal dragFactor = mDrag > 0. ? qExp(-(mDrag * 0.01) * age) : 1.;
            const qreal shapedSpeed = speed * speedCurve;
            const qreal vx = qCos(direction) * shapedSpeed;
            const qreal vy = qSin(direction) * shapedSpeed;

            qreal emitterX = srcLeft + srcWidth * mEmitterPct.x() * 0.01;
            qreal emitterY = srcTop + srcHeight * mEmitterPct.y() * 0.01;
            if (mEmitterTarget) {
                const qreal emissionRelFrame = mRelFrame - age * mFps;
                const QPointF targetAbs = mEmitterTarget->getPivotAbsPos(emissionRelFrame);
                const qreal centeredOffsetX =
                        srcWidth * (mEmitterPct.x() - 50.) * 0.01;
                const qreal centeredOffsetY =
                        srcHeight * (mEmitterPct.y() - 50.) * 0.01;
                emitterX = targetAbs.x() - dataPos.x() + centeredOffsetX;
                emitterY = targetAbs.y() - dataPos.y() + centeredOffsetY;
            }
            const qreal startX = emitterX + centeredRandom(randOffsetX) * emitterHalfWidth;
            const qreal startY = emitterY + centeredRandom(randOffsetY) * emitterHalfHeight;

            const qreal px = startX + vx * age * dragFactor;
            const qreal py = startY + vy * age * dragFactor + 0.5 * mGravity * age * age;
            const qreal sizeScale = applyRandomness(1., mSizeRandomness, randSize, 0.05);
            const qreal alphaScale = applyRandomness(1., mOpacityRandomness, randAlpha, 0.);
            const qreal radius = qMax<qreal>(0.25, lerp(mStartSize, mEndSize, t) * sizeScale);
            const qreal alpha = qBound<qreal>(0., lerp(mStartOpacity, mEndOpacity, t) * alphaScale, 1.);
            const qreal sizeCurve = qMax<qreal>(0., sampleOverLifeCurve(mSizeOverLife, t));
            const qreal opacityCurve = qMax<qreal>(0., sampleOverLifeCurve(mOpacityOverLife, t));
            const qreal finalRadius = qMax<qreal>(0.25, radius * sizeCurve);
            const qreal finalAlpha = qBound<qreal>(0., alpha * opacityCurve, 1.);
            if (finalRadius <= 0.25 || finalAlpha <= 0.001) continue;

            const ParticleColor color = applyColorVariance(
                        mColorMode == ParticleColorMode::gradient && mColorOverLife ?
                            sampleGradientColor(mColorOverLife, t) :
                            mSolidColor,
                        mColorRandomness,
                        randColorR,
                        randColorG,
                        randColorB);

            const qreal angle = mOrientToMotion ? qAtan2(vy + mGravity * age, vx) : 0.;
            const int frameIndex = [this, t, age, particleIndex]() {
                if(!mUseSprite) return 0;
                if(mSpriteSourceMode == SpriteSourceMode::singleSprite) return 0;
                const int availableFrames = qMin(mSpriteFrames, mSpriteColumns * mSpriteRows);
                if(availableFrames <= 1) return 0;
                const int randomStart = mSpriteRandomStart ?
                            qBound(0,
                                   qFloor(hash01(particleIndex, mSeed + 163u) *
                                          static_cast<qreal>(availableFrames)),
                                   availableFrames - 1) :
                            0;
                if(mSpriteFrameMode == SpriteFrameMode::overLife) {
                    const qreal frameCurve = qBound<qreal>(
                                0.,
                                sampleOverLifeCurve(mSpriteFrameOverLife, t),
                                1.);
                    int frame = qFloor(frameCurve * static_cast<qreal>(availableFrames));
                    frame += randomStart;
                    return applyPlaybackMode(frame, availableFrames, mSpritePlaybackMode);
                }
                const int frame = qFloor(age * mSpriteFrameRate) + randomStart;
                return applyPlaybackMode(frame, availableFrames, mSpritePlaybackMode);
            }();

            const int left = qMax(xMin, qFloor(px - finalRadius));
            const int top = qMax(yMin, qFloor(py - finalRadius));
            const int right = qMin(xMax, qCeil(px + finalRadius));
            const int bottom = qMin(yMax, qCeil(py + finalRadius));
            if (left >= right || top >= bottom) continue;

            const SpriteFrameRect spriteFrame = getSpriteFrameRect(
                        mSpriteSourceMode == SpriteSourceMode::singleSprite ? 0 : frameIndex,
                        mSpriteSourceMode == SpriteSourceMode::singleSprite ? 1 : mSpriteColumns,
                        mSpriteSourceMode == SpriteSourceMode::singleSprite ? 1 : mSpriteRows,
                        externalSprite ? externalSprite->width() : src.width(),
                        externalSprite ? externalSprite->height() : src.height());

            for (int yi = top; yi < bottom; ++yi) {
                for (int xi = left; xi < right; ++xi) {
                    const qreal dx = (xi + 0.5) - px;
                    const qreal dy = (yi + 0.5) - py;
                    const qreal cosA = qCos(-angle);
                    const qreal sinA = qSin(-angle);
                    const qreal localX = mOrientToMotion ? dx * cosA - dy * sinA : dx;
                    const qreal localY = mOrientToMotion ? dx * sinA + dy * cosA : dy;

                    qreal intensity = finalAlpha;
                    if (mUseSprite) {
                        const qreal normX = qAbs(localX) / finalRadius;
                        const qreal normY = qAbs(localY) / finalRadius;
                        if (normX >= 1. || normY >= 1.) continue;
                    } else {
                        const qreal dist = qSqrt(localX*localX + localY*localY) / finalRadius;
                        if (dist >= 1.) continue;
                        const qreal falloff = qPow(1. - dist, 1. / mSoftness);
                        intensity = qBound<qreal>(0., finalAlpha * falloff, 1.);
                    }

                    auto *dstPixel = static_cast<uchar*>(dst.getAddr(xi - xMin, yi - yMin));
                    qreal spriteR = color.r;
                    qreal spriteG = color.g;
                    qreal spriteB = color.b;
                    qreal spriteAlpha = 1.;
                    if (mUseSprite) {
                        const qreal u = qBound<qreal>(0., (localX / finalRadius + 1.) * 0.5, 1.);
                        const qreal v = qBound<qreal>(0., (localY / finalRadius + 1.) * 0.5, 1.);
                        const int sampleX = qBound(spriteFrame.left,
                                                   spriteFrame.left + qFloor(u * (spriteFrame.width - 1)),
                                                   spriteFrame.left + spriteFrame.width - 1);
                        const int sampleY = qBound(spriteFrame.top,
                                                   spriteFrame.top + qFloor(v * (spriteFrame.height - 1)),
                                                   spriteFrame.top + spriteFrame.height - 1);
                        const auto *srcPixel = externalSprite ?
                                    static_cast<const uchar*>(externalSprite->addr(sampleX, sampleY)) :
                                    static_cast<const uchar*>(src.addr(sampleX, sampleY));
                        const qreal texB = srcPixel[0] / 255.;
                        const qreal texG = srcPixel[1] / 255.;
                        const qreal texR = srcPixel[2] / 255.;
                        if (mSpriteColorBlendMode == SpriteColorBlendMode::normal) {
                            spriteB = texB;
                            spriteG = texG;
                            spriteR = texR;
                        } else if (mSpriteColorBlendMode == SpriteColorBlendMode::multiply) {
                            spriteB *= texB;
                            spriteG *= texG;
                            spriteR *= texR;
                        } else if (mSpriteColorBlendMode == SpriteColorBlendMode::overlay) {
                            spriteB = overlayBlendChannel(texB, spriteB);
                            spriteG = overlayBlendChannel(texG, spriteG);
                            spriteR = overlayBlendChannel(texR, spriteR);
                        } else if (mSpriteColorBlendMode == SpriteColorBlendMode::add) {
                            spriteB = qMin<qreal>(1., texB + spriteB);
                            spriteG = qMin<qreal>(1., texG + spriteG);
                            spriteR = qMin<qreal>(1., texR + spriteR);
                        }
                        spriteAlpha = srcPixel[3] / 255.;
                        if (spriteAlpha <= 0.001) continue;
                    }

                    const qreal shadedIntensity = intensity * spriteAlpha;
                    if (mAdditive) {
                        dstPixel[0] = static_cast<uchar>(qMin(255, dstPixel[0] + qRound(255. * shadedIntensity * spriteB)));
                        dstPixel[1] = static_cast<uchar>(qMin(255, dstPixel[1] + qRound(255. * shadedIntensity * spriteG)));
                        dstPixel[2] = static_cast<uchar>(qMin(255, dstPixel[2] + qRound(255. * shadedIntensity * spriteR)));
                        dstPixel[3] = static_cast<uchar>(qMin(255, dstPixel[3] + qRound(255. * shadedIntensity)));
                    } else {
                        const qreal inv = 1. - shadedIntensity;
                        dstPixel[0] = static_cast<uchar>(qBound(0, qRound(dstPixel[0] * inv + 255. * spriteB * shadedIntensity), 255));
                        dstPixel[1] = static_cast<uchar>(qBound(0, qRound(dstPixel[1] * inv + 255. * spriteG * shadedIntensity), 255));
                        dstPixel[2] = static_cast<uchar>(qBound(0, qRound(dstPixel[2] * inv + 255. * spriteR * shadedIntensity), 255));
                        dstPixel[3] = static_cast<uchar>(qBound(0, qRound(dstPixel[3] * inv + 255. * shadedIntensity), 255));
                    }
                }
            }
        }
    }

    const qreal mRelFrame;
    const qreal mFps;
    qptr<BoundingBox> mEmitterTarget;
    const QPointF mEmitterPct;
    const QPointF mEmitterSizePct;
    const qreal mBirthRate;
    const qreal mLife;
    const qreal mLifeRandomness;
    const qreal mSpeed;
    const qreal mSpeedRandomness;
    const qreal mDirection;
    const qreal mSpread;
    const qreal mGravity;
    const qreal mDrag;
    const qreal mStartSize;
    const qreal mEndSize;
    const qreal mSizeRandomness;
    const qreal mStartOpacity;
    const qreal mEndOpacity;
    const qreal mOpacityRandomness;
    const qreal mSoftness;
    const int mColorMode;
    const ParticleColor mSolidColor;
    Gradient * const mColorOverLife;
    const qreal mColorRandomness;
    QrealAnimator * const mSpeedOverLife;
    QrealAnimator * const mSizeOverLife;
    QrealAnimator * const mOpacityOverLife;
    const bool mRenderSource;
    const bool mUseSprite;
    const int mSpriteColorBlendMode;
    const stdsptr<BoxRenderData> mSpriteData;
    const int mSpriteSourceMode;
    const int mSpriteColumns;
    const int mSpriteRows;
    const int mSpriteFrames;
    const qreal mSpriteFrameMode;
    const int mSpritePlaybackMode;
    QrealAnimator * const mSpriteFrameOverLife;
    const qreal mSpriteFrameRate;
    const bool mSpriteRandomStart;
    const bool mOrientToMotion;
    const int mMaxParticles;
    const uint32_t mSeed;
    const bool mAdditive;
};

}

ParticleSystemEffect::ParticleSystemEffect()
    : RasterEffect("particle system",
                   AppSupport::getRasterEffectHardwareSupport("ParticleSystem",
                                                              HardwareSupport::gpuPreffered),
                   false,
                   RasterEffectType::PARTICLE_SYSTEM)
{
    mEmitterGroup = enve::make_shared<ParticleUiGroupAnimator>("emitter");
    mMotionGroup = enve::make_shared<ParticleUiGroupAnimator>("motion");
    mOverLifeGroup = enve::make_shared<ParticleUiGroupAnimator>("over life");
    mRenderGroup = enve::make_shared<ParticleUiGroupAnimator>("render");
    mSpriteGroup = enve::make_shared<ParticleUiGroupAnimator>("sprite");
    mSpriteSourceGroup = enve::make_shared<ParticleUiGroupAnimator>("source");
    mSpriteSheetGroup = enve::make_shared<ParticleUiGroupAnimator>("sheet");
    mSpritePlaybackGroup = enve::make_shared<ParticleUiGroupAnimator>("playback");
    mPerfGroup = enve::make_shared<ParticleUiGroupAnimator>("performance");

    mEmitter = enve::make_shared<QPointFAnimator>("emitter");
    mEmitterSize = enve::make_shared<QPointFAnimator>("emitter size");
    mEmitterTarget = enve::make_shared<BoxTargetProperty>("emitter target");
    mBirthRate = enve::make_shared<QrealAnimator>(30, 0, 2000, 1, "birth rate");
    mLife = enve::make_shared<QrealAnimator>(1.5, 0.05, 30, 0.05, "life");
    mLifeRandomness = enve::make_shared<QrealAnimator>(25, 0, 100, 1, "life randomness");
    mSpeed = enve::make_shared<QrealAnimator>(180, 0, 4000, 1, "speed");
    mSpeedRandomness = enve::make_shared<QrealAnimator>(35, 0, 100, 1, "speed randomness");
    mDirection = enve::make_shared<QrealAnimator>(270, -3600, 3600, 1, "direction");
    mSpread = enve::make_shared<QrealAnimator>(45, 0, 360, 1, "spread");
    mGravity = enve::make_shared<QrealAnimator>(120, -4000, 4000, 1, "gravity");
    mDrag = enve::make_shared<QrealAnimator>(0, 0, 200, 1, "drag");
    mStartSize = enve::make_shared<QrealAnimator>(12, 0.1, 400, 0.5, "start size");
    mEndSize = enve::make_shared<QrealAnimator>(2, 0.1, 400, 0.5, "end size");
    mSizeRandomness = enve::make_shared<QrealAnimator>(40, 0, 100, 1, "size randomness");
    mStartOpacity = enve::make_shared<QrealAnimator>(90, 0, 100, 1, "start opacity");
    mEndOpacity = enve::make_shared<QrealAnimator>(0, 0, 100, 1, "end opacity");
    mOpacityRandomness = enve::make_shared<QrealAnimator>(20, 0, 100, 1, "opacity randomness");
    mSpeedOverLife = enve::make_shared<ParticleOverLifeAnimator>(1, "speed over life");
    mSoftness = enve::make_shared<QrealAnimator>(65, 5, 100, 1, "softness");
    mSizeOverLife = enve::make_shared<ParticleOverLifeAnimator>(1, "size over life");
    mOpacityOverLife = enve::make_shared<ParticleOverLifeAnimator>(1, "opacity over life");
    mAdditive = enve::make_shared<ParticleBoolAnimator>("additive");
    mRenderSource = enve::make_shared<ParticleBoolAnimator>("render source");
    mColorMode = enve::make_shared<ParticleColorMode>();
    mSolidColor = enve::make_shared<ColorAnimator>("color");
    mColorOverLife = enve::make_shared<Gradient>();
    mColorRandomness = enve::make_shared<QrealAnimator>(0, 0, 100, 1, "color randomness");
    mUseSprite = enve::make_shared<ParticleBoolAnimator>("use sprite");
    mSpriteColorBlendMode = enve::make_shared<SpriteColorBlendMode>();
    mSpriteSource = enve::make_shared<BoxTargetProperty>("sprite source");
    mSpriteSourceMode = enve::make_shared<SpriteSourceMode>();
    mSpriteSourceTimeMode = enve::make_shared<SpriteSourceTimeMode>();
    mSpriteSourceFrame = enve::make_shared<QrealAnimator>(0, -100000, 100000, 1, "source frame");
    mSpriteSourceStartFrame = enve::make_shared<QrealAnimator>(0, -100000, 100000, 1, "source start");
    mSpriteSourceEndFrame = enve::make_shared<QrealAnimator>(0, -100000, 100000, 1, "source end");
    mSpriteSourcePlaybackMode = enve::make_shared<SpriteSourcePlaybackMode>();
    mSpriteSourceFrameRate = enve::make_shared<QrealAnimator>(24, 0, 240, 1, "source frame rate");
    mSpriteColumns = enve::make_shared<IntAnimator>(1, 1, 32, 1, "columns");
    mSpriteRows = enve::make_shared<IntAnimator>(1, 1, 32, 1, "rows");
    mSpriteFrames = enve::make_shared<IntAnimator>(1, 1, 256, 1, "frame count");
    mSpriteFrameMode = enve::make_shared<SpriteFrameMode>();
    mSpritePlaybackMode = enve::make_shared<SpritePlaybackMode>();
    mSpriteFrameOverLife = enve::make_shared<ParticleOverLifeAnimator>(1, "frame over life");
    mSpriteFrameRate = enve::make_shared<QrealAnimator>(24, 0, 240, 1, "frame rate");
    mSpriteRandomStart = enve::make_shared<ParticleBoolAnimator>("random start");
    mOrientToMotion = enve::make_shared<ParticleBoolAnimator>("orient to motion");
    mRenderSource->setCurrentBoolValue(true);
    mMaxParticles = enve::make_shared<IntAnimator>(2500, 1, 20000, 50, "max particles");
    mSeed = enve::make_shared<IntAnimator>(1, 0, 999999, 1, "seed");

    mEmitter->setValuesRange(-500, 500);
    mEmitter->setBaseValue(50, 50);
    mEmitterSize->setValuesRange(0, 500);
    mEmitterSize->setBaseValue(0, 0);
    mSolidColor->setColor(QColor(255, 255, 255, 255));
    mColorOverLife->addColor(QColor(255, 255, 255, 255));
    mColorOverLife->addColor(QColor(255, 255, 255, 255));
    mSpeedOverLife->setCurrentBaseValue(1);
    mSizeOverLife->setCurrentBaseValue(1);
    mOpacityOverLife->setCurrentBaseValue(1);

    mSpeedOverLife->anim_addKeyAtRelFrame(0);
    mSpeedOverLife->setCurrentBaseValue(1);
    mSpeedOverLife->anim_addKeyAtRelFrame(100);

    mSizeOverLife->anim_addKeyAtRelFrame(0);
    mSizeOverLife->setCurrentBaseValue(1);
    mSizeOverLife->anim_addKeyAtRelFrame(100);

    mOpacityOverLife->anim_addKeyAtRelFrame(0);
    mOpacityOverLife->setCurrentBaseValue(1);
    mOpacityOverLife->anim_addKeyAtRelFrame(75);
    mOpacityOverLife->setCurrentBaseValue(0);
    mOpacityOverLife->anim_addKeyAtRelFrame(100);

    mSpriteFrameOverLife->anim_addKeyAtRelFrame(0);
    mSpriteFrameOverLife->setCurrentBaseValue(0);
    mSpriteFrameOverLife->anim_addKeyAtRelFrame(100);
    mSpriteFrameOverLife->setCurrentBaseValue(1);
    mEmitterGroup->ca_setGUIProperty(mBirthRate.get());
    mMotionGroup->ca_setGUIProperty(mSpeed.get());
    mOverLifeGroup->ca_setGUIProperty(mSizeOverLife.get());
    mRenderGroup->ca_setGUIProperty(mColorMode.get());
    mSpriteGroup->ca_setGUIProperty(mSpriteSource.get());
    mSpriteSourceGroup->ca_setGUIProperty(mSpriteSource.get());
    mSpriteSheetGroup->ca_setGUIProperty(mSpriteColumns.get());
    mSpritePlaybackGroup->ca_setGUIProperty(mSpriteFrameMode.get());
    mPerfGroup->ca_setGUIProperty(mMaxParticles.get());

    ca_addChild(mEmitterGroup);
    mEmitterGroup->ca_addChild(mEmitter);
    mEmitterGroup->ca_addChild(mEmitterSize);
    mEmitterGroup->ca_addChild(mEmitterTarget);
    mEmitterGroup->ca_addChild(mBirthRate);
    mEmitterGroup->ca_addChild(mLife);
    mEmitterGroup->ca_addChild(mLifeRandomness);

    ca_addChild(mMotionGroup);
    mMotionGroup->ca_addChild(mSpeed);
    mMotionGroup->ca_addChild(mSpeedRandomness);
    mMotionGroup->ca_addChild(mDirection);
    mMotionGroup->ca_addChild(mSpread);
    mMotionGroup->ca_addChild(mGravity);
    mMotionGroup->ca_addChild(mDrag);

    ca_addChild(mOverLifeGroup);
    mOverLifeGroup->ca_addChild(mStartSize);
    mOverLifeGroup->ca_addChild(mEndSize);
    mOverLifeGroup->ca_addChild(mSizeRandomness);
    mOverLifeGroup->ca_addChild(mSpeedOverLife);
    mOverLifeGroup->ca_addChild(mSizeOverLife);
    mOverLifeGroup->ca_addChild(mStartOpacity);
    mOverLifeGroup->ca_addChild(mEndOpacity);
    mOverLifeGroup->ca_addChild(mOpacityRandomness);
    mOverLifeGroup->ca_addChild(mOpacityOverLife);

    ca_addChild(mRenderGroup);
    mRenderGroup->ca_addChild(mColorMode);
    mRenderGroup->ca_addChild(mSolidColor);
    mRenderGroup->ca_addChild(mColorOverLife);
    mRenderGroup->ca_addChild(mColorRandomness);
    mRenderGroup->ca_addChild(mSoftness);
    mRenderGroup->ca_addChild(mAdditive);
    mRenderGroup->ca_addChild(mRenderSource);

    ca_addChild(mSpriteGroup);
    mSpriteGroup->ca_addChild(mUseSprite);
    mSpriteGroup->ca_addChild(mSpriteColorBlendMode);
    mSpriteGroup->ca_addChild(mSpriteSourceGroup);
    mSpriteGroup->ca_addChild(mSpriteSheetGroup);
    mSpriteGroup->ca_addChild(mSpritePlaybackGroup);

    mSpriteSourceGroup->ca_addChild(mSpriteSource);
    mSpriteSourceGroup->ca_addChild(mSpriteSourceMode);
    mSpriteSourceGroup->ca_addChild(mSpriteSourceTimeMode);
    mSpriteSourceGroup->ca_addChild(mSpriteSourceFrame);
    mSpriteSourceGroup->ca_addChild(mSpriteSourceStartFrame);
    mSpriteSourceGroup->ca_addChild(mSpriteSourceEndFrame);
    mSpriteSourceGroup->ca_addChild(mSpriteSourcePlaybackMode);
    mSpriteSourceGroup->ca_addChild(mSpriteSourceFrameRate);

    mSpriteSheetGroup->ca_addChild(mSpriteColumns);
    mSpriteSheetGroup->ca_addChild(mSpriteRows);
    mSpriteSheetGroup->ca_addChild(mSpriteFrames);

    mSpritePlaybackGroup->ca_addChild(mSpriteFrameMode);
    mSpritePlaybackGroup->ca_addChild(mSpritePlaybackMode);
    mSpritePlaybackGroup->ca_addChild(mSpriteFrameOverLife);
    mSpritePlaybackGroup->ca_addChild(mSpriteFrameRate);
    mSpritePlaybackGroup->ca_addChild(mSpriteRandomStart);
    mSpritePlaybackGroup->ca_addChild(mOrientToMotion);

    ca_addChild(mPerfGroup);
    mPerfGroup->ca_addChild(mMaxParticles);
    mPerfGroup->ca_addChild(mSeed);

    connect(mSpriteSource.get(), &BoxTargetProperty::targetSet,
            this, [this](BoundingBox* const target) {
        if(!target) return;
        connect(target, &Property::prp_currentFrameChanged,
                this, &Property::prp_currentFrameChanged,
                Qt::UniqueConnection);
        connect(target, &Property::prp_absFrameRangeChanged,
                this, &Property::prp_afterChangedAbsRange,
                Qt::UniqueConnection);
    });

    const auto refreshSpriteUi = [this]() {
        updateSpriteUiState();
    };
    connect(mUseSprite.get(), &QrealAnimator::baseValueChanged,
            this, [refreshSpriteUi](qreal) { refreshSpriteUi(); });
    connect(mColorMode.get(), &QrealAnimator::baseValueChanged,
            this, [refreshSpriteUi](qreal) { refreshSpriteUi(); });
    connect(mSpriteSourceMode.get(), &QrealAnimator::baseValueChanged,
            this, [refreshSpriteUi](qreal) { refreshSpriteUi(); });
    connect(mSpriteSourceTimeMode.get(), &QrealAnimator::baseValueChanged,
            this, [refreshSpriteUi](qreal) { refreshSpriteUi(); });
    connect(mSpriteSourcePlaybackMode.get(), &QrealAnimator::baseValueChanged,
            this, [refreshSpriteUi](qreal) { refreshSpriteUi(); });
    connect(mSpriteFrameMode.get(), &QrealAnimator::baseValueChanged,
            this, [refreshSpriteUi](qreal) { refreshSpriteUi(); });
    connect(mSpritePlaybackMode.get(), &QrealAnimator::baseValueChanged,
            this, [refreshSpriteUi](qreal) { refreshSpriteUi(); });

    updateSpriteUiState();
}

void ParticleSystemEffect::updateSpriteUiState()
{
    const bool useSprite = mUseSprite->getBoolValue();
    const bool useGradient =
            mColorMode->getCurrentValue() == ParticleColorMode::gradient;
    const bool useSheet =
            mSpriteSourceMode->getCurrentValue() == SpriteSourceMode::spriteSheet;
    const int sourceTimeMode = mSpriteSourceTimeMode->getCurrentValue();
    const bool showSourceFrame =
            sourceTimeMode == SpriteSourceTimeMode::compTime ||
            sourceTimeMode == SpriteSourceTimeMode::stillFrame ||
            sourceTimeMode == SpriteSourceTimeMode::localPlayback;
    const bool showSourceFrameRate =
            sourceTimeMode == SpriteSourceTimeMode::localPlayback;
    const bool showSourceRange =
            sourceTimeMode == SpriteSourceTimeMode::compTime ||
            sourceTimeMode == SpriteSourceTimeMode::localPlayback;

    setPropertyVisible(mColorMode.get(), true);
    setPropertyVisible(mSolidColor.get(), !useGradient);
    setPropertyVisible(mColorOverLife.get(), useGradient);
    setPropertyVisible(mSpriteSourceGroup.get(), useSprite);
    setPropertyVisible(mSpriteSheetGroup.get(), useSprite && useSheet);
    setPropertyVisible(mSpritePlaybackGroup.get(), useSprite);

    setPropertyVisible(mSpriteColorBlendMode.get(), useSprite);
    setPropertyVisible(mSpriteSource.get(), useSprite);
    setPropertyVisible(mSpriteSourceMode.get(), useSprite);
    setPropertyVisible(mSpriteSourceTimeMode.get(), useSprite);
    setPropertyVisible(mSpriteSourceFrame.get(), useSprite && showSourceFrame);
    setPropertyVisible(mSpriteSourceStartFrame.get(), useSprite && showSourceRange);
    setPropertyVisible(mSpriteSourceEndFrame.get(), useSprite && showSourceRange);
    setPropertyVisible(mSpriteSourcePlaybackMode.get(), useSprite && showSourceRange);
    setPropertyVisible(mSpriteSourceFrameRate.get(), useSprite && showSourceFrameRate);

    setPropertyVisible(mSpriteColumns.get(), useSprite && useSheet);
    setPropertyVisible(mSpriteRows.get(), useSprite && useSheet);
    setPropertyVisible(mSpriteFrames.get(), useSprite && useSheet);

    setPropertyVisible(mSpriteFrameMode.get(), useSprite && useSheet);
    setPropertyVisible(mSpritePlaybackMode.get(), useSprite && useSheet);
    setPropertyVisible(mSpriteFrameOverLife.get(),
                       useSprite && useSheet &&
                       mSpriteFrameMode->getCurrentValue() == SpriteFrameMode::overLife);
    setPropertyVisible(mSpriteFrameRate.get(),
                       useSprite && useSheet &&
                       mSpriteFrameMode->getCurrentValue() == SpriteFrameMode::frameRate);
    setPropertyVisible(mSpriteRandomStart.get(), useSprite && useSheet);
    setPropertyVisible(mOrientToMotion.get(), useSprite);

    mRenderGroup->ca_setGUIProperty(useGradient ?
                                        static_cast<Property*>(mColorOverLife.get()) :
                                        static_cast<Property*>(mSolidColor.get()));
    mSpriteSourceGroup->ca_setGUIProperty(static_cast<Property*>(mSpriteSource.get()));
    mSpriteSheetGroup->ca_setGUIProperty(mSpriteColumns.get());
    if (useSheet) {
        mSpritePlaybackGroup->ca_setGUIProperty(
                    mSpriteFrameMode->getCurrentValue() == SpriteFrameMode::overLife ?
                        static_cast<Property*>(mSpriteFrameOverLife.get()) :
                        static_cast<Property*>(mSpritePlaybackMode.get()));
    } else {
        mSpritePlaybackGroup->ca_setGUIProperty(
                    static_cast<Property*>(mOrientToMotion.get()));
    }
    mSpriteGroup->ca_setGUIProperty(useSprite ?
                                        static_cast<Property*>(mSpriteColorBlendMode.get()) :
                                        static_cast<Property*>(mUseSprite.get()));
}

FrameRange ParticleSystemEffect::prp_getIdenticalRelRange(const int relFrame) const
{
    Q_UNUSED(relFrame)
    return {relFrame, relFrame};
}

QMargins ParticleSystemEffect::getMargin() const
{
    auto *box = getFirstAncestor<BoundingBox>();
    auto *scene = box ? box->getParentScene() : nullptr;
    const qreal resolution = scene ? scene->getResolution() : 1.;
    const int margin = qCeil(particleTravel(mSpeed->getEffectiveValue() * resolution,
                                            mSpeedRandomness->getEffectiveValue(),
                                            mLife->getEffectiveValue(),
                                            mLifeRandomness->getEffectiveValue(),
                                            mGravity->getEffectiveValue() * resolution,
                                            mStartSize->getEffectiveValue() * resolution,
                                            mEndSize->getEffectiveValue() * resolution));
    return QMargins(margin, margin, margin, margin);
}

stdsptr<RasterEffectCaller> ParticleSystemEffect::getEffectCaller(
        const qreal relFrame,
        const qreal resolution,
        const qreal influence,
        BoxRenderData * const data) const
{
    Q_UNUSED(data)

    if(!PluginManager::isEnabled(PluginFeature::particleSystem)) {
        return nullptr;
    }

    const qreal birthRate = mBirthRate->getEffectiveValue(relFrame) * influence;
    const qreal startOpacity = mStartOpacity->getEffectiveValue(relFrame) * 0.01 * influence;
    const qreal endOpacity = mEndOpacity->getEffectiveValue(relFrame) * 0.01 * influence;
    if (isZero4Dec(birthRate) ||
        (isZero4Dec(startOpacity) && isZero4Dec(endOpacity))) {
        return nullptr;
    }

    auto *box = getFirstAncestor<BoundingBox>();
    auto *scene = box ? box->getParentScene() : nullptr;
    const qreal fps = scene ? scene->getFps() : 24.;
    stdsptr<BoxRenderData> spriteData = nullptr;
    if(mUseSprite->getBoolValue(relFrame)) {
        auto *spriteSource = mSpriteSource->getTarget();
        auto *owner = getFirstAncestor<BoundingBox>();
        if(spriteSource && spriteSource != owner) {
            const qreal sourceFrameControl =
                    mSpriteSourceFrame->getEffectiveValue(relFrame);
            const qreal sourceStartFrame =
                    mSpriteSourceStartFrame->getEffectiveValue(relFrame);
            const qreal sourceEndFrame =
                    mSpriteSourceEndFrame->getEffectiveValue(relFrame);
            const qreal sourceFrameRate =
                    mSpriteSourceFrameRate->getEffectiveValue(relFrame);
            qreal spriteRelFrame = 0.;
            const int timeMode = mSpriteSourceTimeMode->getCurrentValue();
            if (timeMode == SpriteSourceTimeMode::stillFrame) {
                spriteRelFrame = sourceFrameControl;
            } else if (timeMode == SpriteSourceTimeMode::localPlayback) {
                spriteRelFrame =
                    sourceFrameControl +
                    relFrame * (sourceFrameRate / qMax<qreal>(1., fps));
            } else {
                const qreal absFrame = prp_relFrameToAbsFrameF(relFrame);
                spriteRelFrame =
                    spriteSource->prp_absFrameToRelFrameF(absFrame) +
                    sourceFrameControl;
            }
            if (sourceEndFrame > sourceStartFrame) {
                spriteRelFrame = applyPlaybackModeF(
                            spriteRelFrame,
                            sourceStartFrame,
                            sourceEndFrame,
                            mSpriteSourcePlaybackMode->getCurrentValue());
            }
            spriteData = spriteSource->getCurrentRenderData(spriteRelFrame);
            if(!spriteData) {
                spriteData = spriteSource->queExternalRender(spriteRelFrame, true);
            }
            if(spriteData && data && !spriteData->finished()) {
                spriteData->addDependent(data);
            }
        }
    }

    return enve::make_shared<ParticleSystemCaller>(
                relFrame,
                fps,
                mEmitterTarget->getTarget(),
                mEmitter->getEffectiveValue(relFrame),
                mEmitterSize->getEffectiveValue(relFrame),
                birthRate,
                mLife->getEffectiveValue(relFrame),
                mLifeRandomness->getEffectiveValue(relFrame),
                mSpeed->getEffectiveValue(relFrame) * resolution,
                mSpeedRandomness->getEffectiveValue(relFrame),
                mDirection->getEffectiveValue(relFrame),
                mSpread->getEffectiveValue(relFrame),
                mGravity->getEffectiveValue(relFrame) * resolution,
                mDrag->getEffectiveValue(relFrame),
                mStartSize->getEffectiveValue(relFrame) * resolution,
                mEndSize->getEffectiveValue(relFrame) * resolution,
                mSizeRandomness->getEffectiveValue(relFrame),
                startOpacity,
                endOpacity,
                mOpacityRandomness->getEffectiveValue(relFrame),
                mSoftness->getEffectiveValue(relFrame) * 0.01,
                mColorMode->getCurrentValue(),
                toParticleColor(mSolidColor->getColor(relFrame)),
                mColorOverLife.get(),
                mColorRandomness->getEffectiveValue(relFrame),
                mSpeedOverLife.get(),
                mSizeOverLife.get(),
                mOpacityOverLife.get(),
                mRenderSource->getBoolValue(relFrame),
                mUseSprite->getBoolValue(relFrame),
                mSpriteColorBlendMode->getCurrentValue(),
                spriteData,
                mSpriteSourceMode->getCurrentValue(),
                mSpriteColumns->getEffectiveIntValue(relFrame),
                mSpriteRows->getEffectiveIntValue(relFrame),
                mSpriteFrames->getEffectiveIntValue(relFrame),
                mSpriteFrameMode->getCurrentValue(),
                mSpritePlaybackMode->getCurrentValue(),
                mSpriteFrameOverLife.get(),
                mSpriteFrameRate->getEffectiveValue(relFrame),
                mSpriteRandomStart->getBoolValue(relFrame),
                mOrientToMotion->getBoolValue(relFrame),
                mMaxParticles->getEffectiveIntValue(relFrame),
                mSeed->getEffectiveIntValue(relFrame),
                mAdditive->getBoolValue(relFrame));
}
