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

namespace {

bool isTrackMatteInverted(const BoundingBox* const box) {
    if(!box) {
        return false;
    }
    switch(box->getTrackMatteMode()) {
    case TrackMatteMode::alphaInvertedMatte:
    case TrackMatteMode::lumaInvertedMatte:
        return true;
    default:
        return false;
    }
}

const ChildRenderData* findMainChildRenderDataForBox(
        const QList<ChildRenderData>& children,
        const BoundingBox* const box) {
    if(!box) {
        return nullptr;
    }
    for(const auto& candidate : children) {
        if(!candidate.fIsMain || !candidate.fData) {
            continue;
        }
        if(candidate.fData->fBlendEffectIdentifier == box) {
            return &candidate;
        }
    }
    return nullptr;
}

bool drawTrackMatteChild(const QList<ChildRenderData>& children,
                         const ChildRenderData& child,
                         const QRect& parentRect,
                         SkCanvas * const canvas) {
    if(!child.fData) {
        return false;
    }

    const auto box = child.fData->fParentBox.data();
    if(!box) {
        return false;
    }

    const auto matteSource = box->getTrackMatteTarget();
    if(!matteSource) {
        return false;
    }

    const auto matteChild = findMainChildRenderDataForBox(children, matteSource);
    if(!matteChild || !matteChild->fData) {
        return false;
    }

    const auto childRelFrame = child.fData->fRelFrame;
    const auto matteRelFrame = matteChild->fData->fRelFrame;
    const auto targetDrawData =
            TrackMatteDrawResolver::resolve(box, childRelFrame, child.fData);
    const auto matteDrawData =
            TrackMatteDrawResolver::resolve(matteSource, matteRelFrame,
                                            matteChild->fData);
    if(!targetDrawData || !matteDrawData) {
        return false;
    }

    const QRectF compositeRect =
            targetDrawData.fBounds.united(matteDrawData.fBounds);
    const auto compositeBounds =
            SkRect::MakeLTRB(compositeRect.left() - parentRect.left(),
                             compositeRect.top() - parentRect.top(),
                             compositeRect.right() - parentRect.left(),
                             compositeRect.bottom() - parentRect.top());

    SkPaint compositePaint;
    compositePaint.setBlendMode(child.fData->fBlendMode);
    canvas->saveLayer(&compositeBounds, &compositePaint);

    SkPaint mattePaint;
    matteDrawData.fDrawRaw(canvas, mattePaint);

    SkPaint boxPaint;
    boxPaint.setBlendMode(isTrackMatteInverted(box)
                              ? SkBlendMode::kSrcOut
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
    for(const auto &child : fChildrenRenderData) {
        canvas->save();
        if(!child.fClip.fClipOps.isEmpty()) {
            const SkMatrix transform = canvas->getTotalMatrix();
            canvas->concat(toSkMatrix(fResolutionScale));
            child.fClip.clip(canvas);
            canvas->setMatrix(transform);
        }

        if(drawTrackMatteChild(fChildrenRenderData, child, fGlobalRect, canvas)) {
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
