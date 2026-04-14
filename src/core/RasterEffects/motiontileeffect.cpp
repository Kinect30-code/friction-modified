#include "motiontileeffect.h"

#include "Animators/qpointfanimator.h"
#include "Animators/qrealanimator.h"
#include "Boxes/boundingbox.h"
#include "canvas.h"
#include "appsupport.h"

#include <QtMath>

namespace {

int wrapCoord(const int value, const int size)
{
    if (size <= 1) {
        return 0;
    }
    int wrapped = value % size;
    if (wrapped < 0) {
        wrapped += size;
    }
    return wrapped;
}

int mirrorCoord(const int value, const int size)
{
    if (size <= 1) {
        return 0;
    }
    const int period = 2*size - 2;
    int wrapped = value % period;
    if (wrapped < 0) {
        wrapped += period;
    }
    return wrapped < size ? wrapped : period - wrapped;
}

QMargins marginsForTileRect(const QRectF &srcRect,
                            const qreal outputScaleX,
                            const qreal outputScaleY,
                            const QPointF &centerPct)
{
    const qreal srcWidth = qMax<qreal>(1., srcRect.width());
    const qreal srcHeight = qMax<qreal>(1., srcRect.height());
    const qreal outWidth = srcWidth * qMax<qreal>(0.01, outputScaleX);
    const qreal outHeight = srcHeight * qMax<qreal>(0.01, outputScaleY);

    const qreal centerX = srcRect.left() + srcWidth * centerPct.x() * 0.01;
    const qreal centerY = srcRect.top() + srcHeight * centerPct.y() * 0.01;

    const qreal left = centerX - outWidth*0.5;
    const qreal top = centerY - outHeight*0.5;
    const qreal right = centerX + outWidth*0.5;
    const qreal bottom = centerY + outHeight*0.5;

    return QMargins(qMax(0, qCeil(srcRect.left() - left)),
                    qMax(0, qCeil(srcRect.top() - top)),
                    qMax(0, qCeil(right - srcRect.right())),
                    qMax(0, qCeil(bottom - srcRect.bottom())));
}

class MotionTileCaller : public RasterEffectCaller {
public:
    MotionTileCaller(const qreal outputScaleX,
                     const qreal outputScaleY,
                     const QPointF &centerPct,
                     const bool mirrorEdges)
        : RasterEffectCaller(HardwareSupport::cpuOnly, true)
        , mOutputScaleX(qMax<qreal>(0.01, outputScaleX))
        , mOutputScaleY(qMax<qreal>(0.01, outputScaleY))
        , mCenterPct(centerPct)
        , mMirrorEdges(mirrorEdges)
    {}

