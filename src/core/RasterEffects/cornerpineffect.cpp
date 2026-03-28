/*
#
# Friction - https://friction.graphics
#
# Copyright (c) Ole-Andre Rodlie and contributors
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
# See 'README.md' for more information.
#
*/

#include "cornerpineffect.h"

#include "Animators/qpointfanimator.h"
#include "Boxes/boundingbox.h"
#include "Boxes/boxrenderdata.h"
#include "MovablePoints/animatedpoint.h"
#include "appsupport.h"

#include <QPolygonF>
#include <QTransform>
#include <QtMath>

namespace {

class CornerPinCaller : public RasterEffectCaller {
public:
    CornerPinCaller(const QMargins& margin,
                    const QPolygonF& dstQuad,
                    const QTransform& dstToSrc) :
        RasterEffectCaller(HardwareSupport::cpuOnly, true, margin),
        mDstQuad(dstQuad),
        mDstToSrc(dstToSrc) {}

    void processCpu(CpuRenderTools& renderTools,
                    const CpuRenderData& data) override {
        const auto& src = renderTools.fSrcBtmp;
        auto& dst = renderTools.fDstBtmp;
        SkCanvas clearCanvas(dst);
        clearCanvas.clear(SK_ColorTRANSPARENT);

        const int xMin = data.fTexTile.left();
        const int xMax = data.fTexTile.right();
        const int yMin = data.fTexTile.top();
        const int yMax = data.fTexTile.bottom();

        for(int yi = yMin; yi < yMax; ++yi) {
            auto *dstPx = static_cast<uchar*>(dst.getAddr(0, yi - yMin));
            for(int xi = xMin; xi < xMax; ++xi) {
                const QPointF dstPt(xi + 0.5, yi + 0.5);
                if(!mDstQuad.containsPoint(dstPt, Qt::OddEvenFill)) {
                    *dstPx++ = 0;
                    *dstPx++ = 0;
                    *dstPx++ = 0;
                    *dstPx++ = 0;
                    continue;
                }

                const QPointF srcPt = mDstToSrc.map(dstPt);
                const int sx0 = qBound(0, qFloor(srcPt.x()), src.width() - 1);
                const int sy0 = qBound(0, qFloor(srcPt.y()), src.height() - 1);
                const int sx1 = qMin(src.width() - 1, sx0 + 1);
                const int sy1 = qMin(src.height() - 1, sy0 + 1);
                const qreal tx = qBound<qreal>(0., srcPt.x() - sx0, 1.);
                const qreal ty = qBound<qreal>(0., srcPt.y() - sy0, 1.);

                const auto *p00 = static_cast<const uchar*>(src.getAddr(sx0, sy0));
                const auto *p10 = static_cast<const uchar*>(src.getAddr(sx1, sy0));
                const auto *p01 = static_cast<const uchar*>(src.getAddr(sx0, sy1));
                const auto *p11 = static_cast<const uchar*>(src.getAddr(sx1, sy1));

                const auto mix = [tx, ty](const qreal a00, const qreal a10,
                                          const qreal a01, const qreal a11) {
                    const qreal m0 = a00*(1. - tx) + a10*tx;
                    const qreal m1 = a01*(1. - tx) + a11*tx;
                    return m0*(1. - ty) + m1*ty;
                };

                *dstPx++ = qBound(0, qRound(mix(p00[0], p10[0], p01[0], p11[0])), 255);
                *dstPx++ = qBound(0, qRound(mix(p00[1], p10[1], p01[1], p11[1])), 255);
                *dstPx++ = qBound(0, qRound(mix(p00[2], p10[2], p01[2], p11[2])), 255);
                *dstPx++ = qBound(0, qRound(mix(p00[3], p10[3], p01[3], p11[3])), 255);
            }
        }
    }
private:
    const QPolygonF mDstQuad;
    const QTransform mDstToSrc;
};

class CornerPinPoint : public AnimatedPoint {
public:
    CornerPinPoint(QPointFAnimator * const animator,
                   CornerPinEffect * const effect) :
        AnimatedPoint(animator, TYPE_PATH_POINT), mEffect(effect) {}

    QPointF getRelativePos() const override {
        return mEffect->normalizedToCanvas(AnimatedPoint::getRelativePos());
    }

    void setRelativePos(const QPointF &relPos) override {
        AnimatedPoint::setRelativePos(mEffect->canvasToNormalized(relPos));
    }
private:
    CornerPinEffect * const mEffect;
};

QMargins buildMargins(const QPolygonF& quad,
                      const int width,
                      const int height) {
    qreal minX = quad.at(0).x();
    qreal minY = quad.at(0).y();
    qreal maxX = minX;
    qreal maxY = minY;
    for(const auto& pt : quad) {
        minX = qMin(minX, pt.x());
        minY = qMin(minY, pt.y());
        maxX = qMax(maxX, pt.x());
        maxY = qMax(maxY, pt.y());
    }

    const qreal srcRight = width - 1.;
    const qreal srcBottom = height - 1.;
    const int left = qMax(0, qCeil(-minX));
    const int top = qMax(0, qCeil(-minY));
    const int right = qMax(0, qCeil(maxX - srcRight));
    const int bottom = qMax(0, qCeil(maxY - srcBottom));
    return {left, top, right, bottom};
}

}

