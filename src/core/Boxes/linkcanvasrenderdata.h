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

#ifndef LINKCANVASRENDERDATA_H
#define LINKCANVASRENDERDATA_H
#include "canvasrenderdata.h"

class SceneFrameContainer;

struct CORE_EXPORT LinkCanvasRenderData : public CanvasRenderData {
    LinkCanvasRenderData(BoundingBox * const parentBoxT) :
        CanvasRenderData(parentBoxT) {}

    bool fClipToCanvas = false;
    void setCachedSceneFrame(SceneFrameContainer* const container);
protected:
    SkColor eraseColor() const override {
        // In Layer mode (fClipToCanvas), use transparent so the parent
        // canvas background shows through instead of the target's black bg.
        return fClipToCanvas ? SK_ColorTRANSPARENT : fBgColor;
    }

    void drawSk(SkCanvas * const canvas) override;
    void setupRenderData() override;

    void updateRelBoundingRect() override {
        if(fClipToCanvas) CanvasRenderData::updateRelBoundingRect();
        else ContainerBoxRenderData::updateRelBoundingRect();
    }

    void updateGlobalRect() override {
        if(fClipToCanvas) CanvasRenderData::updateGlobalRect();
        else ContainerBoxRenderData::updateGlobalRect();
    }

private:
    sk_sp<SkImage> mCachedSceneImage;
    qreal mCachedSceneResolution = 1.;
};

#endif // LINKCANVASRENDERDATA_H
