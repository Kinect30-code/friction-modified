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

#ifndef LAYERMASKEFFECT_H
#define LAYERMASKEFFECT_H

#include "blendeffect.h"
#include "Properties/comboboxproperty.h"
#include "Animators/qrealanimator.h"
#include <QMetaObject>

class SmartPathCollection;
class SWT_Abstraction;

class CORE_EXPORT LayerMaskEffect : public BlendEffect {
public:
    LayerMaskEffect();

    void setClipPathSource(PathBox * const source);
    void syncMaskDisplayName();

    void SWT_setupAbstraction(SWT_Abstraction *abstraction,
                              const UpdateFuncs &updateFuncs,
                              const int visiblePartWidgetId) override;

    void prp_setupTreeViewMenu(PropertyMenu * const menu) override;

    void blendSetup(ChildRenderData &data,
                    const int index,
                    const qreal relFrame,
                    QList<ChildRenderData> &delayed) const override;

    void detachedBlendUISetup(const qreal relFrame,
                              const int drawId,
                              QList<UIDelayed> &delayed) override;
    void detachedBlendSetup(const BoundingBox* const boxToDraw,
                            const qreal relFrame,
                            SkCanvas * const canvas,
                            const SkFilterQuality filter,
                            const int drawId,
                            QList<Delayed> &delayed) const override;
    void drawBlendSetup(const qreal relFrame,
                        SkCanvas * const canvas) const override;
    FrameRange prp_getIdenticalRelRange(const int relFrame) const override;

    SkPath effectivePath(const qreal relFrame) const;
    PathBox* maskPathSource() const;
    int modeValue() const;
    bool inverted() const;

    static constexpr int sMaskPathTrackIndex = 1;

private:
    SmartPathCollection* maskPathAnimatorFor(PathBox* source) const;
    SmartPathCollection* maskPathAnimator() const;
    void syncMaskPathAbstraction(
            SWT_Abstraction *effectAbs,
            SmartPathCollection *pathAnimator) const;
    void syncMaskPathUi(PathBox *oldSource);
    void reconnectMaskPathAnimatorSignals(
            SmartPathCollection *oldAnimator,
            SmartPathCollection *newAnimator);
    void handleMaskPathAnimatorStructureChanged();

private:
    qsptr<ComboBoxProperty> mMode;
    qsptr<QrealAnimator> mFeather;
    qsptr<QrealAnimator> mExpansion;
    qptr<PathBox> mLastMaskPathSource;
    QMetaObject::Connection mMaskPathChildAddedConn;
    QMetaObject::Connection mMaskPathChildRemovedConn;
    QMetaObject::Connection mMaskPathChildMovedConn;
};

#endif // LAYERMASKEFFECT_H