    void processCpu(CpuRenderTools& renderTools,
                    const CpuRenderData& data) override
    {
        const auto &src = renderTools.fSrcBtmp;
        auto &dst = renderTools.fDstBtmp;

        SkCanvas clearCanvas(dst);
        clearCanvas.clear(SK_ColorTRANSPARENT);

        const int srcLeft = fSrcRect.left() - data.fPos.x();
        const int srcTop = fSrcRect.top() - data.fPos.y();
        const int srcWidth = fSrcRect.width();
        const int srcHeight = fSrcRect.height();

        const int outLeft = fDstRect.left() - data.fPos.x();
        const int outTop = fDstRect.top() - data.fPos.y();
        const int outRight = outLeft + fDstRect.width();
        const int outBottom = outTop + fDstRect.height();

        const qreal centerX = srcLeft + srcWidth * mCenterPct.x() * 0.01;
        const qreal centerY = srcTop + srcHeight * mCenterPct.y() * 0.01;
        const qreal tileOriginX = centerX - srcWidth*0.5;
        const qreal tileOriginY = centerY - srcHeight*0.5;

        const int xMin = data.fTexTile.left();
        const int xMax = data.fTexTile.right();
        const int yMin = data.fTexTile.top();
        const int yMax = data.fTexTile.bottom();

        for (int yi = yMin; yi < yMax; ++yi) {
            auto *dstPx = static_cast<uchar*>(dst.getAddr(0, yi - yMin));
            for (int xi = xMin; xi < xMax; ++xi) {
                if (xi < outLeft || xi >= outRight ||
                    yi < outTop || yi >= outBottom) {
                    *dstPx++ = 0;
                    *dstPx++ = 0;
                    *dstPx++ = 0;
                    *dstPx++ = 0;
                    continue;
                }

                const int sampleX = mMirrorEdges
                        ? mirrorCoord(qFloor(xi - tileOriginX), srcWidth)
                        : wrapCoord(qFloor(xi - tileOriginX), srcWidth);
                const int sampleY = mMirrorEdges
                        ? mirrorCoord(qFloor(yi - tileOriginY), srcHeight)
                        : wrapCoord(qFloor(yi - tileOriginY), srcHeight);
                const auto *srcPx = static_cast<const uchar*>(
                            src.getAddr(srcLeft + sampleX, srcTop + sampleY));
                for (int i = 0; i < 4; ++i) {
                    *dstPx++ = srcPx[i];
                }
            }
        }
    }
protected:
    QMargins getMargin(const SkIRect &srcRect) override
    {
        return marginsForTileRect(QRectF(srcRect.left(),
                                         srcRect.top(),
                                         srcRect.width(),
                                         srcRect.height()),
                                  mOutputScaleX,
                                  mOutputScaleY,
                                  mCenterPct);
    }
private:
    const qreal mOutputScaleX;
    const qreal mOutputScaleY;
    const QPointF mCenterPct;
    const bool mMirrorEdges;
};

}

MotionTileEffect::MotionTileEffect()
    : RasterEffect("motion tile",
                   AppSupport::getRasterEffectHardwareSupport("MotionTile",
                                                              HardwareSupport::cpuOnly),
                   false,
                   RasterEffectType::MOTION_TILE)
{
    mOutputWidth = enve::make_shared<QrealAnimator>(100, 1, 2000, 1, "output width");
    mOutputHeight = enve::make_shared<QrealAnimator>(100, 1, 2000, 1, "output height");
    mCenter = enve::make_shared<QPointFAnimator>("tile center");
    mMirrorEdges = enve::make_shared<QrealAnimator>(0, 0, 1, 1, "mirror edges");

    mCenter->setValuesRange(-500, 500);
    mCenter->setBaseValue(50, 50);

    ca_addChild(mOutputWidth);
    ca_addChild(mOutputHeight);
    ca_addChild(mCenter);
    ca_addChild(mMirrorEdges);
}

QMargins MotionTileEffect::getMargin() const
{
    auto *box = getFirstAncestor<BoundingBox>();
    auto *scene = box ? box->getParentScene() : nullptr;
    const qreal resolution = scene ? scene->getResolution() : 1.;
    const QSizeF sourceSize = box ? box->getAbsBoundingRect().size() * resolution
                                  : QSizeF(1, 1);
    return marginsForTileRect(QRectF(0, 0, sourceSize.width(), sourceSize.height()),
                              qMax<qreal>(0.01, mOutputWidth->getEffectiveValue() * 0.01),
                              qMax<qreal>(0.01, mOutputHeight->getEffectiveValue() * 0.01),
                              mCenter->getEffectiveValue());
}

stdsptr<RasterEffectCaller> MotionTileEffect::getEffectCaller(
        const qreal relFrame,
        const qreal resolution,
        const qreal influence,
        BoxRenderData * const data) const
{
    Q_UNUSED(resolution)
    Q_UNUSED(data)

    const qreal outputScaleX = 1. +
            (mOutputWidth->getEffectiveValue(relFrame) * 0.01 - 1.) * influence;
    const qreal outputScaleY = 1. +
            (mOutputHeight->getEffectiveValue(relFrame) * 0.01 - 1.) * influence;
    const QPointF center = QPointF(50, 50) +
            (mCenter->getEffectiveValue(relFrame) - QPointF(50, 50)) * influence;

    return enve::make_shared<MotionTileCaller>(
                outputScaleX,
                outputScaleY,
                center,
                mMirrorEdges->getEffectiveValue(relFrame) >= 0.5);
}
