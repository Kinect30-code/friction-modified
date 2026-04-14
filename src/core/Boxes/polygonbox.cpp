#include "Boxes/polygonbox.h"

#include "Animators/gradientpoints.h"
#include "Animators/intanimator.h"
#include "Animators/transformanimator.h"
#include "MovablePoints/pointshandler.h"
#include "PathEffects/patheffectcollection.h"
#include "canvas.h"
#include "simplemath.h"
#include "svgexporter.h"
#include "include/utils/SkParsePath.h"

#include <QtMath>

PolygonBox::PolygonBox() : PathBox("Polygon", eBoxType::polygon) {
    setPointsHandler(enve::make_shared<PointsHandler>());

    mCenterAnimator = enve::make_shared<QPointFAnimator>("center");
    mCenterPoint = enve::make_shared<AnimatedPoint>(mCenterAnimator.get(),
                                                    mTransformAnimator.get(),
                                                    TYPE_PATH_POINT);
    getPointsHandler()->appendPt(mCenterPoint);
    mCenterPoint->disableSelection();
    mCenterPoint->setRelativePos(QPointF(0, 0));
    ca_prependChild(mPathEffectsAnimators.data(), mCenterAnimator);

    mHorizontalRadiusAnimator =
            enve::make_shared<QPointFAnimator>("horizontal radius");
    mHorizontalRadiusPoint = enve::make_shared<PolygonRadiusPoint>(
                mHorizontalRadiusAnimator.get(), mTransformAnimator.get(),
                mCenterPoint.get(), TYPE_PATH_POINT, false);
    getPointsHandler()->appendPt(mHorizontalRadiusPoint);
    mHorizontalRadiusPoint->setRelativePos(QPointF(10, 0));
    const auto hXAnimator = mHorizontalRadiusAnimator->getXAnimator();
    ca_prependChild(mPathEffectsAnimators.data(),
                    hXAnimator->ref<QrealAnimator>());
    hXAnimator->prp_setName("horizontal radius");

    mVerticalRadiusAnimator =
            enve::make_shared<QPointFAnimator>("vertical radius");
    mVerticalRadiusPoint = enve::make_shared<PolygonRadiusPoint>(
                mVerticalRadiusAnimator.get(), mTransformAnimator.get(),
                mCenterPoint.get(), TYPE_PATH_POINT, true);
    getPointsHandler()->appendPt(mVerticalRadiusPoint);
    mVerticalRadiusPoint->setRelativePos(QPointF(0, 10));
    const auto vYAnimator = mVerticalRadiusAnimator->getYAnimator();
    ca_prependChild(mPathEffectsAnimators.data(),
                    vYAnimator->ref<QrealAnimator>());
    vYAnimator->prp_setName("vertical radius");

    mSidesAnimator = enve::make_shared<IntAnimator>(5, 3, 32, 1, "sides");
    ca_prependChild(mPathEffectsAnimators.data(), mSidesAnimator);

    const auto pathUpdater = [this](const UpdateReason reason) {
        setPathsOutdated(reason);
    };
    connect(mCenterAnimator.get(), &Property::prp_currentFrameChanged,
            this, pathUpdater);
    connect(vYAnimator, &Property::prp_currentFrameChanged,
            this, pathUpdater);
    connect(hXAnimator, &Property::prp_currentFrameChanged,
            this, pathUpdater);
    connect(mSidesAnimator.get(), &Property::prp_currentFrameChanged,
            this, pathUpdater);
}

void PolygonBox::moveRadiusesByAbs(const QPointF &absTrans) {
    mVerticalRadiusPoint->moveByAbs(absTrans);
    mHorizontalRadiusPoint->moveByAbs(absTrans);
}

void PolygonBox::setVerticalRadius(const qreal verticalRadius) {
    const QPointF centerPos = mCenterPoint->getRelativePos();
    mVerticalRadiusPoint->setRelativePos(
                centerPos + QPointF(0, verticalRadius));
}

void PolygonBox::setHorizontalRadius(const qreal horizontalRadius) {
    const QPointF centerPos = mCenterPoint->getRelativePos();
    mHorizontalRadiusPoint->setRelativePos(
                centerPos + QPointF(horizontalRadius, 0));
}

void PolygonBox::setRadius(const qreal radius) {
    setHorizontalRadius(radius);
    setVerticalRadius(radius);
}

