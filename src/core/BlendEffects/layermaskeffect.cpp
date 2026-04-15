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

#include "layermaskeffect.h"
#include "pathoperations.h"
#include "Boxes/pathbox.h"
#include "Boxes/smartvectorpath.h"
#include "Boxes/boundingbox.h"
#include "canvas.h"
#include "Animators/SmartPath/smartpathcollection.h"
#include "Animators/SmartPath/smartpathanimator.h"
#include "Animators/paintsettingsanimator.h"
#include "Animators/outlinesettingsanimator.h"
#include "Properties/property.h"
#include "BlendEffects/blendeffectcollection.h"
#include "Private/document.h"
#include "Private/esettings.h"
#include "swt_abstraction.h"
#include <QSignalBlocker>

namespace {

void configureMaskPathAnimatorUi(SmartPathCollection* const paths) {
    if(!paths) {
        return;
    }
    paths->prp_setName(QStringLiteral("Path"));
    paths->prp_setDrawingOnCanvasEnabled(true);
    const int childCount = paths->ca_getNumberOfChildren();
    if(childCount > 0) {
        if(auto* const firstPath = paths->ca_getChildAt<Property>(0)) {
            paths->ca_setGUIProperty(firstPath);
        }
    } else {
        paths->ca_setGUIProperty(nullptr);
    }
    for(int i = 0; i < childCount; ++i) {
        auto* const path = paths->ca_getChildAt<SmartPathAnimator>(i);
        if(!path) {
            continue;
        }
        path->prp_setName(QStringLiteral("Path"));
        path->prp_setDrawingOnCanvasEnabled(true);
    }
}

void syncMaskSelection(Canvas* const scene, BoundingBox* const target) {
    if(!scene || !target || !target->isSelected()) {
        return;
    }
    scene->clearSelectedProps();
    bool addedAny = false;
    target->ca_execOnDescendants([scene, &addedAny](Property* const prop) {
        const auto layerMask = enve_cast<LayerMaskEffect*>(prop);
        if(!layerMask) {
            return;
        }
        const auto vectorMask =
                enve_cast<SmartVectorPath*>(layerMask->maskPathSource());
        if(!vectorMask) {
            return;
        }
        auto* const paths = vectorMask->getPathAnimator();
        if(!paths) {
            return;
        }
        const int childCount = paths->ca_getNumberOfChildren();
        for(int i = 0; i < childCount; ++i) {
            if(auto* const child = paths->ca_getChildAt<Property>(i)) {
                scene->addToSelectedProps(child);
                addedAny = true;
            }
        }
        if(!addedAny) {
            scene->addToSelectedProps(paths);
        }
    });
}

void prepareMaskSource(PathBox* const maskPath) {
    if(!maskPath) {
        return;
    }
    const bool prevFillFlat = eSettings::instance().fLastFillFlatEnabled;
    const bool prevStrokeFlat = eSettings::instance().fLastStrokeFlatEnabled;
    maskPath->setVisible(false);
    maskPath->setVisibleForScene(false);
    if(auto* fill = maskPath->getFillSettings()) {
        const QSignalBlocker blocker(fill);
        fill->setPaintType(PaintType::NOPAINT);
    }
    if(auto* stroke = maskPath->getStrokeSettings()) {
        const QSignalBlocker blocker(stroke);
        stroke->setPaintType(PaintType::NOPAINT);
    }
    eSettings::sInstance->fLastFillFlatEnabled = prevFillFlat;
    eSettings::sInstance->fLastStrokeFlatEnabled = prevStrokeFlat;
    maskPath->setBlendModeSk(SkBlendMode::kSrcOver);
}

void configureConvertedMaskPath(PathBox* const maskPath,
                                const QString& maskName) {
    if(!maskPath) {
        return;
    }
    maskPath->prp_setName(maskName);
    maskPath->prp_setDrawingOnCanvasEnabled(false);

    const auto vectorMask = enve_cast<SmartVectorPath*>(maskPath);
    if(!vectorMask) {
        return;
    }

    auto* const paths = vectorMask->getPathAnimator();
    if(!paths) {
        return;
    }
    configureMaskPathAnimatorUi(paths);
}

Property* firstEditableMaskPath(PathBox* const maskPath) {
    const auto vectorMask = enve_cast<SmartVectorPath*>(maskPath);
    if(!vectorMask) {
        return nullptr;
    }
    auto* const paths = vectorMask->getPathAnimator();
    if(!paths) {
        return nullptr;
    }
    if(paths->ca_getNumberOfChildren() > 0) {
        if(auto* const firstPath = paths->ca_getChildAt<Property>(0)) {
            return firstPath;
        }
    }
    return paths;
}

void focusEditableMaskPath(Canvas* const scene,
                           BoundingBox* const target,
                           PathBox* const maskPath) {
    if(!scene || !target || !maskPath) {
        return;
    }
    scene->clearBoxesSelection();
    scene->addBoxToSelection(target);
    scene->clearPointsSelection();
    scene->clearSelectedProps();
    if(auto* const editable = firstEditableMaskPath(maskPath)) {
        scene->addToSelectedProps(editable);
    }
    scene->scheduleUpdate();
}

}

