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

#include "boxscrollwidget.h"
#include "optimalscrollarena/scrollarea.h"
#include "Boxes/boundingbox.h"
#include "Boxes/animationbox.h"
#include "Boxes/pathbox.h"
#include "Boxes/smartvectorpath.h"
#include "Boxes/frameremapping.h"
#include "Animators/eboxorsound.h"
#include "Animators/animator.h"
#include "Animators/qpointfanimator.h"
#include "Animators/qrealanimator.h"
#include "Animators/transformanimator.h"
#include "Boxes/containerbox.h"
#include "BlendEffects/layermaskeffect.h"
#include "boxscroller.h"
#include "GUI/canvaswindow.h"
#include "GUI/mainwindow.h"
#include "Private/document.h"
#include "canvas.h"
#include "Properties/property.h"
#include "singlewidgettarget.h"
#include "swt_abstraction.h"
#include "GUI/BoxesList/boxsinglewidget.h"

#include <QPointF>

BoxScrollWidget::BoxScrollWidget(Document &document,
                                 ScrollArea * const parent) :
    ScrollWidget(new BoxScroller(this), parent),
    mDocument(document) {
    const auto visPartWidget = visiblePartWidget();
    mCoreAbs = document.SWT_createAbstraction(
                visPartWidget->getUpdateFuncs(),
                visPartWidget->getId());
}

BoxScrollWidget::~BoxScrollWidget() {
    if(mCoreAbs) mCoreAbs->removeAlongWithAllChildren_k();
}

BoxScroller *BoxScrollWidget::getBoxScroller() {
    const auto visPartWidget = visiblePartWidget();
    return static_cast<BoxScroller*>(visPartWidget);
}

void BoxScrollWidget::setCurrentScene(Canvas * const scene) {
    getBoxScroller()->setCurrentScene(scene);
    mRevealScene.assign(scene);
    reapplyAeRevealPreset();
}

void BoxScrollWidget::setSiblingKeysView(KeysView * const keysView) {
    getBoxScroller()->setKeysView(keysView);
}

TimelineHighlightWidget *BoxScrollWidget::requestHighlighter() {
    return getBoxScroller()->requestHighlighter();
}

void BoxScrollWidget::toggleSolo(eBoxOrSound *target)
{
    const auto scene = *mDocument.fActiveScene;
    if (!scene || !target) {
        return;
    }

    auto restoreSolo = [this]() {
        for (const auto &entry : mSoloVisibility) {
            if (entry.first) {
                entry.first->setVisible(entry.second);
            }
        }
        mSoloVisibility.clear();
        mSoloTarget = nullptr;
    };

    if (mSoloTarget == target) {
        restoreSolo();
        Document::sInstance->actionFinished();
        return;
    }

    if (mSoloTarget) {
        restoreSolo();
    }

    for (auto *box : scene->getContainedBoxes()) {
        if (!box) { continue; }
        mSoloVisibility[box] = box->isVisible();
        box->setVisible(box == target);
    }
    mSoloTarget = target;
    Document::sInstance->actionFinished();
}

bool BoxScrollWidget::isSolo(const eBoxOrSound *target) const
{
    return target && mSoloTarget == target;
}

void BoxScrollWidget::applyAeRevealPreset(const AeRevealPreset preset)
{
    if (preset == AeRevealPreset::None) {
        clearAeRevealPreset();
        return;
    }

    const auto scene = *mDocument.fActiveScene;
    if (!scene) {
        return;
    }
    auto selected = scene->selectedBoxesList();
    if (selected.isEmpty()) {
        if (auto *current = scene->getCurrentBox()) {
            scene->addBoxToSelection(current);
            selected.append(current);
        }
    }
    if (selected.isEmpty()) {
        return;
    }

    if (mCurrentAeRevealPreset == preset) {
        for (const auto &box : selected) {
            applyAeRevealPresetToBox(box);
        }
        return;
    }

    restoreAeRevealState();
    if (!mHasStoredRules) {
        mStoredRules = getRulesCollection();
        mHasStoredRules = true;
    }
    mCurrentAeRevealPreset = preset;
    setAlwaysShowChildren(true);
    reapplyAeRevealPreset();
}

