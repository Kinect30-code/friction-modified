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

#ifndef CORNERPINEFFECT_H
#define CORNERPINEFFECT_H

#include "rastereffect.h"

class QPointFAnimator;
class AnimatedPoint;

class CORE_EXPORT CornerPinEffect : public RasterEffect {
    e_OBJECT
public:
    CornerPinEffect();

    stdsptr<RasterEffectCaller> getEffectCaller(
            const qreal relFrame,
            const qreal resolution,
            const qreal influence,
            BoxRenderData * const data) const override;

    void prp_drawCanvasControls(
            SkCanvas * const canvas,
            const CanvasMode mode,
            const float invScale,
            const bool ctrlPressed) override;

    QPointF normalizedToCanvas(const QPointF& normalized) const;
    QPointF canvasToNormalized(const QPointF& point) const;
private:
    qsptr<QPointFAnimator> mTopLeft;
    qsptr<QPointFAnimator> mTopRight;
    qsptr<QPointFAnimator> mBottomRight;
    qsptr<QPointFAnimator> mBottomLeft;

    stdsptr<AnimatedPoint> mTopLeftPt;
    stdsptr<AnimatedPoint> mTopRightPt;
    stdsptr<AnimatedPoint> mBottomRightPt;
    stdsptr<AnimatedPoint> mBottomLeftPt;
};

#endif // CORNERPINEFFECT_H
