/*
#
# Friction - https://friction.graphics
#
# Copyright (c) Ole-André Rodlie and contributors
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

// Fork of enve - Copyright (C) 2016-2020 Maurycy Liebner

#include "layerboxrenderdata.h"
#include "containerbox.h"
#include "BlendEffects/trackmattedrawresolver.h"
#include "BlendEffects/trackmatteeffect.h"
#include "skia/skiahelpers.h"
#include "skia/skqtconversions.h"
#include <QHash>

namespace {

using MainChildRenderDataByBox = QHash<const BoundingBox*, const ChildRenderData*>;
using ResolvedTrackMatteDrawDataByRenderData =
        QHash<const BoxRenderData*, TrackMatteDrawResolver::DrawData>;

MainChildRenderDataByBox buildMainChildRenderDataIndex(
        const QList<ChildRenderData>& children) {
    MainChildRenderDataByBox result;
    result.reserve(children.count());
    for(const auto& candidate : children) {
        if(!candidate.fIsMain || !candidate.fData) {
            continue;
        }
        const auto * const box = candidate.fData->fBlendEffectIdentifier;
        if(!box || result.contains(box)) {
            continue;
        }
        result.insert(box, &candidate);
    }
    return result;
}

TrackMatteDrawResolver::DrawData resolveTrackMatteDrawData(
        const BoundingBox* const box,
        const ChildRenderData& child,
        ResolvedTrackMatteDrawDataByRenderData& resolvedDrawDataByRenderData) {
    if(!child.fData) {
        return {};
    }

    const auto * const renderData = child.fData.get();
    const auto resolved =
            resolvedDrawDataByRenderData.constFind(renderData);
    if(resolved != resolvedDrawDataByRenderData.constEnd()) {
        return resolved.value();
    }

    const auto drawData =
            TrackMatteDrawResolver::resolve(box, child.fData->fRelFrame,
                                            child.fData);
    resolvedDrawDataByRenderData.insert(renderData, drawData);
    return drawData;
}

bool drawTrackMatteChild(const ChildRenderData& child,
                         const MainChildRenderDataByBox& mainChildrenByBox,
                         ResolvedTrackMatteDrawDataByRenderData&
                             resolvedDrawDataByRenderData,
                         const QRect& parentRect,
                         SkCanvas * const canvas) {
    if(!child.fData) {
        return false;
    }

    const auto box = child.fData->fParentBox.data();
    if(!box) {
        return false;
    }

    const auto trackMatte = box->getTrackMatteEffect();
    if(!trackMatte) {
        return false;
    }

    const auto matteSource = trackMatte->matteSource();
    if(!matteSource) {
        return false;
    }

    const auto mode = trackMatte->getMode();
    const bool inverted = mode == TrackMatteMode::alphaInvertedMatte ||
                          mode == TrackMatteMode::lumaInvertedMatte;

    const auto targetDrawData =
            resolveTrackMatteDrawData(box, child, resolvedDrawDataByRenderData);
    if(!targetDrawData) {
        return true;
    }

    const auto matteChild = mainChildrenByBox.value(matteSource, nullptr);
    if(!matteChild || !matteChild->fData) {
        if(inverted) {
            SkPaint boxPaint;
            boxPaint.setFilterQuality(child.fData->fFilterQuality);
            boxPaint.setBlendMode(targetDrawData.fBlendMode);
            targetDrawData.fDrawRaw(canvas, boxPaint);
        }
        return true;
    }

    const auto matteDrawData =
            resolveTrackMatteDrawData(matteSource, *matteChild,
                                      resolvedDrawDataByRenderData);
    if(!matteDrawData) {
        if(inverted) {
            SkPaint boxPaint;
            boxPaint.setFilterQuality(child.fData->fFilterQuality);
            boxPaint.setBlendMode(targetDrawData.fBlendMode);
            targetDrawData.fDrawRaw(canvas, boxPaint);
        }
        return true;
    }

    const bool intersects = targetDrawData.fBounds.intersects(matteDrawData.fBounds);
    if(!intersects) {
        if(inverted) {
            SkPaint boxPaint;
            boxPaint.setFilterQuality(child.fData->fFilterQuality);
            boxPaint.setBlendMode(targetDrawData.fBlendMode);
            targetDrawData.fDrawRaw(canvas, boxPaint);
            return true;
        }
        return true;
    }

    const QRectF compositeRect = targetDrawData.fBounds;
    const auto compositeBounds =
            SkRect::MakeLTRB(compositeRect.left() - parentRect.left(),
                             compositeRect.top() - parentRect.top(),
                             compositeRect.right() - parentRect.left(),
                             compositeRect.bottom() - parentRect.top());

    SkPaint compositePaint;
    compositePaint.setBlendMode(child.fData->fBlendMode);
    canvas->saveLayer(&compositeBounds, &compositePaint);

    SkPaint mattePaint;
    mattePaint.setFilterQuality(matteChild->fData->fFilterQuality);
    matteDrawData.fDrawRaw(canvas, mattePaint);

    SkPaint boxPaint;
    boxPaint.setFilterQuality(child.fData->fFilterQuality);
    boxPaint.setBlendMode(inverted ? SkBlendMode::kSrcOut
                                   : SkBlendMode::kSrcIn);
    targetDrawData.fDrawRaw(canvas, boxPaint);

    canvas->restore();
    return true;
}

}

ContainerBoxRenderData::ContainerBoxRenderData(BoundingBox * const parentBox) :
    BoxRenderData(parentBox) {
    mDelayDataSet = true;
}

void ContainerBoxRenderData::transformRenderCanvas(SkCanvas &canvas) const {
    canvas.translate(toSkScalar(-fGlobalRect.x()),
                     toSkScalar(-fGlobalRect.y()));
}
#include "pointhelpers.h"
void ContainerBoxRenderData::updateRelBoundingRect() {
    fRelBoundingRect = QRectF();
    const auto invTrans = fTotalTransform.inverted();
    for(const auto &child : fChildrenRenderData) {
        if(child->fRelBoundingRect.isEmpty()) continue;
        QPointF tl = child->fRelBoundingRect.topLeft();
        QPointF tr = child->fRelBoundingRect.topRight();
        QPointF br = child->fRelBoundingRect.bottomRight();
        QPointF bl = child->fRelBoundingRect.bottomLeft();

        const auto trans = child->fTotalTransform*invTrans;

        tl = trans.map(tl);
        tr = trans.map(tr);
        br = trans.map(br);
        bl = trans.map(bl);

        if(fRelBoundingRect.isNull()) {
            fRelBoundingRect.setTop(qMin4(tl.y(), tr.y(), br.y(), bl.y()));
            fRelBoundingRect.setLeft(qMin4(tl.x(), tr.x(), br.x(), bl.x()));
            fRelBoundingRect.setBottom(qMax4(tl.y(), tr.y(), br.y(), bl.y()));
            fRelBoundingRect.setRight(qMax4(tl.x(), tr.x(), br.x(), bl.x()));
        } else {
            fRelBoundingRect.setTop(qMin(fRelBoundingRect.top(),
                                         qMin4(tl.y(), tr.y(), br.y(), bl.y())));
            fRelBoundingRect.setLeft(qMin(fRelBoundingRect.left(),
                                          qMin4(tl.x(), tr.x(), br.x(), bl.x())));
            fRelBoundingRect.setBottom(qMax(fRelBoundingRect.bottom(),
                                            qMax4(tl.y(), tr.y(), br.y(), bl.y())));
            fRelBoundingRect.setRight(qMax(fRelBoundingRect.right(),
                                           qMax4(tl.x(), tr.x(), br.x(), bl.x())));
        }

        fOtherGlobalRects << child->fGlobalRect;
    }
}

void ContainerBoxRenderData::drawSk(SkCanvas * const canvas) {
    const auto mainChildrenByBox =
            buildMainChildRenderDataIndex(fChildrenRenderData);
    ResolvedTrackMatteDrawDataByRenderData resolvedDrawDataByRenderData;
    resolvedDrawDataByRenderData.reserve(mainChildrenByBox.count());
    for(const auto &child : fChildrenRenderData) {
        canvas->save();
        if(!child.fClip.fClipOps.isEmpty()) {
            const SkMatrix transform = canvas->getTotalMatrix();
            canvas->concat(toSkMatrix(fResolutionScale));
            child.fClip.clip(canvas);
            canvas->setMatrix(transform);
        }

        if(drawTrackMatteChild(child, mainChildrenByBox,
                               resolvedDrawDataByRenderData,
                               fGlobalRect, canvas)) {
            canvas->restore();
            continue;
        }

        const auto childBox = child.fData ? child.fData->fParentBox.data()
                                          : nullptr;
        if(childBox && childBox->isUsedAsTrackMatteSource()) {
            canvas->restore();
            continue;
        }

        child->drawOnParentLayer(canvas);
        canvas->restore();
    }
}
