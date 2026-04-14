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

#include "canvas.h"
#include "MovablePoints/smartnodepoint.h"
#include "Boxes/smartvectorpath.h"
#include "eevent.h"
#include "Private/document.h"
#include "BlendEffects/layermaskeffect.h"
#include "GUI/dialogsinterface.h"
#include "../modules/ae_masks/aemaskmodule.h"

void Canvas::clearCurrentSmartEndPoint() {
    setCurrentSmartEndPoint(nullptr);
}

void Canvas::setCurrentSmartEndPoint(SmartNodePoint * const point) {
    if(mLastEndPoint) mLastEndPoint->setSelected(false);
    if(point) point->setSelected(true);
    mLastEndPoint = point;
}
#include "MovablePoints/pathpointshandler.h"
#include "Animators/SmartPath/smartpathcollection.h"
#include "Animators/transformanimator.h"

namespace {
void configureMaskPathAnimatorForCanvas(SmartVectorPath* const maskPath) {
    if(!maskPath) {
        return;
    }
    auto* const paths = maskPath->getPathAnimator();
    if(!paths) {
        return;
    }
    paths->prp_setName(QStringLiteral("Path"));
    paths->prp_setDrawingOnCanvasEnabled(true);
    const int pathCount = paths->ca_getNumberOfChildren();
    if(pathCount <= 0) {
        return;
    }
    if(auto* const firstPath = paths->ca_getChildAt<Property>(0)) {
        paths->ca_setGUIProperty(firstPath);
    }
    for(int i = 0; i < pathCount; ++i) {
        if(auto* const path = paths->ca_getChildAt<SmartPathAnimator>(i)) {
            path->prp_setName(QStringLiteral("Path"));
            path->prp_setDrawingOnCanvasEnabled(true);
        }
    }
}

void syncAeMaskSelection(Canvas* const scene, BoundingBox* const target) {
    AeMaskModule::syncSelection(scene, target);
    if(scene) scene->scheduleUpdate();
}
}

