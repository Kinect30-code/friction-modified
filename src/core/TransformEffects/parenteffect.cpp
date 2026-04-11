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

#include "parenteffect.h"

#include "Boxes/boundingbox.h"
#include "Animators/transformanimator.h"
#include "Animators/qpointfanimator.h"
#include "Animators/qrealanimator.h"
#include "Animators/qrealkey.h"
#include "matrixdecomposition.h"

#include <set>

namespace {

void collectAnimatorFrames(Animator* const animator,
                           std::set<int>& frames) {
    if(!animator) return;
    for(const auto &key : animator->anim_getKeys()) {
        frames.insert(key->getAbsFrame());
    }
}

void collectQPointAnimatorFrames(QPointFAnimator* const animator,
                                 std::set<int>& frames) {
    if(!animator) return;
    collectAnimatorFrames(animator->getXAnimator(), frames);
    collectAnimatorFrames(animator->getYAnimator(), frames);
}

void collectTransformFrames(BasicTransformAnimator* const animator,
                            std::set<int>& frames) {
    if(!animator) return;
    collectQPointAnimatorFrames(animator->getPosAnimator(), frames);
    collectQPointAnimatorFrames(animator->getScaleAnimator(), frames);
    collectAnimatorFrames(animator->getRotAnimator(), frames);
    const auto advancedAnimator = static_cast<AdvancedTransformAnimator*>(animator);
    collectQPointAnimatorFrames(advancedAnimator->getPivotAnimator(), frames);
    collectQPointAnimatorFrames(advancedAnimator->getShearAnimator(), frames);
}

void setKeyValue(QrealAnimator* const animator,
                 const int absFrame,
                 const qreal value) {
    if(!animator) return;
    const auto key = animator->anim_getKeyAtAbsFrame<QrealKey>(absFrame);
    if(!key) return;
    key->startValueTransform();
    key->setValue(value);
    key->finishValueTransform();
}

void setBaseValueIfUnkeyed(QrealAnimator* const animator,
                           const qreal value) {
    if(!animator || animator->anim_hasKeys()) return;
    animator->setCurrentBaseValue(value);
}

void applyCurrentBaseValues(AdvancedTransformAnimator* const animator,
                            const TransformValues& values) {
    if(!animator) return;
    setBaseValueIfUnkeyed(animator->getPosAnimator()->getXAnimator(), values.fMoveX);
    setBaseValueIfUnkeyed(animator->getPosAnimator()->getYAnimator(), values.fMoveY);
    setBaseValueIfUnkeyed(animator->getScaleAnimator()->getXAnimator(), values.fScaleX);
    setBaseValueIfUnkeyed(animator->getScaleAnimator()->getYAnimator(), values.fScaleY);
    setBaseValueIfUnkeyed(animator->getRotAnimator(), values.fRotation);
    setBaseValueIfUnkeyed(animator->getPivotAnimator()->getXAnimator(), values.fPivotX);
    setBaseValueIfUnkeyed(animator->getPivotAnimator()->getYAnimator(), values.fPivotY);
    setBaseValueIfUnkeyed(animator->getShearAnimator()->getXAnimator(), values.fShearX);
    setBaseValueIfUnkeyed(animator->getShearAnimator()->getYAnimator(), values.fShearY);
}

void applyExistingKeyValues(AdvancedTransformAnimator* const animator,
                            const int absFrame,
                            const TransformValues& values) {
    if(!animator) return;
    setKeyValue(animator->getPosAnimator()->getXAnimator(), absFrame, values.fMoveX);
    setKeyValue(animator->getPosAnimator()->getYAnimator(), absFrame, values.fMoveY);
    setKeyValue(animator->getScaleAnimator()->getXAnimator(), absFrame, values.fScaleX);
    setKeyValue(animator->getScaleAnimator()->getYAnimator(), absFrame, values.fScaleY);
    setKeyValue(animator->getRotAnimator(), absFrame, values.fRotation);
    setKeyValue(animator->getPivotAnimator()->getXAnimator(), absFrame, values.fPivotX);
    setKeyValue(animator->getPivotAnimator()->getYAnimator(), absFrame, values.fPivotY);
    setKeyValue(animator->getShearAnimator()->getXAnimator(), absFrame, values.fShearX);
    setKeyValue(animator->getShearAnimator()->getYAnimator(), absFrame, values.fShearY);
}

void coordinateVec2AnimatorKeys(QPointFAnimator* const animator) {
    if(!animator) return;
    auto * const x = animator->getXAnimator();
    auto * const y = animator->getYAnimator();
    if(!x || !y) return;
    x->anim_coordinateKeysWith(y);
}

void calculateParentEffectContribution(
        const TargetTransformEffect* const effect,
        BoundingBox* const target,
        QPointFAnimator* const posInfluence,
        QPointFAnimator* const scaleInfluence,
        QrealAnimator* const rotInfluence,
        const qreal relFrame,
        QMatrix& postTransform,
        qreal& rotDelta) {
    postTransform.reset();
    rotDelta = 0.;
    if(!effect || !target || !posInfluence || !scaleInfluence ||
       !rotInfluence || !effect->isVisible()) return;

    const qreal absFrame = effect->prp_relFrameToAbsFrameF(relFrame);
    const qreal targetRelFrame = target->prp_absFrameToRelFrameF(absFrame);
    const auto targetTransform = target->getTransformAnimator()->
            getRelativeTransformAtFrame(targetRelFrame);
    const auto targetPivot = target->getPivotRelPos(targetRelFrame);
    const auto targetValues =
            MatrixDecomposition::decomposePivoted(targetTransform, targetPivot);

    const qreal posXInfl = qBound(-10.0, posInfluence->getEffectiveXValue(relFrame), 10.0);
    const qreal posYInfl = qBound(-10.0, posInfluence->getEffectiveYValue(relFrame), 10.0);
    const qreal scaleXInfl = qBound(-10.0, scaleInfluence->getEffectiveXValue(relFrame), 10.0);
    const qreal scaleYInfl = qBound(-10.0, scaleInfluence->getEffectiveYValue(relFrame), 10.0);
    const qreal rotInfl = qBound(-10.0, rotInfluence->getEffectiveValue(relFrame), 10.0);
    if(!std::isfinite(posXInfl) || !std::isfinite(posYInfl) ||
       !std::isfinite(scaleXInfl) || !std::isfinite(scaleYInfl) ||
       !std::isfinite(rotInfl)) {
        return;
    }

    TransformValues influencedValues = targetValues;
    influencedValues.fMoveX = targetValues.fMoveX * posXInfl;
    influencedValues.fMoveY = targetValues.fMoveY * posYInfl;
    influencedValues.fScaleX = 1.0 + (targetValues.fScaleX - 1.0) * scaleXInfl;
    influencedValues.fScaleY = 1.0 + (targetValues.fScaleY - 1.0) * scaleYInfl;
    influencedValues.fRotation = targetValues.fRotation;
    postTransform = influencedValues.calculate();

    const bool zeroRotInfluence = std::abs(rotInfl) < 1e-6;
    const qreal desiredRotation = zeroRotInfluence
            ? -targetValues.fRotation
            : targetValues.fRotation * rotInfl;
    const qreal rotDeltaZero = -targetValues.fRotation;
    const qreal rotDeltaFull = desiredRotation - influencedValues.fRotation;
    if(rotInfl >= 0.0 && rotInfl <= 1.0) {
        const qreal t = rotInfl;
        rotDelta = rotDeltaZero + t * (rotDeltaFull - rotDeltaZero);
    } else if(zeroRotInfluence) {
        rotDelta = rotDeltaZero;
    } else {
        rotDelta = rotDeltaFull;
    }
}

}