LayerMaskEffect::LayerMaskEffect()
    : BlendEffect("layer mask", BlendEffectType::layerMask)
{
    if(auto* clipPath = clipPathProperty()) {
        clipPath->prp_setName("path");
        clipPath->SWT_hide();
    }
    const auto modes = QStringList()
            << "None"
            << "Add"
            << "Subtract"
            << "Intersect"
            << "Lighten"
            << "Darken"
            << "Difference";
    mMode = enve::make_shared<ComboBoxProperty>("mode", modes);
    mMode->setCurrentValue(1);
    mFeather = enve::make_shared<QrealAnimator>(0, 0, 500, 0.1, "feather");
    mExpansion = enve::make_shared<QrealAnimator>(0, -500, 500, 0.1, "expansion");
    ca_addChild(mMode);
    ca_addChild(mFeather);
    ca_addChild(mExpansion);

    if(auto* clipPath = clipPathProperty()) {
        connect(clipPath, &BoxTargetProperty::targetSet,
                this, [this](BoundingBox*) {
            PathBox* const oldSource = mLastMaskPathSource;
            syncMaskPathUi(oldSource);
            mLastMaskPathSource = maskPathSource();
            syncMaskDisplayName();
        });
    }
}

void LayerMaskEffect::setClipPathSource(PathBox * const source)
{
    auto* const oldAnimator = maskPathAnimator();
    BlendEffect::setClipPathSource(source);
    auto* const newAnimator = maskPathAnimator();
    reconnectMaskPathAnimatorSignals(oldAnimator, newAnimator);
    configureMaskPathAnimatorUi(newAnimator);
    syncMaskDisplayName();
}

void LayerMaskEffect::syncMaskDisplayName()
{
    QString baseName = QStringLiteral("Layer Mask");
    if(auto* const source = maskPathSource()) {
        const QString sourceName = source->prp_getName().trimmed();
        if(!sourceName.isEmpty()) {
            baseName = sourceName;
        }
    }

    if(auto* const parent = getParent<BlendEffectCollection>()) {
        prp_setName(parent->makeNameUnique(baseName, this));
    } else {
        prp_setName(baseName);
    }
}

void LayerMaskEffect::SWT_setupAbstraction(
        SWT_Abstraction *abstraction,
        const UpdateFuncs &updateFuncs,
        const int visiblePartWidgetId)
{
    eEffect::SWT_setupAbstraction(abstraction, updateFuncs, visiblePartWidgetId);
    syncMaskPathAbstraction(abstraction, maskPathAnimator());
}

void LayerMaskEffect::prp_setupTreeViewMenu(PropertyMenu * const menu)
{
    if(!menu->hasActionsForType<LayerMaskEffect>()) {
        menu->addedActionsForType<LayerMaskEffect>();
        menu->addPlainAction<LayerMaskEffect>(
                    QIcon::fromTheme("draw-bezier-curves"),
                    tr("Convert Mask To Path"),
                    [](LayerMaskEffect * const effect) {
            auto* const source = effect->maskPathSource();
            if(!source || enve_cast<SmartVectorPath*>(source)) {
                return;
            }
            auto* const sourceParentTransform = source->getParentTransform();
            auto* const vectorPath = source->objectToVectorPathBox();
            if(!vectorPath) {
                return;
            }
            vectorPath->prp_setName(source->prp_getName());
            prepareMaskSource(vectorPath);
            configureConvertedMaskPath(vectorPath, source->prp_getName());
            vectorPath->setParentTransform(sourceParentTransform);
            effect->setClipPathSource(vectorPath);
            source->removeFromParent_k();
            if(auto* const target = effect->getFirstAncestor<BoundingBox>()) {
                target->prp_afterWholeInfluenceRangeChanged();
                if(auto* const scene = target->getParentScene()) {
                    focusEditableMaskPath(scene, target, vectorPath);
                }
            }
            Document::sInstance->actionFinished();
        })->setEnabled(maskPathSource() &&
                       !enve_cast<SmartVectorPath*>(maskPathSource()));
        menu->addSeparator();
    }
    BlendEffect::prp_setupTreeViewMenu(menu);
}

