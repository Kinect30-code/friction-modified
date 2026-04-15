#ifndef TRACKMATTEDRAWRESOLVER_H
#define TRACKMATTEDRAWRESOLVER_H

#include "Boxes/boundingbox.h"
#include "Boxes/boxrenderdata.h"
#include "actions.h"
#include "skia/skqtconversions.h"

#include <functional>

namespace TrackMatteDrawResolver {

inline bool matricesMatch(const QMatrix& lhs,
                          const QMatrix& rhs) {
    return isZero4Dec(lhs.m11() - rhs.m11()) &&
           isZero4Dec(lhs.m12() - rhs.m12()) &&
           isZero4Dec(lhs.m21() - rhs.m21()) &&
           isZero4Dec(lhs.m22() - rhs.m22()) &&
           isZero4Dec(lhs.dx() - rhs.dx()) &&
           isZero4Dec(lhs.dy() - rhs.dy());
}

inline QMatrix displayTransform(const BoxRenderData* const renderData,
                                const QMatrix& totalTransform) {
    QMatrix paintTransform =
            renderData->fTotalTransform.inverted()*totalTransform;
    const qreal invRes = renderData->fResolution > 0.
            ? 1/renderData->fResolution
            : 1.;
    paintTransform.scale(invRes, invRes);
    return paintTransform;
}

struct DrawData {
    QRectF fBounds;
    SkBlendMode fBlendMode = SkBlendMode::kSrcOver;
    std::function<void(SkCanvas * const, SkPaint&)> fDrawRaw;

    explicit operator bool() const {
        return static_cast<bool>(fDrawRaw) && !fBounds.isEmpty();
    }
};

inline DrawData resolve(const BoundingBox* const box,
                        const qreal relFrame,
                        const stdsptr<BoxRenderData>& renderData) {
    DrawData result;
    if(box && Actions::sInstance && Actions::sInstance->smoothChange() &&
       box->hasLatestFinishedDisplayData(relFrame)) {
        result.fBounds = box->getLatestFinishedDisplayRect(relFrame);
        result.fBlendMode = renderData ? renderData->fBlendMode
                                       : SkBlendMode::kSrcOver;
        result.fDrawRaw = [box, relFrame](SkCanvas * const canvas, SkPaint& paint) {
            box->drawLatestFinishedDisplayRaw(canvas, paint, relFrame);
        };
        return result;
    }

    stdsptr<BoxRenderData> resolvedRenderData;
    const auto currentTransform = box ? box->getTotalTransformAtFrame(relFrame)
                                      : QMatrix();
    if(renderData && renderData->finished()) {
        if(box &&
           (!isZero4Dec(renderData->fRelFrame - relFrame) ||
            !matricesMatch(renderData->fTotalTransform, currentTransform))) {
            if(renderData->fRenderedImage &&
               !renderData->fUseRenderTransform) {
                const QMatrix paintTransform =
                        displayTransform(renderData.get(), currentTransform);
                result.fBounds = paintTransform.mapRect(
                            QRectF(renderData->fGlobalRect));
                result.fBlendMode = renderData->fBlendMode;
                result.fDrawRaw = [renderData, paintTransform](
                        SkCanvas * const canvas,
                        SkPaint& paint) {
                    canvas->save();
                    canvas->concat(toSkMatrix(paintTransform));
                    renderData->drawOnParentLayerRaw(canvas, paint);
                    canvas->restore();
                };
                return result;
            }
            resolvedRenderData = renderData->makeCopy();
            if(resolvedRenderData) {
                resolvedRenderData->fRelFrame = relFrame;
                resolvedRenderData->remapToTotalTransform(currentTransform);
            }
        } else {
            resolvedRenderData = renderData;
        }
    } else if(box) {
        const auto latestFinished = box->getLatestFinishedRenderData(relFrame);
        if(latestFinished) {
            if(latestFinished->fRenderedImage &&
               !latestFinished->fUseRenderTransform) {
                const QMatrix paintTransform =
                        displayTransform(latestFinished.get(), currentTransform);
                result.fBounds = paintTransform.mapRect(
                            QRectF(latestFinished->fGlobalRect));
                result.fBlendMode = latestFinished->fBlendMode;
                result.fDrawRaw = [latestFinished, paintTransform](
                        SkCanvas * const canvas,
                        SkPaint& paint) {
                    canvas->save();
                    canvas->concat(toSkMatrix(paintTransform));
                    latestFinished->drawOnParentLayerRaw(canvas, paint);
                    canvas->restore();
                };
                return result;
            }
            resolvedRenderData = latestFinished->makeCopy();
            if(resolvedRenderData) {
                resolvedRenderData->fRelFrame = relFrame;
                resolvedRenderData->remapToTotalTransform(currentTransform);
            }
        }
    }

    if(!resolvedRenderData) {
        return result;
    }

    result.fBounds = QRectF(resolvedRenderData->fGlobalRect);
    result.fBlendMode = resolvedRenderData->fBlendMode;
    result.fDrawRaw = [resolvedRenderData](SkCanvas * const canvas, SkPaint& paint) {
        resolvedRenderData->drawOnParentLayerRaw(canvas, paint);
    };
    return result;
}

}

#endif // TRACKMATTEDRAWRESOLVER_H
