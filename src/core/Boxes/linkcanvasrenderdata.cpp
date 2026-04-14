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

#include "linkcanvasrenderdata.h"
#include "CacheHandlers/imagecachecontainer.h"
#include "skia/skiahelpers.h"
#include "skia/skqtconversions.h"

void LinkCanvasRenderData::setCachedSceneFrame(ImageCacheContainer * const container) {
    if(!container) {
        return;
    }
    mCachedSceneImage = container->getImage();
}

void LinkCanvasRenderData::setupRenderData() {
    if(!mCachedSceneImage) {
        return;
    }

    if(!fRelBoundingRectSet) {
        fRelBoundingRectSet = true;
        updateRelBoundingRect();
    }

    if(!fForceRasterize && !hasEffects()) {
        fBaseMargin = QMargins();
        dataSet();
        updateGlobalRect();
        fRenderTransform.reset();
        fRenderTransform.translate(fRelBoundingRect.x(), fRelBoundingRect.y());
        fRenderTransform *= fScaledTransform;
        fRenderTransform.translate(-fGlobalRect.x(), -fGlobalRect.y());
        fUseRenderTransform = true;
        fRenderedImage = mCachedSceneImage;
        fAntiAlias = true;
        finishedProcessing();
    }
}

void LinkCanvasRenderData::drawSk(SkCanvas * const canvas) {
    if(mCachedSceneImage) {
        const float x = static_cast<float>(fRelBoundingRect.x());
        const float y = static_cast<float>(fRelBoundingRect.y());
        if(fFilterQuality > kNone_SkFilterQuality) {
            SkPaint paint;
            paint.setAntiAlias(true);
            paint.setFilterQuality(fFilterQuality);
            canvas->drawImage(mCachedSceneImage, x, y, &paint);
        } else {
            canvas->drawImage(mCachedSceneImage, x, y);
        }
        return;
    }

    ContainerBoxRenderData::drawSk(canvas);
    if(fClipToCanvas) {
        canvas->save();
        canvas->concat(toSkMatrix(fScaledTransform));
        canvas->clipRect(toSkRect(fRelBoundingRect), SkClipOp::kDifference, true);
        canvas->clear(SK_ColorTRANSPARENT);
        canvas->restore();
    }
}

void LinkCanvasRenderData::afterProcessing() {
    BoxRenderData::afterProcessing();
}