void BoxScrollWidget::clearAeRevealPreset()
{
    restoreAeRevealState();
    mCurrentAeRevealPreset = AeRevealPreset::None;
    if (mHasStoredRules) {
        const auto scene = mRevealScene ? *mRevealScene : *mDocument.fActiveScene;
        SingleWidgetTarget *target = nullptr;
        switch (mStoredRules.fTarget) {
        case SWT_Target::canvas:
            target = scene;
            break;
        case SWT_Target::group:
            target = scene ? scene->getCurrentGroup() : nullptr;
            break;
        case SWT_Target::all:
        default:
            target = nullptr;
            break;
        }
        setCurrentRule(mStoredRules.fRule);
        setCurrentType(mStoredRules.fType);
        setAlwaysShowChildren(mStoredRules.fAlwaysShowChildren);
        setCurrentTarget(target, mStoredRules.fTarget);
        mHasStoredRules = false;
    }
}

void BoxScrollWidget::dismissAeRevealPreset()
{
    mCurrentAeRevealPreset = AeRevealPreset::None;
    mStoredVisibility.clear();
    mStoredExpanded.clear();
    if (mHasStoredRules) {
        mStoredRules = getRulesCollection();
        mHasStoredRules = false;
    }
}

void BoxScrollWidget::revealSelectedFrameRemapping()
{
    applyAeRevealPreset(AeRevealPreset::FrameRemapping);
}

void BoxScrollWidget::revealSelectedMasks()
{
    applyAeRevealPreset(AeRevealPreset::Masks);
}

void BoxScrollWidget::toggleSelectedTransformVisibility()
{
    BoxSingleWidget *selectedPointRow = nullptr;
    for (auto *widget : visibleWidgets()) {
        auto *singleWidget = dynamic_cast<BoxSingleWidget*>(widget);
        if (!singleWidget) {
            continue;
        }

        auto *prop = singleWidget->targetProperty();
        auto *pointAnimator = prop ? enve_cast<QPointFAnimator*>(prop) : nullptr;
        if (pointAnimator && singleWidget->isPropertyRowSelected()) {
            selectedPointRow = singleWidget;
        }
    }

    if (selectedPointRow) {
        auto *abs = selectedPointRow->getTargetAbstraction();
        if (!abs) {
            abs = selectedPointRow->targetProperty()
                      ? selectedPointRow->targetProperty()->SWT_getAbstractionForWidget(getId())
                      : nullptr;
        }
        if (!abs) {
            return;
        }
        abs->setContentVisible(!abs->contentVisible());
        return;
    }

    const auto scene = *mDocument.fActiveScene;
    if (!scene) { return; }

    auto selected = scene->selectedBoxesList();
    if (selected.isEmpty()) {
        if (auto *current = scene->getCurrentBox()) {
            scene->addBoxToSelection(current);
            selected.append(current);
        }
    }
    if (selected.isEmpty()) { return; }

    bool shouldExpand = false;
    for (auto *box : selected) {
        if (!box) {
            continue;
        }
        auto *boxAbs = box->SWT_getAbstractionForWidget(getId());
        if (!boxAbs) {
            shouldExpand = true;
            break;
        }
        auto *transform = box->getTransformAnimator();
        auto *transformProp = enve_cast<Property*>(transform);
        auto *transformAbs = transformProp ? boxAbs->getChildAbsFor(transformProp) : nullptr;
        if (!transformAbs && transformProp) {
            transformAbs = transformProp->SWT_getAbstractionForWidget(getId());
        }
        if (!transformAbs || !transformAbs->contentVisible()) {
            shouldExpand = true;
            break;
        }
    }

    for (auto *box : selected) {
        if (!box) {
            continue;
        }

        auto *boxAbs = box->SWT_getAbstractionForWidget(getId());
        auto *transform = box->getTransformAnimator();
        auto *transformProp = enve_cast<Property*>(transform);
        if (!boxAbs || !transformProp) {
            continue;
        }

        if (shouldExpand) {
            boxAbs->setContentVisible(true);
        }
        transformProp->SWT_setVisible(true);

        auto *transformAbs = boxAbs->getChildAbsFor(transformProp);
        if (!transformAbs) {
            transformAbs = transformProp->SWT_getAbstractionForWidget(getId());
        }
        if (!transformAbs) { continue; }
        transformAbs->setContentVisible(shouldExpand);

        const int childCount = transform->ca_getNumberOfChildren();
        for (int i = 0; i < childCount; ++i) {
            auto *childProp = transform->ca_getChildAt<Property>(i);
            auto *pointAnimator = childProp ? enve_cast<QPointFAnimator*>(childProp) : nullptr;
            if (!pointAnimator) {
                continue;
            }

            childProp->SWT_setVisible(true);
            auto *pointAbs = transformAbs->getChildAbsFor(childProp);
            if (!pointAbs) {
                pointAbs = childProp->SWT_getAbstractionForWidget(getId());
            }
            if (!pointAbs) {
                continue;
            }
            pointAbs->setContentVisible(shouldExpand);
        }
    }
}

