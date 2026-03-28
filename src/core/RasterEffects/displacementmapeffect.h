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

#ifndef DISPLACEMENTMAPEFFECT_H
#define DISPLACEMENTMAPEFFECT_H

#include "rastereffect.h"
#include "Properties/boxtargetproperty.h"
#include "conncontextptr.h"

class QrealAnimator;
class BoundingBox;

class CORE_EXPORT DisplacementMapEffect : public RasterEffect {
    e_OBJECT
public:
    DisplacementMapEffect();

    stdsptr<RasterEffectCaller> getEffectCaller(
            const qreal relFrame,
            const qreal resolution,
            const qreal influence,
            BoxRenderData * const data) const override;

    FrameRange prp_getIdenticalRelRange(const int relFrame) const override;

private:
    BoundingBox* mapSource() const;

    qsptr<BoxTargetProperty> mMapSource;
    qsptr<QrealAnimator> mAmountX;
    qsptr<QrealAnimator> mAmountY;
    mutable ConnContextQPtr<BoundingBox> mMapConn;
};

#endif // DISPLACEMENTMAPEFFECT_H