void LayerMaskEffect::blendSetup(ChildRenderData &data,
                                 const int index,
                                 const qreal relFrame,
                                 QList<ChildRenderData> &delayed) const
{
    Q_UNUSED(data)
    Q_UNUSED(index)
    Q_UNUSED(relFrame)
    Q_UNUSED(delayed)
}

void LayerMaskEffect::detachedBlendUISetup(const qreal relFrame,
                                           const int drawId,
                                           QList<UIDelayed> &delayed)
{
    Q_UNUSED(relFrame)
    Q_UNUSED(drawId)
    Q_UNUSED(delayed)
}

void LayerMaskEffect::detachedBlendSetup(const BoundingBox* const boxToDraw,
                                         const qreal relFrame,
                                         SkCanvas * const canvas,
                                         const SkFilterQuality filter,
                                         const int drawId,
                                         QList<Delayed> &delayed) const
{
    Q_UNUSED(boxToDraw)
    Q_UNUSED(relFrame)
    Q_UNUSED(canvas)
    Q_UNUSED(filter)
    Q_UNUSED(drawId)
    Q_UNUSED(delayed)
}

void LayerMaskEffect::drawBlendSetup(const qreal relFrame,
                                     SkCanvas * const canvas) const
{
    auto path = effectivePath(relFrame);
    if (path.isEmpty()) { return; }

    canvas->clipPath(path,
                     inverted() ? SkClipOp::kDifference
                                : SkClipOp::kIntersect,
                     true);
}

FrameRange LayerMaskEffect::prp_getIdenticalRelRange(const int relFrame) const
{
    auto range = eEffect::prp_getIdenticalRelRange(relFrame);

    if(mMode) {
        range *= mMode->prp_getIdenticalRelRange(relFrame);
    }
    if(mFeather) {
        range *= mFeather->prp_getIdenticalRelRange(relFrame);
    }
    if(mExpansion) {
        range *= mExpansion->prp_getIdenticalRelRange(relFrame);
    }

    auto* const paths = maskPathAnimator();
    if(!paths) {
        return range;
    }

    const int absFrame = prp_relFrameToAbsFrame(relFrame);
    const int pathRelFrame = paths->prp_absFrameToRelFrame(absFrame);
    const auto pathRange = paths->prp_getIdenticalRelRange(pathRelFrame);
    const auto pathAbsRange = paths->prp_relRangeToAbsRange(pathRange);
    return range * prp_absRangeToRelRange(pathAbsRange);
}

SkPath LayerMaskEffect::effectivePath(const qreal relFrame) const
{
    if (!isPathValid()) { return SkPath(); }
    const int relFrameI = qRound(relFrame);
    const auto * const owner = getFirstAncestor<BoundingBox>();
    const auto * const source = maskPathSource();
    const uint ownerStateId = owner ? owner->currentStateId() : 0;
    const uint sourceStateId = source ? source->currentStateId() : 0;
    const FrameRange identicalRange = prp_getIdenticalRelRange(relFrameI);
    {
        QReadLocker locker(&mEffectivePathCacheLock);
        if(mEffectivePathCache.fValid &&
           mEffectivePathCache.fOwnerStateId == ownerStateId &&
           mEffectivePathCache.fSourceStateId == sourceStateId &&
           mEffectivePathCache.fRange.inRange(relFrameI)) {
            return mEffectivePathCache.fPath;
        }
    }

    auto path = clipPath(relFrame);
    if (path.isEmpty()) {
        QWriteLocker locker(&mEffectivePathCacheLock);
        mEffectivePathCache = {true, identicalRange, ownerStateId, sourceStateId,
                               SkPath()};
        return SkPath();
    }

    if(mExpansion) {
        const qreal expansion = mExpansion->getEffectiveValue(relFrame);
        if(qAbs(expansion) > 0.0001) {
            SkPath expanded;
            gSolidify(expansion, path, &expanded);
            if(!expanded.isEmpty()) {
                path = expanded;
            }
        }
    }

    if(mFeather) {
        const qreal feather = mFeather->getEffectiveValue(relFrame);
        if(qAbs(feather) > 0.0001) {
            SkPath feathered;
            gSolidify(feather, path, &feathered);
            if(!feathered.isEmpty()) {
                path = feathered;
            }
        }
    }

    {
        QWriteLocker locker(&mEffectivePathCacheLock);
        mEffectivePathCache = {true, identicalRange, ownerStateId, sourceStateId,
                               path};
    }
    return path;
}