void PolygonBox::setSideCount(const int sides) {
    mSidesAnimator->setCurrentIntValue(qBound(3, sides, 32));
}

int PolygonBox::getCurrentSideCount() const {
    return qBound(3, mSidesAnimator->getEffectiveIntValue(), 32);
}

int PolygonBox::adjustSideCountBy(const int delta) {
    const int sides = qBound(3, getCurrentSideCount() + delta, 32);
    setSideCount(sides);
    return sides;
}

SkPath PolygonBox::getRelativePath(const qreal relFrame) const {
    const QPointF center = mCenterAnimator->getEffectiveValue(relFrame);
    const qreal xRad = mHorizontalRadiusAnimator->getEffectiveXValue(relFrame);
    const qreal yRad = mVerticalRadiusAnimator->getEffectiveYValue(relFrame);
    const int sides = qBound(3, mSidesAnimator->getEffectiveIntValue(relFrame), 32);

    SkPath path;
    if (isZero4Dec(xRad) || isZero4Dec(yRad)) {
        return path;
    }

    static constexpr qreal kHalfPi = M_PI / 2.0;
    static constexpr qreal kTwoPi = M_PI * 2.0;
    for (int i = 0; i < sides; ++i) {
        const qreal angle = -kHalfPi + (kTwoPi * i) / sides;
        const QPointF pt(center.x() + qCos(angle) * xRad,
                         center.y() + qSin(angle) * yRad);
        if (i == 0) {
            path.moveTo(toSkPoint(pt));
        } else {
            path.lineTo(toSkPoint(pt));
        }
    }
    path.close();
    return path;
}

QPointFAnimator *PolygonBox::getCenterAnimator()
{
    return mCenterAnimator.get();
}

QPointFAnimator *PolygonBox::getHRadiusAnimator()
{
    return mHorizontalRadiusAnimator.get();
}

QPointFAnimator *PolygonBox::getVRadiusAnimator()
{
    return mVerticalRadiusAnimator.get();
}

IntAnimator *PolygonBox::getSidesAnimator()
{
    return mSidesAnimator.get();
}

void PolygonBox::getMotionBlurProperties(QList<Property*> &list) const {
    PathBox::getMotionBlurProperties(list);
    list.append(mHorizontalRadiusAnimator.get());
    list.append(mVerticalRadiusAnimator.get());
    list.append(mSidesAnimator.get());
}

bool PolygonBox::differenceInEditPathBetweenFrames(
        const int frame1, const int frame2) const {
    if (mCenterAnimator->prp_differencesBetweenRelFrames(frame1, frame2)) return true;
    if (mHorizontalRadiusAnimator->prp_differencesBetweenRelFrames(frame1, frame2)) return true;
    if (mVerticalRadiusAnimator->prp_differencesBetweenRelFrames(frame1, frame2)) return true;
    return mSidesAnimator->prp_differencesBetweenRelFrames(frame1, frame2);
}

void PolygonBox::setCenter(const QPointF &center) {
    mCenterAnimator->setBaseValue(center);
}

void PolygonBox::saveSVG(SvgExporter& exp,
                         DomEleTask* const task) const
{
    auto& ele = task->initialize("path");
    SkString pathStr;
    SkParsePath::ToSVGString(getRelativePath(anim_getCurrentRelFrame()), &pathStr);
    ele.setAttribute("d", QString::fromUtf8(pathStr.c_str()));
    savePathBoxSVG(exp, ele, task->visRange());
}

PolygonRadiusPoint::PolygonRadiusPoint(QPointFAnimator * const associatedAnimator,
                                       BasicTransformAnimator * const parent,
                                       AnimatedPoint * const centerPoint,
                                       const MovablePointType &type,
                                       const bool blockX) :
    AnimatedPoint(associatedAnimator, type),
    mXBlocked(blockX), mCenterPoint(centerPoint) {
    setTransform(parent);
    disableSelection();
}

QPointF PolygonRadiusPoint::getRelativePos() const {
    const QPointF centerPos = mCenterPoint->getRelativePos();
    return AnimatedPoint::getRelativePos() + centerPos;
}

void PolygonRadiusPoint::setRelativePos(const QPointF &relPos) {
    const QPointF centerPos = mCenterPoint->getRelativePos();
    if (mXBlocked) {
        setValue(QPointF(0, relPos.y() - centerPos.y()));
    } else {
        setValue(QPointF(relPos.x() - centerPos.x(), 0));
    }
}