void BoxScrollWidget::restoreAeRevealState()
{
    for (const auto &state : mStoredVisibility) {
        if (state.first) {
            state.first->SWT_setVisible(state.second);
        }
    }
    mStoredVisibility.clear();

    for (const auto &state : mStoredExpanded) {
        if (state.first) {
            state.first->setContentVisible(state.second);
        }
    }
    mStoredExpanded.clear();
}

void BoxScrollWidget::reapplyAeRevealPreset()
{
    if (mCurrentAeRevealPreset == AeRevealPreset::None) {
        return;
    }

    restoreAeRevealState();

    const auto scene = *mDocument.fActiveScene;
    if (!scene) {
        return;
    }

    if (!mHasStoredRules) {
        mStoredRules = getRulesCollection();
        mHasStoredRules = true;
    }

    const auto rules = getRulesCollection();
    if (rules.fRule != SWT_BoxRule::all) {
        setCurrentRule(SWT_BoxRule::all);
    }
    if (rules.fType != SWT_Type::all) {
        setCurrentType(SWT_Type::all);
    }
    if (!rules.fAlwaysShowChildren) {
        setAlwaysShowChildren(true);
    }

    SingleWidgetTarget *target = scene;
    SWT_Target scope = SWT_Target::canvas;
    if (auto *group = scene->getCurrentGroup()) {
        target = group;
        scope = SWT_Target::group;
    }
    setCurrentTarget(target, scope);

    auto selected = scene->selectedBoxesList();
    if (selected.isEmpty()) {
        if (auto *current = scene->getCurrentBox()) {
            scene->addBoxToSelection(current);
            selected.append(current);
        }
    }
    if (selected.isEmpty()) {
        return;
    }

    for (const auto &box : selected) {
        applyAeRevealPresetToBox(box);
    }
}

void BoxScrollWidget::applyAeRevealPresetToBox(BoundingBox *box)
{
    if (!box) {
        return;
    }

    if (mCurrentAeRevealPreset == AeRevealPreset::FrameRemapping) {
        applyFrameRemappingRevealToBox(box);
        return;
    }

    if (mCurrentAeRevealPreset == AeRevealPreset::Masks) {
        applyMaskRevealToBox(box);
        return;
    }

    const auto boxAbs = box->SWT_getAbstractionForWidget(getId());
    setAbstractionExpandedTracked(boxAbs, true);

    const auto transform = box->getTransformAnimator();
    auto transformProp = enve_cast<Property*>(transform);
    if (!transformProp) {
        return;
    }

    const auto transformAbs = transformProp->SWT_getAbstractionForWidget(getId());
    setTargetVisibleTracked(transformProp, true);
    setAbstractionExpandedTracked(transformAbs, true);

    if (mCurrentAeRevealPreset == AeRevealPreset::AnchorPoint) {
        if (const auto advanced = enve_cast<AdvancedTransformAnimator*>(transform)) {
            revealPropertyTracked(advanced->getPivotAnimator());
            return;
        }
    } else if (mCurrentAeRevealPreset == AeRevealPreset::Position) {
        revealPropertyTracked(transform->getPosAnimator());
        return;
    } else if (mCurrentAeRevealPreset == AeRevealPreset::Scale) {
        revealPropertyTracked(transform->getScaleAnimator());
        return;
    } else if (mCurrentAeRevealPreset == AeRevealPreset::Rotation) {
        revealPropertyTracked(transform->getRotAnimator());
        return;
    } else if (mCurrentAeRevealPreset == AeRevealPreset::Opacity) {
        if (const auto advanced = enve_cast<AdvancedTransformAnimator*>(transform)) {
            revealPropertyTracked(advanced->getOpacityAnimator());
            return;
        }
    }

    const int nChildren = transform->ca_getNumberOfChildren();
    for (int i = 0; i < nChildren; i++) {
        const auto child = transform->ca_getChildAt<Property>(i);
        if (!child) {
            continue;
        }
        const bool visible = matchesRevealPreset(child, mCurrentAeRevealPreset);
        setTargetVisibleTracked(child, visible);
        if (visible) {
            setAbstractionExpandedTracked(child->SWT_getAbstractionForWidget(getId()), true);
        }
    }
}

