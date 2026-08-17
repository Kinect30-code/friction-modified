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

#include "adjustmentlayerbox.h"
#include "Animators/transformanimator.h"
#include "BlendEffects/blendeffectcollection.h"

AdjustmentLayerBox::AdjustmentLayerBox()
    : ContainerBox(eBoxType::adjustmentLayer)
{
    prp_setName("Adjustment Layer (Experimental)");
}

void AdjustmentLayerBox::drawPixmapSk(SkCanvas * const canvas,
                                      const SkFilterQuality filter,
                                      int& drawId,
                                      QList<BlendEffect::Delayed> &delayed) const
{
    if(!isVisible()) return;
    if(isGroup()) return drawContained(canvas, filter, drawId, delayed);

    canvas->save();
    if(blendEffectsEnabled()) {
        mBlendEffectCollection->drawBlendSetup(canvas);
    }

    const int intAlpha = qRound(mTransformAnimator->getOpacity() * 2.55);
    SkPaint paint;
    paint.setAlpha(static_cast<U8CPU>(intAlpha));
    paint.setBlendMode(getBlendMode());

    canvas->saveLayer(nullptr, &paint);
    canvas->restore();
    canvas->restore();
}
