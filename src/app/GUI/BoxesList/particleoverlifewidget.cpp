#include "particleoverlifewidget.h"

#include <QContextMenuEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>

#include "Animators/qrealanimator.h"
#include "Animators/qrealkey.h"
#include "Private/document.h"
#include "themesupport.h"

namespace {
constexpr int kLeftPad = 6;
constexpr int kRightPad = 6;
constexpr int kTopPad = 5;
constexpr int kBottomPad = 5;
constexpr qreal kMinFrame = 0.;
constexpr qreal kMaxFrame = 100.;
constexpr qreal kMinValue = 0.;
constexpr qreal kMaxValue = 1.;
constexpr int kKeyRadius = 4;
}

ParticleOverLifeWidget::ParticleOverLifeWidget(QWidget *parent)
    : QWidget(parent) {
    setMouseTracking(true);
    setMinimumWidth(132);
    setMinimumHeight(34);
}

void ParticleOverLifeWidget::setTarget(QrealAnimator *animator) {
    if (animator == mTarget) return;
    auto &conn = mTarget.assign(animator);
    mDraggedKey = nullptr;
    mDragging = false;
    if (animator) {
        conn << connect(animator, &QrealAnimator::baseValueChanged,
                        this, [this](qreal) { update(); });
        conn << connect(animator, &QrealAnimator::effectiveValueChanged,
                        this, [this](qreal) { update(); });
        conn << connect(animator, &Animator::anim_addedKey,
                        this, [this](Key*) { update(); });
        conn << connect(animator, &Animator::anim_removedKey,
                        this, [this](Key*) { update(); });
        conn << connect(animator, &Property::prp_currentFrameChanged,
                        this, [this](const UpdateReason) { update(); });
    }
    update();
}

bool ParticleOverLifeWidget::hasTarget() const {
    return static_cast<bool>(mTarget);
}

QSize ParticleOverLifeWidget::sizeHint() const {
    return {148, 38};
}

QRectF ParticleOverLifeWidget::curveRect() const {
    return QRectF(kLeftPad,
                  kTopPad,
                  qMax(24, width() - kLeftPad - kRightPad),
                  qMax(20, height() - kTopPad - kBottomPad));
}

QPointF ParticleOverLifeWidget::posForKeyValues(const qreal frame,
                                                const qreal value) const {
    const QRectF rect = curveRect();
    const qreal x = rect.left() + rect.width() * ((frame - kMinFrame) / (kMaxFrame - kMinFrame));
    const qreal y = rect.bottom() - rect.height() * ((value - kMinValue) / (kMaxValue - kMinValue));
    return {x, y};
}

QPointF ParticleOverLifeWidget::keyToPoint(const QrealKey *key) const {
    return posForKeyValues(key->getRelFrame(), key->getValue());
}

QPointF ParticleOverLifeWidget::normalizedFromPos(const QPointF &pos) const {
    const QRectF rect = curveRect();
    const qreal nx = qBound<qreal>(0., (pos.x() - rect.left()) / rect.width(), 1.);
    const qreal ny = qBound<qreal>(0., 1. - ((pos.y() - rect.top()) / rect.height()), 1.);
    return {nx, ny};
}

QrealKey *ParticleOverLifeWidget::keyAtPos(const QPointF &pos) const {
    if (!mTarget) return nullptr;
    for (const auto &baseKey : mTarget->anim_getKeys()) {
        const auto key = static_cast<QrealKey*>(baseKey);
        if (QLineF(pos, keyToPoint(key)).length() <= kKeyRadius + 3) {
            return key;
        }
    }
    return nullptr;
}

bool ParticleOverLifeWidget::isEndpointKey(const QrealKey *key) const {
    if (!mTarget || !key) return false;
    return key == mTarget->anim_getKeyAtIndex<QrealKey>(0) ||
           key == mTarget->anim_getKeyAtIndex<QrealKey>(mTarget->anim_getKeys().count() - 1);
}

void ParticleOverLifeWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event)

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRectF rect = curveRect();
    p.setPen(QPen(ThemeSupport::getThemeButtonBorderColor(), 1));
    p.setBrush(ThemeSupport::getThemeButtonBaseColor(220));
    p.drawRoundedRect(rect, 4, 4);

    QColor grid = ThemeSupport::getThemeTimelineColor();
    grid.setAlpha(55);
    p.setPen(QPen(grid, 1));
    for (int i = 1; i < 4; ++i) {
        const qreal x = rect.left() + rect.width() * (static_cast<qreal>(i) / 4.);
        const qreal y = rect.top() + rect.height() * (static_cast<qreal>(i) / 4.);
        p.drawLine(QPointF(x, rect.top()), QPointF(x, rect.bottom()));
        p.drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y));
    }

    if (!mTarget) return;

    QPainterPath path;
    for (int i = 0; i <= 100; ++i) {
        const qreal frame = static_cast<qreal>(i);
        const qreal value = qBound<qreal>(0., mTarget->getBaseValue(frame), 1.);
        const QPointF pt = posForKeyValues(frame, value);
        if (i == 0) path.moveTo(pt);
        else path.lineTo(pt);
    }

    p.setPen(QPen(ThemeSupport::getThemeHighlightSelectedColor(), 1.8));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);

    for (const auto &baseKey : mTarget->anim_getKeys()) {
        const auto key = static_cast<QrealKey*>(baseKey);
        const QPointF pt = keyToPoint(key);
        const bool endpoint = isEndpointKey(key);
        p.setPen(QPen(endpoint ? ThemeSupport::getThemeHighlightSelectedColor()
                               : ThemeSupport::getThemeButtonBorderColor(), 1));
        p.setBrush(endpoint ? ThemeSupport::getThemeHighlightColor()
                            : ThemeSupport::getThemeButtonBaseColor());
        p.drawEllipse(pt, endpoint ? kKeyRadius + 0.5 : kKeyRadius,
                      endpoint ? kKeyRadius + 0.5 : kKeyRadius);
    }
}

void ParticleOverLifeWidget::mousePressEvent(QMouseEvent *event) {
    if (!mTarget || event->button() != Qt::LeftButton) return;
    auto *key = keyAtPos(event->pos());
    if (!key) return;
    mDraggedKey = key;
    mDragStartValue = {static_cast<qreal>(key->getRelFrame()), key->getValue()};
    key->startFrameAndValueTransform();
    mDragging = true;
}

void ParticleOverLifeWidget::dragSelectedKeyTo(const QPointF &pos) {
    if (!mTarget || !mDraggedKey) return;
    const QPointF norm = normalizedFromPos(pos);
    qreal frame = qRound(norm.x() * 100.);
    const qreal value = norm.y();

    if (isEndpointKey(mDraggedKey)) {
        frame = (mDraggedKey == mTarget->anim_getKeyAtIndex<QrealKey>(0)) ? 0. : 100.;
    } else {
        const auto prev = mTarget->anim_getPrevKey<QrealKey>(mDraggedKey);
        const auto next = mTarget->anim_getNextKey<QrealKey>(mDraggedKey);
        if (prev) frame = qMax<qreal>(frame, prev->getRelFrame() + 1);
        if (next) frame = qMin<qreal>(frame, next->getRelFrame() - 1);
    }

    const QPointF delta(frame - mDragStartValue.x(), value - mDragStartValue.y());
    mDraggedKey->changeFrameAndValueBy(delta);
    update();
}

void ParticleOverLifeWidget::mouseMoveEvent(QMouseEvent *event) {
    if (!mDragging || !(event->buttons() & Qt::LeftButton)) return;
    dragSelectedKeyTo(event->pos());
}

void ParticleOverLifeWidget::mouseReleaseEvent(QMouseEvent *event) {
    Q_UNUSED(event)
    if (!mDraggedKey) return;
    mDraggedKey->finishFrameAndValueTransform();
    mDraggedKey = nullptr;
    mDragging = false;
    Document::sInstance->actionFinished();
    update();
}

void ParticleOverLifeWidget::mouseDoubleClickEvent(QMouseEvent *event) {
    if (!mTarget || event->button() != Qt::LeftButton) return;
    if (keyAtPos(event->pos())) return;
    const QPointF norm = normalizedFromPos(event->pos());
    const int frame = qBound(1, qRound(norm.x() * 100.), 99);
    mTarget->anim_addKeyAtRelFrame(frame);
    if (auto *key = mTarget->anim_getKeyAtRelFrame<QrealKey>(frame)) {
        key->startValueTransform();
        key->setValue(norm.y());
        key->finishValueTransform();
    }
    mTarget->anim_setRecordingWithoutChangingKeys(false);
    Document::sInstance->actionFinished();
    update();
}

void ParticleOverLifeWidget::removeKey(QrealKey *key) {
    if (!mTarget || !key || isEndpointKey(key)) return;
    mTarget->anim_removeKeyAction(key->ref<Key>());
    mTarget->anim_setRecordingWithoutChangingKeys(false);
    Document::sInstance->actionFinished();
    update();
}

void ParticleOverLifeWidget::contextMenuEvent(QContextMenuEvent *event) {
    if (!mTarget) return;
    auto *key = keyAtPos(event->pos());
    if (!key || isEndpointKey(key)) return;
    QMenu menu(this);
    auto *removeAction = menu.addAction(tr("Remove Point"));
    if (menu.exec(event->globalPos()) == removeAction) {
        removeKey(key);
    }
}