void BoxScrollWidget::revealPropertyTracked(Property *property)
{
    if (!property) {
        return;
    }

    setTargetVisibleTracked(property, true);
    setAbstractionExpandedTracked(property->SWT_getAbstractionForWidget(getId()), true);

    if (const auto complex = enve_cast<ComplexAnimator*>(property)) {
        const int childCount = complex->ca_getNumberOfChildren();
        for (int i = 0; i < childCount; ++i) {
            if (auto *child = complex->ca_getChildAt<Property>(i)) {
                setTargetVisibleTracked(child, true);
                setAbstractionExpandedTracked(child->SWT_getAbstractionForWidget(getId()), true);
            }
        }
    }
}

void BoxScrollWidget::applyFrameRemappingRevealToBox(BoundingBox *box)
{
    const auto animBox = enve_cast<AnimationBox*>(box);
    if (!animBox || !animBox->frameRemappingEnabled()) {
        return;
    }

    setAbstractionExpandedTracked(box->SWT_getAbstractionForWidget(getId()), true);

    auto *remap = animBox->getFrameRemapping();
    if (!remap) {
        return;
    }
    setTargetVisibleTracked(remap, true);
    setAbstractionExpandedTracked(remap->SWT_getAbstractionForWidget(getId()), true);
}

void BoxScrollWidget::applyMaskRevealToBox(BoundingBox *box)
{
    auto *parent = box->getParentGroup();
    if (!parent) {
        return;
    }

    setTargetVisibleTracked(box, true);
    setAbstractionExpandedTracked(box->SWT_getAbstractionForWidget(getId()), true);

    const auto &siblings = parent->getContainedBoxes();
    const int boxIndex = siblings.indexOf(box);
    if (boxIndex < 0) {
        return;
    }

    box->ca_execOnDescendants([this](Property *prop) {
        const auto layerMask = enve_cast<LayerMaskEffect*>(prop);
        if (!layerMask) {
            return;
        }
        setTargetVisibleTracked(layerMask, true);
        setAbstractionExpandedTracked(layerMask->SWT_getAbstractionForWidget(getId()), true);

        const int childCount = layerMask->ca_getNumberOfChildren();
        for (int i = 0; i < childCount; ++i) {
            auto *child = layerMask->ca_getChildAt<Property>(i);
            if (!child) {
                continue;
            }
            if (child->prp_getName() == QStringLiteral("path")) {
                continue;
            }
            setTargetVisibleTracked(child, true);
            setAbstractionExpandedTracked(child->SWT_getAbstractionForWidget(getId()), true);
        }

        auto *maskPath = layerMask->maskPathSource();
        if (!maskPath) {
            return;
        }
        if (const auto vectorPath = enve_cast<SmartVectorPath*>(maskPath)) {
            if (auto *pathAnimator = vectorPath->getPathAnimator()) {
                setTargetVisibleTracked(pathAnimator, true);
                setAbstractionExpandedTracked(pathAnimator->SWT_getAbstractionForWidget(getId()), true);
                const int pathChildCount = pathAnimator->ca_getNumberOfChildren();
                for (int j = 0; j < pathChildCount; ++j) {
                    auto *pathChild = pathAnimator->ca_getChildAt<Property>(j);
                    if (!pathChild) {
                        continue;
                    }
                    setTargetVisibleTracked(pathChild, true);
                    setAbstractionExpandedTracked(pathChild->SWT_getAbstractionForWidget(getId()), true);
                    if (const auto complexPathChild = enve_cast<ComplexAnimator*>(pathChild)) {
                        complexPathChild->ca_execOnDescendants([this](Property *pathDesc) {
                            if (!pathDesc) {
                                return;
                            }
                            setTargetVisibleTracked(pathDesc, true);
                            setAbstractionExpandedTracked(pathDesc->SWT_getAbstractionForWidget(getId()), true);
                        });
                    }
                }
            }
        }
    });

    for (auto *candidate : siblings) {
        if (!candidate || candidate == box) {
            continue;
        }
        const auto blendMode = candidate->getBlendMode();
        if (blendMode != SkBlendMode::kDstIn &&
            blendMode != SkBlendMode::kDstOut) {
            continue;
        }
        const int candidateIndex = siblings.indexOf(candidate);
        if (candidateIndex < 0 || candidateIndex > boxIndex) {
            continue;
        }

        setTargetVisibleTracked(candidate, true);
        setAbstractionExpandedTracked(candidate->SWT_getAbstractionForWidget(getId()), true);
    }
}

