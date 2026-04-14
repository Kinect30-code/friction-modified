#ifndef PARTICLEOVERLIFEWIDGET_H
#define PARTICLEOVERLIFEWIDGET_H

#include <QWidget>
#include "Animators/qrealanimator.h"
#include "smartPointers/selfref.h"
#include "conncontextptr.h"

class QrealKey;

class ParticleOverLifeWidget : public QWidget {
    Q_OBJECT
public:
    explicit ParticleOverLifeWidget(QWidget *parent = nullptr);

    void setTarget(QrealAnimator *animator);
    bool hasTarget() const;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    QSize sizeHint() const override;

private:
    QRectF curveRect() const;
    QPointF keyToPoint(const QrealKey *key) const;
    QPointF normalizedFromPos(const QPointF &pos) const;
    QPointF posForKeyValues(qreal frame, qreal value) const;
    QrealKey *keyAtPos(const QPointF &pos) const;
    bool isEndpointKey(const QrealKey *key) const;
    void dragSelectedKeyTo(const QPointF &pos);
    void removeKey(QrealKey *key);

    ConnContextQPtr<QrealAnimator> mTarget;
    QrealKey *mDraggedKey = nullptr;
    QPointF mDragStartValue;
    bool mDragging = false;
};

#endif // PARTICLEOVERLIFEWIDGET_H
