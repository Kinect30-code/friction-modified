#include "blindseffect.h"

#include "Animators/qrealanimator.h"
#include "appsupport.h"

#include <QtMath>
#include <cmath>

namespace {

qreal positiveMod(const qreal value, const qreal divisor)
{
    if (qFuzzyIsNull(divisor)) {
        return 0.;
    }
    qreal mod = std::fmod(value, divisor);
    if (mod < 0.) {
        mod += divisor;
    }
    return mod;
}

class BlindsEffectCaller : public RasterEffectCaller {
public:
    BlindsEffectCaller(const qreal completion,
                       const qreal direction,
                       const qreal width,
                       const qreal feather)
        : RasterEffectCaller(HardwareSupport::cpuOnly)
        , mCompletion(completion)
        , mDirection(direction)
        , mWidth(qMax<qreal>(1., width))
        , mFeather(qBound<qreal>(0., feather, 1.))
    {}

    void processCpu(CpuRenderTools& renderTools,
                    const CpuRenderData& data) override
    {
        const auto &src = renderTools.fSrcBtmp;
        auto &dst = renderTools.fDstBtmp;

        const int xMin = data.fTexTile.left();
        const int xMax = data.fTexTile.right();
        const int yMin = data.fTexTile.top();
        const int yMax = data.fTexTile.bottom();

        const qreal directionRad = qDegreesToRadians(mDirection);
        const qreal axisX = qCos(directionRad);
        const qreal axisY = qSin(directionRad);
        const qreal openWidth = mWidth * (1. - mCompletion);
        const qreal featherWidth = qMin(openWidth, mWidth * mFeather);

        for (int yi = yMin; yi < yMax; ++yi) {
            auto *dstPx = static_cast<uchar*>(dst.getAddr(0, yi - yMin));
            auto *srcPx = static_cast<const uchar*>(src.getAddr(xMin, yi));
            for (int xi = xMin; xi < xMax; ++xi) {
                const qreal blindCoord = positiveMod(xi*axisX + yi*axisY, mWidth);
                qreal visible = 0.;
                if (blindCoord < openWidth - featherWidth || qFuzzyIsNull(featherWidth)) {
                    visible = blindCoord < openWidth ? 1. : 0.;
                } else if (blindCoord < openWidth) {
                    visible = 1. - (blindCoord - (openWidth - featherWidth))/featherWidth;
                }

                for (int i = 0; i < 4; ++i) {
                    *dstPx++ = static_cast<uchar>(qBound(0, qRound(srcPx[i] * visible), 255));
                }
                srcPx += 4;
            }
        }
    }
private:
    const qreal mCompletion;
    const qreal mDirection;
    const qreal mWidth;
    const qreal mFeather;
};

}

BlindsEffect::BlindsEffect()
    : RasterEffect("blinds",
                   AppSupport::getRasterEffectHardwareSupport("Blinds",
                                                              HardwareSupport::cpuOnly),
                   false,
                   RasterEffectType::BLINDS)
{
    mCompletion = enve::make_shared<QrealAnimator>(0, 0, 100, 1, "completion");
    mDirection = enve::make_shared<QrealAnimator>(0, -3600, 3600, 1, "direction");
    mWidth = enve::make_shared<QrealAnimator>(40, 1, 2000, 1, "width");
    mFeather = enve::make_shared<QrealAnimator>(20, 0, 100, 1, "feather");

    ca_addChild(mCompletion);
    ca_addChild(mDirection);
    ca_addChild(mWidth);
    ca_addChild(mFeather);
}

stdsptr<RasterEffectCaller> BlindsEffect::getEffectCaller(
        const qreal relFrame,
        const qreal resolution,
        const qreal influence,
        BoxRenderData * const data) const
{
    Q_UNUSED(data)

    const qreal completion = qBound<qreal>(0.,
                                           mCompletion->getEffectiveValue(relFrame) *
                                           0.01 * influence,
                                           1.);
    if (isZero4Dec(completion)) {
        return nullptr;
    }

    return enve::make_shared<BlindsEffectCaller>(
                completion,
                mDirection->getEffectiveValue(relFrame),
                mWidth->getEffectiveValue(relFrame) * resolution,
                mFeather->getEffectiveValue(relFrame) * 0.01);
}