ParentEffect::ParentEffect() :
    FollowObjectEffectBase("parent", TransformEffectType::parent) {}

void ParentEffect::beforeTargetChanged(BoundingBox* const parent,
                                       BoundingBox* const oldTarget,
                                       BoundingBox* const newTarget)
{
    Q_UNUSED(oldTarget)
    Q_UNUSED(newTarget)

    mTargetChangeCurrentFrameValid = false;
    mTargetChangeFrames.clear();
    mTargetChangeTotalTransforms.clear();

    if(!parent) return;

    const qreal currentRelFrame = parent->anim_getCurrentRelFrame();
    mTargetChangeCurrentFrame = parent->anim_getCurrentAbsFrame();
    mTargetChangeCurrentTotalTransform =
            parent->getTotalTransformAtFrame(currentRelFrame);
    mTargetChangeCurrentFrameValid = true;

    std::set<int> frames;
    collectTransformFrames(parent->getTransformAnimator(), frames);
    if(frames.empty()) return;

    mTargetChangeFrames.reserve(int(frames.size()));
    mTargetChangeTotalTransforms.reserve(int(frames.size()));
    for(const int absFrame : frames) {
        const qreal relFrame = parent->prp_absFrameToRelFrameF(absFrame);
        mTargetChangeFrames.append(absFrame);
        mTargetChangeTotalTransforms.append(
                parent->getTotalTransformAtFrame(relFrame));
    }
}