PathBox *LayerMaskEffect::maskPathSource() const
{
    return clipPathSource();
}

SmartPathCollection *LayerMaskEffect::maskPathAnimatorFor(PathBox *source) const
{
    const auto vectorMask = enve_cast<SmartVectorPath*>(source);
    if(!vectorMask) {
        return nullptr;
    }
    return vectorMask->getPathAnimator();
}

SmartPathCollection *LayerMaskEffect::maskPathAnimator() const
{
    return maskPathAnimatorFor(maskPathSource());
}

void LayerMaskEffect::syncMaskPathAbstraction(
        SWT_Abstraction *effectAbs,
        SmartPathCollection *pathAnimator) const
{
    if(!effectAbs || !pathAnimator) {
        return;
    }
    configureMaskPathAnimatorUi(pathAnimator);
    pathAnimator->SWT_show();
    auto* const pathAbs = pathAnimator->SWT_abstractionForWidget(
                effectAbs->updateFuncs(),
                effectAbs->getParentVisiblePartWidgetId());
    if(!pathAbs) {
        return;
    }
    if(auto* const oldParent = pathAbs->getParent()) {
        oldParent->removeChild(pathAbs->ref<SWT_Abstraction>());
    }
    effectAbs->addChildAbstractionAt(pathAbs->ref<SWT_Abstraction>(),
                                     sMaskPathTrackIndex);
}

void LayerMaskEffect::syncMaskPathUi(PathBox *oldSource)
{
    auto* const oldPathAnimator = maskPathAnimatorFor(oldSource);
    auto* const newPathAnimator = maskPathAnimator();
    configureMaskPathAnimatorUi(newPathAnimator);

    SWT_forEachAbstraction([this, oldPathAnimator, newPathAnimator](
                           SWT_Abstraction *effectAbs) {
        if(!effectAbs) {
            return;
        }
        const int widgetId = effectAbs->getParentVisiblePartWidgetId();
        if(oldPathAnimator) {
            if(auto* const oldAbs = oldPathAnimator->SWT_getAbstractionForWidget(widgetId)) {
                if(auto* const oldParent = oldAbs->getParent()) {
                    oldParent->removeChild(oldAbs->ref<SWT_Abstraction>());
                }
            }
        }
        syncMaskPathAbstraction(effectAbs, newPathAnimator);
    });

    if(auto* const target = getFirstAncestor<BoundingBox>()) {
        target->refreshCanvasControls();
        target->prp_afterWholeInfluenceRangeChanged();
        if(auto* const scene = target->getParentScene()) {
            syncMaskSelection(scene, target);
            scene->scheduleUpdate();
        }
    }
}

void LayerMaskEffect::reconnectMaskPathAnimatorSignals(
        SmartPathCollection *oldAnimator,
        SmartPathCollection *newAnimator)
{
    if(oldAnimator == newAnimator &&
       mMaskPathChildAddedConn &&
       mMaskPathChildRemovedConn &&
       mMaskPathChildMovedConn) {
        return;
    }

    if(mMaskPathChildAddedConn) {
        disconnect(mMaskPathChildAddedConn);
    }
    if(mMaskPathChildRemovedConn) {
        disconnect(mMaskPathChildRemovedConn);
    }
    if(mMaskPathChildMovedConn) {
        disconnect(mMaskPathChildMovedConn);
    }

    mMaskPathChildAddedConn = {};
    mMaskPathChildRemovedConn = {};
    mMaskPathChildMovedConn = {};

    if(!newAnimator) {
        return;
    }

    mMaskPathChildAddedConn = connect(newAnimator, &ComplexAnimator::ca_childAdded,
                                      this, [this](Property*) {
        handleMaskPathAnimatorStructureChanged();
    });
    mMaskPathChildRemovedConn = connect(newAnimator, &ComplexAnimator::ca_childRemoved,
                                        this, [this](Property*) {
        handleMaskPathAnimatorStructureChanged();
    });
    mMaskPathChildMovedConn = connect(newAnimator, &ComplexAnimator::ca_childMoved,
                                      this, [this](Property*) {
        handleMaskPathAnimatorStructureChanged();
    });
}

void LayerMaskEffect::handleMaskPathAnimatorStructureChanged()
{
    auto* const paths = maskPathAnimator();
    configureMaskPathAnimatorUi(paths);
    syncMaskPathUi(maskPathSource());
}

int LayerMaskEffect::modeValue() const
{
    return mMode ? mMode->getCurrentValue() : 0;
}

bool LayerMaskEffect::inverted() const
{
    return mMode && mMode->getCurrentValue() == 2;
}
