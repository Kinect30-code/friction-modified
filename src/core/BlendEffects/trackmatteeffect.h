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

#ifndef TRACKMATTEEFFECT_H
#define TRACKMATTEEFFECT_H

#include "blendeffect.h"
#include "Properties/comboboxproperty.h"

enum class TrackMatteMode {
    alphaMatte,
    alphaInvertedMatte,
    lumaMatte,
    lumaInvertedMatte
};

class CORE_EXPORT TrackMatteEffect : public BlendEffect {
public:
    TrackMatteEffect();

    void blendSetup(ChildRenderData &data,
                    const int index,
                    const qreal relFrame,
                    QList<ChildRenderData> &delayed) const;

    void detachedBlendUISetup(const qreal relFrame,
                              const int drawId,
                              QList<UIDelayed> &delayed);
    void detachedBlendSetup(const BoundingBox* const boxToDraw,
                        const qreal relFrame,
                        SkCanvas * const canvas,
                        const SkFilterQuality filter,
                        const int drawId,
                        QList<Delayed> &delayed) const;
    void drawBlendSetup(const qreal relFrame,
                        SkCanvas * const canvas) const;

    TrackMatteMode getMode() const;
    BoundingBox* matteSource() const;
    bool invert() const;

private:
    qsptr<ComboBoxProperty> mMode;
    qsptr<BoxTargetProperty> mMatteSource;
};

#endif // TRACKMATTEEFFECT_H