bool ParentEffect::applyTargetChangeCompensation(BoundingBox* const parent,
                                                 BoundingBox* const oldTarget,
                                                 BoundingBox* const newTarget)
{
    if(!parent || !mTargetChangeCurrentFrameValid) {
        mTargetChangeCurrentFrameValid = false;
        mTargetChangeFrames.clear();
        mTargetChangeTotalTransforms.clear();
        return false;
    }

    const int savedFrame = parent->anim_getCurrentAbsFrame();
    auto * const transformAnimator =
            static_cast<AdvancedTransformAnimator*>(parent->getTransformAnimator());
    coordinateVec2AnimatorKeys(transformAnimator->getPosAnimator());
    coordinateVec2AnimatorKeys(transformAnimator->getScaleAnimator());
    coordinateVec2AnimatorKeys(transformAnimator->getPivotAnimator());
    coordinateVec2AnimatorKeys(transformAnimator->getShearAnimator());
    parent->anim_setAbsFrame(mTargetChangeCurrentFrame);
    const qreal currentRelFrame = parent->anim_getCurrentRelFrame();

    if(oldTarget && !newTarget) {
        bool actualInvertible = false;
        const auto actualTotalNow = parent->getTotalTransformAtFrame(currentRelFrame);
        const auto actualTotalInverse =
                actualTotalNow.inverted(&actualInvertible);
        if(!actualInvertible) {
            parent->anim_setAbsFrame(savedFrame);
            mTargetChangeCurrentFrameValid = false;
            mTargetChangeFrames.clear();
            mTargetChangeTotalTransforms.clear();
            return false;
        }

        const auto delta =
                mTargetChangeCurrentTotalTransform*actualTotalInverse;

        const auto currentPivot = parent->getPivotRelPos(currentRelFrame);
        const auto currentRelMatrix = parent->getRelativeTransformAtFrame(currentRelFrame);

        QVector<QMatrix> relMatrices;
        QVector<QPointF> pivots;
        relMatrices.reserve(mTargetChangeFrames.size());
        pivots.reserve(mTargetChangeFrames.size());
        const int nFrames = mTargetChangeFrames.size();
        for(int i = 0; i < nFrames; ++i) {
            const int absFrame = mTargetChangeFrames.at(i);
            parent->anim_setAbsFrame(absFrame);
            const qreal relFrame = parent->anim_getCurrentRelFrame();
            pivots.append(parent->getPivotRelPos(relFrame));
            relMatrices.append(parent->getRelativeTransformAtFrame(relFrame));
        }

        {
            parent->anim_setAbsFrame(mTargetChangeCurrentFrame);
            const auto targetRelMatrix =
                    delta*currentRelMatrix;
            auto values = MatrixDecomposition::decomposePivoted(
                    targetRelMatrix, currentPivot);
            values.fPivotX = currentPivot.x();
            values.fPivotY = currentPivot.y();
            applyCurrentBaseValues(transformAnimator, values);
        }

        for(int i = 0; i < nFrames; ++i) {
            const int absFrame = mTargetChangeFrames.at(i);
            parent->anim_setAbsFrame(absFrame);
            const auto pivot = pivots.at(i);
            const auto targetRelMatrix =
                    delta*relMatrices.at(i);
            auto values = MatrixDecomposition::decomposePivoted(
                    targetRelMatrix, pivot);
            values.fPivotX = pivot.x();
            values.fPivotY = pivot.y();
            applyExistingKeyValues(transformAnimator, absFrame, values);
        }

        parent->anim_setAbsFrame(savedFrame);
        mTargetChangeCurrentFrameValid = false;
        mTargetChangeFrames.clear();
        mTargetChangeTotalTransforms.clear();
        return true;
    }

    bool inheritedInvertible = false;
    const auto inheritedInverse =
            parent->getInheritedTransformAtFrame(currentRelFrame)
            .inverted(&inheritedInvertible);
    if(!inheritedInvertible) {
        parent->anim_setAbsFrame(savedFrame);
        mTargetChangeCurrentFrameValid = false;
        mTargetChangeFrames.clear();
        mTargetChangeTotalTransforms.clear();
        return false;
    }
    const auto desiredRelativeTransform =
            mTargetChangeCurrentTotalTransform*inheritedInverse;
    QMatrix currentPostTransform;
    qreal currentRotDelta = 0.;
    calculateParentEffectContribution(this, targetProperty()->getTarget(),
                                      mPosInfluence.get(),
                                      mScaleInfluence.get(),
                                      mRotInfluence.get(),
                                      currentRelFrame,
                                      currentPostTransform,
                                      currentRotDelta);
    {
        const auto pivot = parent->getPivotRelPos(currentRelFrame);
        bool postInvertible = false;
        const auto postInverse = currentPostTransform.inverted(&postInvertible);
        const auto localMatrix = postInvertible ?
                desiredRelativeTransform*postInverse :
                desiredRelativeTransform;
        auto values = MatrixDecomposition::decomposePivoted(
                localMatrix, pivot);
        values.fRotation -= currentRotDelta;
        values.fPivotX = pivot.x();
        values.fPivotY = pivot.y();
        applyCurrentBaseValues(transformAnimator, values);
    }

    const int nFrames = mTargetChangeFrames.size();
    for(int i = 0; i < nFrames; ++i) {
        const int absFrame = mTargetChangeFrames.at(i);
        parent->anim_setAbsFrame(absFrame);
        const qreal relFrame = parent->anim_getCurrentRelFrame();
        bool frameInheritedInvertible = false;
        const auto frameInheritedInverse =
                parent->getInheritedTransformAtFrame(relFrame)
                .inverted(&frameInheritedInvertible);
        if(!frameInheritedInvertible) continue;
        const auto desiredRel =
                mTargetChangeTotalTransforms.at(i)*frameInheritedInverse;
        QMatrix postTransform;
        qreal rotDelta = 0.;
        calculateParentEffectContribution(this, targetProperty()->getTarget(),
                                          mPosInfluence.get(),
                                          mScaleInfluence.get(),
                                          mRotInfluence.get(),
                                          relFrame,
                                          postTransform,
                                          rotDelta);
        const auto pivot = parent->getPivotRelPos(relFrame);
        bool postInvertible = false;
        const auto postInverse = postTransform.inverted(&postInvertible);
        const auto localMatrix = postInvertible ?
                desiredRel*postInverse :
                desiredRel;
        auto values = MatrixDecomposition::decomposePivoted(
                localMatrix, pivot);
        values.fRotation -= rotDelta;
        values.fPivotX = pivot.x();
        values.fPivotY = pivot.y();
        applyExistingKeyValues(transformAnimator, absFrame, values);
    }

    parent->anim_setAbsFrame(savedFrame);
    mTargetChangeCurrentFrameValid = false;
    mTargetChangeFrames.clear();
    mTargetChangeTotalTransforms.clear();
    return true;
}

