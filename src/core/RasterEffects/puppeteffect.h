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

#ifndef PUPPETEFFECT_H
#define PUPPETEFFECT_H

#include "rastereffect.h"
#include "Animators/dynamiccomplexanimator.h"
#include "Animators/intanimator.h"
#include "Animators/qrealanimator.h"
#include "Animators/staticcomplexanimator.h"
#include "Animators/qpointfanimator.h"
#include <QVector>

class CORE_EXPORT PuppetPin : public StaticComplexAnimator {
    e_OBJECT
public:
    PuppetPin();
    void prp_setupTreeViewMenu(PropertyMenu * const menu) override;

    QPointF normalizedSource() const;
    QPointF normalizedPosition(const qreal relFrame) const;
    QPointF normalizedPosition() const;
    void setNormalizedSource(const QPointF& value);
    void setNormalizedPosition(const QPointF& value);
    void initializeAt(const QPointF& normalizedPos);

    QPointFAnimator* positionAnimator() const;
    QPointFAnimator* sourceAnimator() const;
private:
    qsptr<QPointFAnimator> mPosition;
    qsptr<QPointFAnimator> mSourcePosition;
};

typedef DynamicComplexAnimator<PuppetPin> PuppetPinCollectionBase;

class CORE_EXPORT PuppetDeformAnimator : public PuppetPinCollectionBase {
    e_OBJECT
public:
    PuppetDeformAnimator();
    void prp_setupTreeViewMenu(PropertyMenu * const menu) override;
    PuppetPin* addPinAtNormalized(const QPointF& normalizedPos);
};

class CORE_EXPORT PuppetMeshAnimator : public StaticComplexAnimator {
    e_OBJECT
public:
    PuppetMeshAnimator();
    PuppetDeformAnimator* deform() const;
private:
    qsptr<PuppetDeformAnimator> mDeform;
};

class CORE_EXPORT PuppetEffect : public RasterEffect {
    e_OBJECT
public:
    PuppetEffect();
    ~PuppetEffect() override;

    stdsptr<RasterEffectCaller> getEffectCaller(
            const qreal relFrame,
            const qreal resolution,
            const qreal influence,
            BoxRenderData * const data) const override;

    void prp_setupTreeViewMenu(PropertyMenu * const menu) override;
    void prp_drawCanvasControls(
            SkCanvas * const canvas,
            const CanvasMode mode,
            const float invScale,
            const bool ctrlPressed) override;

    PuppetPin* addPinAtNormalized(const QPointF& normalizedPos);
    QPointF defaultNewPinPosition() const;
    PuppetDeformAnimator* deform() const;
    void notifyPinsChanged();
private:
    struct PreviewCache {
        int width = 0;
        int height = 0;
        int density = 0;
        qreal expansion = -1.0;
        QVector<QPointF> sourcePins;
    };

    qsptr<IntAnimator> mMeshDensity;
    qsptr<QrealAnimator> mExpansion;
    qsptr<QrealAnimator> mPinStiffness;
    qsptr<PuppetMeshAnimator> mMesh;
    mutable PreviewCache mPreviewCache;
};

#endif // PUPPETEFFECT_H