void Canvas::handleAddSmartPointMousePress(const eMouseEvent &e) {
    if(mLastEndPoint ? mLastEndPoint->isHidden(mCurrentMode) : false) {
        clearCurrentSmartEndPoint();
    }
    const qreal invScale = 1/e.fScale;
    qptr<BoundingBox> test;

    if (e.fModifiers & Qt::AltModifier) {
        if (const auto deletePoint =
                enve_cast<SmartNodePoint*>(getPointAtAbsPos(e.fPos,
                                                            CanvasMode::pointTransform,
                                                            invScale))) {
            clearPointsSelection();
            clearCurrentSmartEndPoint();
            clearLastPressedPoint();
            deletePoint->actionRemove(false);
            mPressedPoint = nullptr;
            mStartTransform = false;
            clearHovered();
            scheduleUpdate();
            return;
        }
    }

    if (mHoveredNormalSegment.isValid()) {
        clearPointsSelection();
        clearCurrentSmartEndPoint();
        mPressedPoint = mHoveredNormalSegment.divideAtAbsPos(snapEventPos(e, false));
        if (mPressedPoint && !mPressedPoint->isSelected()) {
            addPointToSelection(mPressedPoint);
        }
        mStartTransform = false;
        clearHovered();
        scheduleUpdate();
        return;
    }

    auto nodePointUnderMouse = static_cast<SmartNodePoint*>(mPressedPoint.data());
    if(nodePointUnderMouse ? !nodePointUnderMouse->isEndPoint() : false) {
        nodePointUnderMouse = nullptr;
    }
    if(nodePointUnderMouse == mLastEndPoint &&
            nodePointUnderMouse) return;
    if(!mLastEndPoint && !nodePointUnderMouse) {
        const auto newPath = enve::make_shared<SmartVectorPath>();
        newPath->planCenterPivotPosition();
        
        BoundingBox* maskTarget = nullptr;
        if (mCurrentBox) {
            maskTarget = mCurrentBox;
        }
        if(!maskTarget && !mSelectedBoxes.isEmpty()) {
            maskTarget = mSelectedBoxes.first();
        }
        
        if (maskTarget) {
            mCurrentContainer->addContained(newPath);
            newPath->prp_setName(Canvas::nextAeMaskName(maskTarget, nullptr));
            Canvas::attachLayerMaskEffect(maskTarget, newPath.get());
            DialogsInterface::instance().showStatusMessage(
                QObject::tr("AE: Created mask for %1")
                    .arg(maskTarget->prp_getName()));
        } else {
            mCurrentContainer->addContained(newPath);
        }
        
        clearBoxesSelection();
        if(maskTarget) {
            addBoxToSelection(maskTarget);
        } else {
            addBoxToSelection(newPath.get());
        }
        const QPointF snappedPos = snapEventPos(e, false);
        const auto newHandler = newPath->getPathAnimator();
        SmartNodePoint* node = nullptr;
        if(maskTarget) {
            node = newHandler->createNewSubPathAtPos(snappedPos);
        } else {
            const auto relPos = newPath->mapAbsPosToRel(snappedPos);
            newPath->getBoxTransformAnimator()->setPosition(relPos.x(), relPos.y());
            node = newHandler->createNewSubPathAtRelPos({0, 0});
        }
        configureMaskPathAnimatorForCanvas(newPath.get());
        setCurrentSmartEndPoint(node);
        if(maskTarget) {
            syncAeMaskSelection(this, maskTarget);
            maskTarget->refreshCanvasControls();
        }
    } else {
        if(!nodePointUnderMouse) {
            const QPointF snappedPos = snapEventPos(e, false);
            const auto newPoint = mLastEndPoint->actionAddPointAbsPos(snappedPos);
            //newPoint->startTransform();
            setCurrentSmartEndPoint(newPoint);
        } else if(!mLastEndPoint) {
            setCurrentSmartEndPoint(nodePointUnderMouse);
        } else { // mCurrentSmartEndPoint
            const auto targetNode = nodePointUnderMouse->getTargetNode();
            const auto handler = nodePointUnderMouse->getHandler();
            const bool success = nodePointUnderMouse->isEndPoint() &&
                    mLastEndPoint->actionConnectToNormalPoint(
                        nodePointUnderMouse);
            if(success) {
                const int newTargetId = targetNode->getNodeId();
                const auto sel = handler->getPointWithId<SmartNodePoint>(newTargetId);
                setCurrentSmartEndPoint(sel);
            }
        }
    } // pats is not null
}


void Canvas::handleAddSmartPointMouseMove(const eMouseEvent &e) {
    if(!mLastEndPoint) return;
    if(mStartTransform) mLastEndPoint->startTransform();
    const QPointF snappedPos = snapEventPos(e, false);
    if(mLastEndPoint->hasNextNormalPoint() &&
       mLastEndPoint->hasPrevNormalPoint()) {
        mLastEndPoint->setCtrlsMode(CtrlsMode::corner);
        mLastEndPoint->setC0Enabled(true);
        mLastEndPoint->moveC0ToAbsPos(snappedPos);
    } else {
        if(!mLastEndPoint->hasNextNormalPoint() &&
           !mLastEndPoint->hasPrevNormalPoint()) {            
            mLastEndPoint->setCtrlsMode(CtrlsMode::corner);
            mLastEndPoint->setC2Enabled(true);
        } else {
            mLastEndPoint->setCtrlsMode(CtrlsMode::symmetric);
        }
        if(mLastEndPoint->hasNextNormalPoint()) {
            mLastEndPoint->moveC0ToAbsPos(snappedPos);
        } else {
            mLastEndPoint->moveC2ToAbsPos(snappedPos);
        }
    }
}

void Canvas::handleAddSmartPointMouseRelease(const eMouseEvent &e) {
    Q_UNUSED(e)
    if(mLastEndPoint) {
        if(!mStartTransform) mLastEndPoint->finishTransform();
        //mCurrentSmartEndPoint->prp_afterWholeInfluenceRangeChanged();
        if(!mLastEndPoint->isEndPoint())
            clearCurrentSmartEndPoint();
    }
}
