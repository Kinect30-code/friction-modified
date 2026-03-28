/*
#
# Friction - https://friction.graphics
#
#   Copyright (c) Ole-André Rodlie and contributors
#
#   This program is free software: you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation, either version 3 of the License, or
#   (at your option) any later version.
#
#   This program is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
#
#   You should have received a copy of the GNU General Public License
#   along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
#   See 'README.md' for more information.
#
*/

#include "trackmatteeffect.h"

#include "Boxes/layerboxrenderdata.h"
#include "Boxes/boundingbox.h"
#include "skia/skiaincludes.h"

TrackMatteEffect::TrackMatteEffect() :
    BlendEffect("track matte", BlendEffectType::targeted) {
    const auto modes = QStringList() 
        << "Alpha Matte" 
        << "Alpha Inverted Matte"
        << "Luma Matte"
        << "Luma Inverted Matte";
    mMode = enve::make_shared<ComboBoxProperty>("mode", modes);
    mMatteSource = enve::make_shared<BoxTargetProperty>("matte source");

    ca_addChild(mMode);
    ca_addChild(mMatteSource);
}

TrackMatteMode TrackMatteEffect::getMode() const {
    return static_cast<TrackMatteMode>(mMode->getCurrentValue());
}

BoundingBox* TrackMatteEffect::matteSource() const {
    return mMatteSource->getTarget();
}

bool TrackMatteEffect::invert() const {
    const auto mode = getMode();
    return mode == TrackMatteMode::alphaInvertedMatte || 
           mode == TrackMatteMode::lumaInvertedMatte;
}

void TrackMatteEffect::blendSetup(
        ChildRenderData &data,
        const int index,
        const qreal relFrame,
        QList<ChildRenderData> &delayed) const {
    Q_UNUSED(index);
    const auto matte = matteSource();
    if(!matte) return;

    const auto mode = getMode();
    const bool isLuma = mode == TrackMatteMode::lumaMatte || 
                       mode == TrackMatteMode::lumaInvertedMatte;
    const bool isInverted = invert();

    ChildRenderData iData(data.fData);
    auto& iClip = iData.fClip;
    iClip.fTargetBox = matte;
    iClip.fAbove = false;

    if(isPathValid()) {
        const auto clipPath = this->clipPath(relFrame);
        if(isInverted) {
            data.fClip.fClipOps.append({clipPath, SkClipOp::kDifference, false});
            iClip.fClipOps.append({clipPath, SkClipOp::kIntersect, false});
        } else {
            data.fClip.fClipOps.append({clipPath, SkClipOp::kIntersect, false});
        }
    } else {
        data.fClip.fClipOps.append({SkPath(), SkClipOp::kIntersect, false});
    }

    delayed << iData;
}

void TrackMatteEffect::detachedBlendUISetup(
        const qreal relFrame, const int drawId,
        QList<UIDelayed> &delayed) {
    Q_UNUSED(relFrame)
    Q_UNUSED(drawId)
    const auto matte = matteSource();
    if(!matte) return;

    delayed << [this, matte]
               (int, BoundingBox* prev, BoundingBox* next) {
        if(prev == matte || next == matte) {
            return static_cast<BlendEffect*>(this);
        }
        return static_cast<BlendEffect*>(nullptr);
    };
}

void TrackMatteEffect::detachedBlendSetup(
        const BoundingBox* const boxToDraw,
        const qreal relFrame,
        SkCanvas * const canvas,
        const SkFilterQuality filter,
        const int drawId,
        QList<Delayed> &delayed) const {
    Q_UNUSED(drawId)
    const auto matte = matteSource();
    if(!matte) return;

    const auto mode = getMode();
    const bool isLuma = mode == TrackMatteMode::lumaMatte || 
                       mode == TrackMatteMode::lumaInvertedMatte;
    const bool isInverted = invert();

    if(isPathValid()) {
        const auto clipPath = this->clipPath(relFrame);
        delayed << [boxToDraw, matte, isLuma, isInverted, clipPath, canvas, filter]
                   (int, BoundingBox* prev, BoundingBox* next) {
            if(prev != matte && next != matte) return false;

            canvas->save();
            if(isInverted) {
                canvas->clipPath(clipPath, SkClipOp::kDifference, true);
            } else {
                canvas->clipPath(clipPath, SkClipOp::kIntersect, true);
            }

            return true;
        };
    }
}

void TrackMatteEffect::drawBlendSetup(const qreal relFrame,
                                    SkCanvas * const canvas) const {
    Q_UNUSED(relFrame)
    Q_UNUSED(canvas)
}