void BoxScrollWidget::setTargetVisibleTracked(SingleWidgetTarget *target,
                                              bool visible)
{
    if (!target) {
        return;
    }
    if (mStoredVisibility.find(target) == mStoredVisibility.end()) {
        mStoredVisibility[target] = target->SWT_isVisible();
    }
    target->SWT_setVisible(visible);
}

void BoxScrollWidget::setAbstractionExpandedTracked(SWT_Abstraction *abstraction,
                                                    bool expanded)
{
    if (!abstraction) {
        return;
    }
    if (mStoredExpanded.find(abstraction) == mStoredExpanded.end()) {
        mStoredExpanded[abstraction] = abstraction->contentVisible();
    }
    abstraction->setContentVisible(expanded);
}

bool BoxScrollWidget::matchesRevealPreset(Property *property,
                                          const AeRevealPreset preset) const
{
    if (!property) {
        return false;
    }

    const QString name = property->prp_getName().trimmed().toLower();
    switch (preset) {
    case AeRevealPreset::AnchorPoint:
        return name == "pivot" ||
               name == "pivot point" ||
               name == "anchor" ||
               name == "anchor point";
    case AeRevealPreset::Position:
        return name == "translation" ||
               name == "position";
    case AeRevealPreset::Scale:
        return name == "scale";
    case AeRevealPreset::Rotation:
        return name == "rotation";
    case AeRevealPreset::Opacity:
        return name == "opacity";
    case AeRevealPreset::Keyframed: {
        if (const auto animator = enve_cast<Animator*>(property)) {
            return animator->anim_hasKeys();
        }
        return false;
    }
    case AeRevealPreset::Modified: {
        if (const auto point = enve_cast<QPointFAnimator*>(property)) {
            const QPointF value = point->getBaseValue();
            if (name == "translation" || name == "pivot" || name == "shear") {
                return !qFuzzyCompare(value.x() + 1.0, 1.0) ||
                       !qFuzzyCompare(value.y() + 1.0, 1.0);
            }
            if (name == "scale") {
                return !qFuzzyCompare(value.x(), 1.0) ||
                       !qFuzzyCompare(value.y(), 1.0);
            }
        } else if (const auto real = enve_cast<QrealAnimator*>(property)) {
            const qreal value = real->getCurrentBaseValue();
            if (name == "rotation") {
                return !qFuzzyIsNull(value);
            }
            if (name == "opacity") {
                return !qFuzzyCompare(value, 100.0);
            }
        }
        return false;
    }
    case AeRevealPreset::FrameRemapping:
    case AeRevealPreset::Masks:
    default:
        return false;
    }
}
