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

#include "trackmattedrawresolver.h"
#include "Boxes/boundingbox.h"
#include "skia/skiaincludes.h"

namespace {

SkRect trackMatteCompositeBounds(const QRectF& targetBounds) {
    return SkRect::MakeLTRB(targetBounds.left(),
                            targetBounds.top(),
                            targetBounds.right(),
                            targetBounds.bottom());
}

}

TrackMatteEffect::TrackMatteEffect() :
    BlendEffect("track matte", BlendEffectType::trackMatte) {
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

void TrackMatteEffect::setModeAction(TrackMatteMode mode) {
    mMode->setCurrentValue(static_cast<int>(mode));
}

BoundingBox* TrackMatteEffect::matteSource() const {
    return mMatteSource->getTarget();
}

void TrackMatteEffect::setMatteSourceAction(BoundingBox *source) {
    mMatteSource->setTargetAction(source);
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
    Q_UNUSED(data)
    Q_UNUSED(index)
    Q_UNUSED(relFrame)
    Q_UNUSED(delayed)
}

void TrackMatteEffect::detachedBlendUISetup(
        const qreal relFrame, const int drawId,
        QList<UIDelayed> &delayed) {
    Q_UNUSED(relFrame)
    Q_UNUSED(drawId)
    Q_UNUSED(delayed)
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

    const bool isInverted = invert();
    delayed << [boxToDraw, matte, relFrame, isInverted, canvas, filter]
               (int, BoundingBox* prev, BoundingBox* next) {
        Q_UNUSED(prev)
        if(next != boxToDraw) return false;

        const qreal absFrame = boxToDraw->prp_relFrameToAbsFrameF(relFrame);
        const qreal matteRelFrame = matte->prp_absFrameToRelFrameF(absFrame);
        const auto boxData =
                boxToDraw->getLatestFinishedRenderData(relFrame);
        const auto matteData =
                matte->getLatestFinishedRenderData(matteRelFrame);
        const auto boxDrawData =
                TrackMatteDrawResolver::resolve(boxToDraw, relFrame, boxData);
        const auto matteDrawData =
                TrackMatteDrawResolver::resolve(matte, matteRelFrame, matteData);
        if(!boxDrawData) {
            return true;
        }

        if(!matteDrawData) {
            if(!isInverted) {
                return true;
            }

            SkPaint boxPaint;
            boxPaint.setFilterQuality(filter);
            boxPaint.setBlendMode(boxDrawData.fBlendMode);
            boxDrawData.fDrawRaw(canvas, boxPaint);
            return true;
        }

        if(!boxDrawData.fBounds.intersects(matteDrawData.fBounds)) {
            if(!isInverted) {
                return true;
            }

            SkPaint boxPaint;
            boxPaint.setFilterQuality(filter);
            boxPaint.setBlendMode(boxDrawData.fBlendMode);
            boxDrawData.fDrawRaw(canvas, boxPaint);
            return true;
        }

        SkPaint compositePaint;
        compositePaint.setBlendMode(boxDrawData.fBlendMode);
        const auto compositeBounds = trackMatteCompositeBounds(boxDrawData.fBounds);
        canvas->saveLayer(&compositeBounds, &compositePaint);

        SkPaint mattePaint;
        mattePaint.setFilterQuality(filter);
        matteDrawData.fDrawRaw(canvas, mattePaint);

        SkPaint boxPaint;
        boxPaint.setFilterQuality(filter);
        boxPaint.setBlendMode(isInverted ? SkBlendMode::kSrcOut
                                         : SkBlendMode::kSrcIn);
        boxDrawData.fDrawRaw(canvas, boxPaint);

        canvas->restore();
        return true;
    };
}

void TrackMatteEffect::drawBlendSetup(const qreal relFrame,
                                    SkCanvas * const canvas) const {
    Q_UNUSED(relFrame)
    if(!matteSource()) return;
    canvas->clipRect(SkRect::MakeEmpty(), SkClipOp::kIntersect, false);
}