CornerPinEffect::CornerPinEffect() :
    RasterEffect("corner pin",
                 AppSupport::getRasterEffectHardwareSupport("CornerPin",
                                                            HardwareSupport::cpuOnly),
                 false,
                 RasterEffectType::CORNER_PIN) {
    mTopLeft = enve::make_shared<QPointFAnimator>("top left");
    mTopRight = enve::make_shared<QPointFAnimator>("top right");
    mBottomRight = enve::make_shared<QPointFAnimator>("bottom right");
    mBottomLeft = enve::make_shared<QPointFAnimator>("bottom left");

    mTopLeft->setValuesRange(-4, 4);
    mTopRight->setValuesRange(-4, 4);
    mBottomRight->setValuesRange(-4, 4);
    mBottomLeft->setValuesRange(-4, 4);

    mTopLeft->setBaseValue(0, 0);
    mTopRight->setBaseValue(1, 0);
    mBottomRight->setBaseValue(1, 1);
    mBottomLeft->setBaseValue(0, 1);

    ca_addChild(mTopLeft);
    ca_addChild(mTopRight);
    ca_addChild(mBottomRight);
    ca_addChild(mBottomLeft);

    setPointsHandler(enve::make_shared<PointsHandler>());
    mTopLeftPt = enve::make_shared<CornerPinPoint>(mTopLeft.get(), this);
    mTopRightPt = enve::make_shared<CornerPinPoint>(mTopRight.get(), this);
    mBottomRightPt = enve::make_shared<CornerPinPoint>(mBottomRight.get(), this);
    mBottomLeftPt = enve::make_shared<CornerPinPoint>(mBottomLeft.get(), this);
    getPointsHandler()->appendPt(mTopLeftPt);
    getPointsHandler()->appendPt(mTopRightPt);
    getPointsHandler()->appendPt(mBottomRightPt);
    getPointsHandler()->appendPt(mBottomLeftPt);

    ca_setGUIProperty(mTopLeft.get());
    prp_enabledDrawingOnCanvas();
}

QPointF CornerPinEffect::normalizedToCanvas(const QPointF &normalized) const {
    qreal width = 100;
    qreal height = 100;
    if(const auto owner = getFirstAncestor<BoundingBox>()) {
        const auto rect = owner->getRelBoundingRect();
        width = qMax<qreal>(1., rect.width());
        height = qMax<qreal>(1., rect.height());
    }
    return {normalized.x()*width, normalized.y()*height};
}

QPointF CornerPinEffect::canvasToNormalized(const QPointF &point) const {
    qreal width = 100;
    qreal height = 100;
    if(const auto owner = getFirstAncestor<BoundingBox>()) {
        const auto rect = owner->getRelBoundingRect();
        width = qMax<qreal>(1., rect.width());
        height = qMax<qreal>(1., rect.height());
    }
    return {point.x()/width, point.y()/height};
}

void CornerPinEffect::prp_drawCanvasControls(
        SkCanvas * const canvas,
        const CanvasMode mode,
        const float invScale,
        const bool ctrlPressed) {
    if(mode == CanvasMode::pointTransform && isVisible()) {
        const auto p1 = normalizedToCanvas(mTopLeft->getEffectiveValue());
        const auto p2 = normalizedToCanvas(mTopRight->getEffectiveValue());
        const auto p3 = normalizedToCanvas(mBottomRight->getEffectiveValue());
        const auto p4 = normalizedToCanvas(mBottomLeft->getEffectiveValue());

        SkPaint paint;
        paint.setAntiAlias(true);
        paint.setStyle(SkPaint::kStroke_Style);
        paint.setStrokeWidth(2*invScale);
        paint.setColor(SK_ColorYELLOW);

        SkPath path;
        path.moveTo(toSkScalar(p1.x()), toSkScalar(p1.y()));
        path.lineTo(toSkScalar(p2.x()), toSkScalar(p2.y()));
        path.lineTo(toSkScalar(p3.x()), toSkScalar(p3.y()));
        path.lineTo(toSkScalar(p4.x()), toSkScalar(p4.y()));
        path.close();
        canvas->drawPath(path, paint);
    }
    RasterEffect::prp_drawCanvasControls(canvas, mode, invScale, ctrlPressed);
}

stdsptr<RasterEffectCaller> CornerPinEffect::getEffectCaller(
        const qreal relFrame,
        const qreal resolution,
        const qreal influence,
        BoxRenderData * const data) const {
    Q_UNUSED(resolution)
    if(!data) return nullptr;
    if(isZero4Dec(influence)) return nullptr;

    const int width = qMax(1, data->fGlobalRect.width());
    const int height = qMax(1, data->fGlobalRect.height());

    const auto mapToImage = [width, height](const QPointF& normalized) {
        const qreal x = normalized.x()*(width - 1.);
        const qreal y = normalized.y()*(height - 1.);
        return QPointF(x, y);
    };

    const QPointF src1(0, 0);
    const QPointF src2(width - 1., 0);
    const QPointF src3(width - 1., height - 1.);
    const QPointF src4(0, height - 1.);

    const QPointF t1 = mapToImage(mTopLeft->getEffectiveValue(relFrame));
    const QPointF t2 = mapToImage(mTopRight->getEffectiveValue(relFrame));
    const QPointF t3 = mapToImage(mBottomRight->getEffectiveValue(relFrame));
    const QPointF t4 = mapToImage(mBottomLeft->getEffectiveValue(relFrame));

    const auto blend = [influence](const QPointF& from, const QPointF& to) {
        return from + (to - from)*influence;
    };

    const QPointF d1 = blend(src1, t1);
    const QPointF d2 = blend(src2, t2);
    const QPointF d3 = blend(src3, t3);
    const QPointF d4 = blend(src4, t4);

    QPolygonF srcQuad;
    srcQuad << src1 << src2 << src3 << src4;
    QPolygonF dstQuad;
    dstQuad << d1 << d2 << d3 << d4;

    QTransform dstToSrc;
    if(!QTransform::quadToQuad(dstQuad, srcQuad, dstToSrc)) return nullptr;

    return enve::make_shared<CornerPinCaller>(buildMargins(dstQuad, width, height),
                                              dstQuad, dstToSrc);
}