void ParentEffect::applyEffect(const qreal relFrame,
                               qreal& pivotX,
                               qreal& pivotY,
                               qreal& posX,
                               qreal& posY,
                               qreal& rot,
                               qreal& scaleX,
                               qreal& scaleY,
                               qreal& shearX,
                               qreal& shearY,
                               QMatrix& postTransform,
                               BoundingBox* const parent)
{
    Q_UNUSED(pivotX)
    Q_UNUSED(pivotY)
    Q_UNUSED(posX)
    Q_UNUSED(posY)
    Q_UNUSED(rot)
    Q_UNUSED(scaleX)
    Q_UNUSED(scaleY)
    Q_UNUSED(shearX)
    Q_UNUSED(shearY)

    if (!isVisible() || !parent) { return; }

    const auto target = targetProperty()->getTarget();
    if (!target) { return; }

    qreal rotDelta = 0.;
    calculateParentEffectContribution(this, target,
                                      mPosInfluence.get(),
                                      mScaleInfluence.get(),
                                      mRotInfluence.get(),
                                      relFrame,
                                      postTransform,
                                      rotDelta);
    rot += rotDelta;
}

bool ParentEffect::validateInfluenceValues(const qreal posXInfl,
                                           const qreal posYInfl,
                                           const qreal scaleXInfl,
                                           const qreal scaleYInfl,
                                           const qreal rotInfl) const
{
    return std::isfinite(posXInfl) &&
           std::isfinite(posYInfl) &&
           std::isfinite(scaleXInfl) &&
           std::isfinite(scaleYInfl) &&
           std::isfinite(rotInfl);
}

void ParentEffect::applyInfluenceToTransform(TransformValues& values,
                                             const TransformValues& targetValues,
                                             const qreal posXInfl,
                                             const qreal posYInfl,
                                             const qreal scaleXInfl,
                                             const qreal scaleYInfl) const
{
    values.fMoveX = targetValues.fMoveX * posXInfl;
    values.fMoveY = targetValues.fMoveY * posYInfl;
    
    // Scale influence: interpolate between no scaling (1.0) and target scaling
    values.fScaleX = 1.0 + (targetValues.fScaleX - 1.0) * scaleXInfl;
    values.fScaleY = 1.0 + (targetValues.fScaleY - 1.0) * scaleYInfl;
}
