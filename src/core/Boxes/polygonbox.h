#ifndef POLYGONBOX_H
#define POLYGONBOX_H

#include "Boxes/pathbox.h"
#include "MovablePoints/animatedpoint.h"

class QPointFAnimator;
class IntAnimator;
class BasicTransformAnimator;

class CORE_EXPORT PolygonRadiusPoint : public AnimatedPoint {
    e_OBJECT
protected:
    PolygonRadiusPoint(QPointFAnimator * const associatedAnimator,
                       BasicTransformAnimator * const parent,
                       AnimatedPoint * const centerPoint,
                       const MovablePointType &type,
                       const bool blockX);
public:
    QPointF getRelativePos() const;
    void setRelativePos(const QPointF &relPos);
private:
    const bool mXBlocked = false;
    const stdptr<AnimatedPoint> mCenterPoint;
};

class CORE_EXPORT PolygonBox : public PathBox {
    e_OBJECT
protected:
    PolygonBox();
public:
    SkPath getRelativePath(const qreal relFrame) const;

    bool differenceInEditPathBetweenFrames(
                const int frame1, const int frame2) const;
    void saveSVG(SvgExporter& exp, DomEleTask* const task) const;

    void setCenter(const QPointF& center);
    void setVerticalRadius(const qreal verticalRadius);
    void setHorizontalRadius(const qreal horizontalRadius);
    void setRadius(const qreal radius);
    void moveRadiusesByAbs(const QPointF &absTrans);
    void setSideCount(const int sides);
    int getCurrentSideCount() const;
    int adjustSideCountBy(const int delta);

    QPointFAnimator* getCenterAnimator();
    QPointFAnimator* getHRadiusAnimator();
    QPointFAnimator* getVRadiusAnimator();
    IntAnimator* getSidesAnimator();

protected:
    void getMotionBlurProperties(QList<Property*> &list) const;

private:
    stdsptr<AnimatedPoint> mCenterPoint;
    stdsptr<PolygonRadiusPoint> mHorizontalRadiusPoint;
    stdsptr<PolygonRadiusPoint> mVerticalRadiusPoint;

    qsptr<QPointFAnimator> mCenterAnimator;
    qsptr<QPointFAnimator> mHorizontalRadiusAnimator;
    qsptr<QPointFAnimator> mVerticalRadiusAnimator;
    qsptr<IntAnimator> mSidesAnimator;
};

#endif // POLYGONBOX_H
