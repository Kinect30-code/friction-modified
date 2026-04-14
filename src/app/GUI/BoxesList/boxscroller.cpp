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

#include "boxscroller.h"
#include "boxsinglewidget.h"
#include <QPainter>
#include "Animators/qrealanimator.h"
#include "boxscrollwidget.h"
#include <QTimer>
#include <QMimeData>
#include "Boxes/boundingbox.h"
#include "Boxes/containerbox.h"
#include "GUI/mainwindow.h"
#include "GUI/global.h"
#include "swt_abstraction.h"
#include "GUI/keysview.h"
#include "RasterEffects/rastereffectcollection.h"
#include "BlendEffects/trackmatteeffect.h"
#include "GUI/timelinehighlightwidget.h"
#include "GUI/timelinewidget.h"
#include "renderhandler.h"

#include <QMouseEvent>
#include <QKeyEvent>
#include <QStatusBar>
#include <QApplication>
#include <QPainterPath>
#include <QRubberBand>
#include <limits>

class PickWhipOverlay : public QWidget {
public:
    explicit PickWhipOverlay(BoxScroller *parent)
        : QWidget(parent)
        , mScroller(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_TranslucentBackground);
        hide();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (!mScroller || !mScroller->hasPendingPickWhip()) { return; }

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QColor accent = mScroller->mPickWhipMode == BoxScroller::PickWhipMode::parent
                ? QColor(110, 198, 255)
                : QColor(255, 181, 84);

        if (mScroller->mPickWhipHoverTarget && mScroller->mPickWhipHoverGlobalRect.isValid()) {
            const QPoint tl = mapFromGlobal(mScroller->mPickWhipHoverGlobalRect.topLeft());
            const QPoint br = mapFromGlobal(mScroller->mPickWhipHoverGlobalRect.bottomRight());
            const QRect hoverRect = QRect(tl, br).normalized();
            p.fillRect(hoverRect, QColor(accent.red(), accent.green(), accent.blue(), 42));
            p.setPen(QPen(QColor(accent.red(), accent.green(), accent.blue(), 180), 1.5));
            p.drawRoundedRect(hoverRect.adjusted(1, 1, -1, -1), 4, 4);
        }

        if (mScroller->mPickWhipStartGlobalPos.isNull()) { return; }
        const QPointF start = mapFromGlobal(mScroller->mPickWhipStartGlobalPos);
        QPointF end = mapFromGlobal(mScroller->mPickWhipCursorGlobalPos);
        if (mScroller->mPickWhipHoverGlobalRect.isValid()) {
            const QPoint tl = mapFromGlobal(mScroller->mPickWhipHoverGlobalRect.topLeft());
            const QPoint br = mapFromGlobal(mScroller->mPickWhipHoverGlobalRect.bottomRight());
            const QRect hoverRect = QRect(tl, br).normalized();
            end = QPointF(hoverRect.left() + 12, hoverRect.center().y());
        }

        const qreal dx = qMax<qreal>(36.0, qAbs(end.x() - start.x()) * 0.45);
        QPainterPath path(start);
        path.cubicTo(start + QPointF(dx, 0.0),
                     end - QPointF(dx, 0.0),
                     end);

        p.setPen(QPen(QColor(0, 0, 0, 90), 4.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(path);
        p.setPen(QPen(accent, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(path);

        p.setBrush(accent);
        p.setPen(Qt::NoPen);
        p.drawEllipse(start, 4.0, 4.0);
        p.drawEllipse(end, 4.0, 4.0);
    }

private:
    BoxScroller *mScroller = nullptr;
};

namespace {
bool boxIsAncestorOf(BoundingBox *ancestor, BoundingBox *box)
{
    if (!ancestor || !box) { return false; }
    auto *current = box->getParentGroup();
    while (current) {
        if (current == ancestor) {
            return true;
        }
        current = current->getParentGroup();
    }
    return false;
}

bool resolvePickWhipLayerRow(BoxScroller *scroller,
                             const QPoint &globalPos,
                             BoundingBox *&target,
                             QRect &globalRect)
{
    if (!scroller) { return false; }
    const QRect scrollerRect(scroller->mapToGlobal(QPoint(0, 0)), scroller->size());
    if (!scrollerRect.adjusted(-64, -eSizesUI::widget, 64, eSizesUI::widget).contains(globalPos)) {
        return false;
    }

    const QPoint localPos = scroller->mapFromGlobal(globalPos);
    const auto &wids = scroller->widgets();
    for (const auto &wid : wids) {
        const auto row = dynamic_cast<BoxSingleWidget*>(wid);
        if (!row || row->isHidden()) { continue; }
        const auto bbox = enve_cast<BoundingBox*>(row->getTarget());
        if (!bbox) { continue; }

        QRect rowRect = row->geometry();
        rowRect.setLeft(0);
        rowRect.setRight(scroller->width());
        const QRect directHitRect = rowRect.adjusted(0, -qRound(eSizesUI::widget * 0.25),
                                                     0, qRound(eSizesUI::widget * 0.25));
        if (directHitRect.contains(localPos)) {
            target = bbox;
            globalRect = QRect(scroller->mapToGlobal(QPoint(0, rowRect.top())),
                               rowRect.size());
            return true;
        }
    }

    int bestDistance = std::numeric_limits<int>::max();
    for (const auto &wid : wids) {
        const auto row = dynamic_cast<BoxSingleWidget*>(wid);
        if (!row || row->isHidden()) { continue; }
        const auto bbox = enve_cast<BoundingBox*>(row->getTarget());
        if (!bbox) { continue; }

        QRect rowRect = row->geometry();
        rowRect.setLeft(0);
        rowRect.setRight(scroller->width());
        const QRect expandedRowRect = rowRect.adjusted(0, -qRound(eSizesUI::widget * 0.2),
                                                       0, qRound(eSizesUI::widget * 0.2));
        const int distance = expandedRowRect.contains(localPos) ? 0 :
                             qAbs(rowRect.center().y() - localPos.y());
        if (distance < bestDistance) {
            bestDistance = distance;
            target = bbox;
            globalRect = QRect(scroller->mapToGlobal(QPoint(0, rowRect.top())),
                               rowRect.size());
        }
    }
    return target != nullptr;
}
}

BoxScroller::BoxScroller(ScrollWidget * const parent) :
    ScrollWidgetVisiblePart(parent) {
    setAcceptDrops(true);
    setMouseTracking(true);
    mScrollTimer = new QTimer(this);
}

void BoxScroller::ensurePickWhipOverlay()
{
    if (mPickWhipOverlay) { return; }
    mPickWhipOverlay = new PickWhipOverlay(this);
    mPickWhipOverlay->setGeometry(rect());
    mPickWhipOverlay->raise();
}

void BoxScroller::updatePickWhipOverlay()
{
    if (!mPickWhipOverlay) { return; }
    mPickWhipOverlay->setGeometry(rect());
    mPickWhipOverlay->setVisible(hasPendingPickWhip());
    mPickWhipOverlay->raise();
    mPickWhipOverlay->update();
}

void BoxScroller::beginPickWhip(BoundingBox *source, PickWhipMode mode,
                                const QPoint &startGlobalPos) {
    mPickWhipSource = source;
    mPickWhipMode = mode;
    mPickWhipStartGlobalPos = startGlobalPos;
    mPickWhipCursorGlobalPos = startGlobalPos.isNull() ? QCursor::pos() : startGlobalPos;
    mPickWhipHoverTarget = nullptr;
    mPickWhipHoverGlobalRect = QRect();
    ensurePickWhipOverlay();
    updatePickWhipOverlay();
    qApp->installEventFilter(this);
    if (auto *win = MainWindow::sGetInstance()) {
        const auto message = mode == PickWhipMode::parent
                ? tr("Pick-whip: click a layer row to use as parent.")
                : tr("Pick-whip: click a layer row to use as this layer's track matte source.");
        if (win->statusBar()) {
            win->statusBar()->showMessage(message, 4000);
        }
    }
}

bool BoxScroller::handlePickWhipTarget(BoundingBox *target) {
    if (!mPickWhipSource || mPickWhipMode == PickWhipMode::none) {
        return false;
    }

    const auto source = mPickWhipSource;
    const auto mode = mPickWhipMode;
    cancelPickWhip();

    if (!source || !target || source == target) {
        return true;
    }

    if (mode == PickWhipMode::parent) {
        if (boxIsAncestorOf(source, target)) {
            if (auto *win = MainWindow::sGetInstance()) {
                if (win->statusBar()) {
                    win->statusBar()->showMessage(
                        tr("AE: Cannot parent a layer to its own child/descendant."),
                        3500);
                }
            }
            return true;
        }
        source->setParentEffectTarget(target);
        if (auto *win = MainWindow::sGetInstance()) {
            if (win->statusBar()) {
                win->statusBar()->showMessage(
                    tr("Parented \"%1\" to \"%2\".").arg(source->prp_getName(),
                                                          target->prp_getName()),
                    2500);
            }
        }
        Document::sInstance->actionFinished();
        return true;
    }

    if (mode == PickWhipMode::matte) {
        const auto parent = source->getParentGroup();
        if (parent && parent == target->getParentGroup()) {
            const auto trackMatteMode = source->getTrackMatteTarget()
                    ? source->getTrackMatteMode()
                    : TrackMatteMode::alphaMatte;
            source->setTrackMatteTarget(target, trackMatteMode);
            if (auto *win = MainWindow::sGetInstance()) {
                if (win->statusBar()) {
                    win->statusBar()->showMessage(
                        tr("Track matte for \"%1\" now uses \"%2\".").arg(source->prp_getName(),
                                                                          target->prp_getName()),
                        2500);
                }
            }
            Document::sInstance->actionFinished();
        }
        return true;
    }

    return false;
}

void BoxScroller::cancelPickWhip() {
    qApp->removeEventFilter(this);
    mPickWhipSource = nullptr;
    mPickWhipMode = PickWhipMode::none;
    mPickWhipStartGlobalPos = QPoint();
    mPickWhipCursorGlobalPos = QPoint();
    mPickWhipHoverTarget = nullptr;
    mPickWhipHoverGlobalRect = QRect();
    updatePickWhipOverlay();
}

bool BoxScroller::eventFilter(QObject *watched, QEvent *event)
{
    Q_UNUSED(watched)
    if (!hasPendingPickWhip() || !event) {
        return ScrollWidgetVisiblePart::eventFilter(watched, event);
    }

    switch (event->type()) {
    case QEvent::MouseMove: {
        const auto *mouseEvent = static_cast<QMouseEvent*>(event);
        updatePickWhipPointer(mouseEvent->globalPos());
        return true;
    }
    case QEvent::MouseButtonRelease: {
        const auto *mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() != Qt::LeftButton) {
            cancelPickWhip();
            return true;
        }

        BoundingBox *resolvedTarget = nullptr;
        QRect resolvedRect;
        if (mPickWhipHoverTarget &&
            mPickWhipHoverGlobalRect.adjusted(-8, -8, 8, 8).contains(mouseEvent->globalPos())) {
            resolvedTarget = mPickWhipHoverTarget;
            resolvedRect = mPickWhipHoverGlobalRect;
        } else {
            resolvePickWhipLayerRow(this, mouseEvent->globalPos(), resolvedTarget, resolvedRect);
        }
        updatePickWhipPointer(mouseEvent->globalPos(), resolvedTarget, resolvedRect);
        if (mPickWhipHoverTarget) {
            handlePickWhipTarget(mPickWhipHoverTarget);
            return true;
        }
        QWidget *widget = QApplication::widgetAt(mouseEvent->globalPos());
        while (widget) {
            if (const auto row = dynamic_cast<BoxSingleWidget*>(widget)) {
                const auto target = row->getTarget();
                if (const auto bbox = enve_cast<BoundingBox*>(target)) {
                    handlePickWhipTarget(bbox);
                    return true;
                }
                break;
            }
            widget = widget->parentWidget();
        }
        cancelPickWhip();
        return true;
    }
    case QEvent::KeyPress: {
        const auto *keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            cancelPickWhip();
            return true;
        }
        break;
    }
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick:
        return true;
    default:
        break;
    }

    return ScrollWidgetVisiblePart::eventFilter(watched, event);
}

void BoxScroller::updatePickWhipPointer(const QPoint &globalPos,
                                        BoundingBox *hoverTarget,
                                        const QRect &hoverGlobalRect)
{
    if (!hasPendingPickWhip()) { return; }
    mPickWhipCursorGlobalPos = globalPos;
    QRect resolvedRect = hoverGlobalRect;
    if (!hoverTarget) {
        if (mPickWhipHoverTarget &&
            mPickWhipHoverGlobalRect.adjusted(-6, -6, 6, 6).contains(globalPos)) {
            hoverTarget = mPickWhipHoverTarget;
            resolvedRect = mPickWhipHoverGlobalRect;
        } else {
            resolvePickWhipLayerRow(this, globalPos, hoverTarget, resolvedRect);
        }
    }
    if (hoverTarget == mPickWhipSource) {
        hoverTarget = nullptr;
    }
    mPickWhipHoverTarget = hoverTarget;
    mPickWhipHoverGlobalRect = hoverTarget ? resolvedRect : QRect();
    updatePickWhipOverlay();
}

void BoxScroller::clearPickWhipHover(BoundingBox *target)
{
    if (!hasPendingPickWhip()) { return; }
    if (target && target != mPickWhipHoverTarget) { return; }
    updatePickWhipPointer(QCursor::pos());
}

void BoxScroller::beginLayerRectSelection(const QPoint &globalPos,
                                          Qt::KeyboardModifiers modifiers)
{
    mLayerRectPressGlobalPos = globalPos;
    mLayerRectSelectionGlobalRect = QRect(globalPos, globalPos);
    mLayerRectSelectionModifiers = modifiers;
    mLayerRectSelectionActive = false;
    if (mLayerRectRubberBand) {
        mLayerRectRubberBand->hide();
    }
}

bool BoxScroller::updateLayerRectSelection(const QPoint &globalPos)
{
    if (mLayerRectPressGlobalPos.isNull()) {
        return false;
    }

    const QPoint delta = globalPos - mLayerRectPressGlobalPos;
    if (!mLayerRectSelectionActive) {
        if (delta.manhattanLength() < QApplication::startDragDistance()) {
            return false;
        }
        if (qAbs(delta.y()) < qAbs(delta.x())) {
            return false;
        }
        mLayerRectSelectionActive = true;
        if (!mLayerRectRubberBand) {
            mLayerRectRubberBand = new QRubberBand(QRubberBand::Rectangle, this);
        }
    }

    mLayerRectSelectionGlobalRect = QRect(mLayerRectPressGlobalPos, globalPos).normalized();
    const QRect localRect(mapFromGlobal(mLayerRectSelectionGlobalRect.topLeft()),
                          mapFromGlobal(mLayerRectSelectionGlobalRect.bottomRight()));
    mLayerRectRubberBand->setGeometry(localRect.normalized());
    mLayerRectRubberBand->show();
    mLayerRectRubberBand->raise();
    return true;
}

bool BoxScroller::finishLayerRectSelection(const QPoint &globalPos)
{
    if (mLayerRectPressGlobalPos.isNull()) {
        return false;
    }

    const bool wasActive = mLayerRectSelectionActive;
    const auto modifiers = mLayerRectSelectionModifiers;
    if (wasActive) {
        updateLayerRectSelection(globalPos);
    }

    QList<BoundingBox*> boxes;
    if (wasActive) {
        const auto &wids = widgets();
        for (const auto &wid : wids) {
            const auto row = dynamic_cast<BoxSingleWidget*>(wid);
            if (!row || row->isHidden()) {
                continue;
            }
            auto *box = enve_cast<BoundingBox*>(row->targetProperty());
            if (!box) {
                continue;
            }
            const QRect rowGlobalRect(row->mapToGlobal(QPoint(0, 0)), row->size());
            if (mLayerRectSelectionGlobalRect.intersects(rowGlobalRect)) {
                boxes.append(box);
            }
        }
    }

    cancelLayerRectSelection();

    if (!wasActive || boxes.isEmpty()) {
        return false;
    }

    for (QWidget *p = parentWidget(); p; p = p->parentWidget()) {
        if (auto *timeline = qobject_cast<TimelineWidget*>(p)) {
            const bool handled = timeline->handleTimelineLayerRectSelection(
                        boxes, modifiers);
            if (handled) {
                Document::sInstance->actionFinished();
            }
            return handled;
        }
    }
    return false;
}

void BoxScroller::cancelLayerRectSelection()
{
    mLayerRectPressGlobalPos = QPoint();
    mLayerRectSelectionGlobalRect = QRect();
    mLayerRectSelectionModifiers = Qt::NoModifier;
    mLayerRectSelectionActive = false;
    if (mLayerRectRubberBand) {
        mLayerRectRubberBand->hide();
    }
}

QWidget *BoxScroller::createNewSingleWidget() {
    return new BoxSingleWidget(this);
}

void BoxScroller::toggleSolo(eBoxOrSound *target)
{
    if (auto *scrollWidget = qobject_cast<BoxScrollWidget*>(parentWidget())) {
        scrollWidget->toggleSolo(target);
    }
}

bool BoxScroller::isSolo(const eBoxOrSound *target) const
{
    if (const auto *scrollWidget = qobject_cast<BoxScrollWidget*>(parentWidget())) {
        return scrollWidget->isSolo(target);
    }
    return false;
}

void BoxScroller::paintEvent(QPaintEvent *) {
    QPainter p(this);

    int currY = eSizesUI::widget;
    p.setPen(QPen(QColor(40, 40, 40), 1));
    const auto parent = static_cast<BoxScrollWidget*>(parentWidget());
    const int parentContHeight = parent->getContentHeight() - eSizesUI::widget;
    while(currY < parentContHeight) {
        p.drawLine(0, currY, width(), currY);
        currY += eSizesUI::widget;
    }

    if(mDropTarget.isValid()) {
        p.setPen(QPen(Qt::white, 2));
        p.drawRect(mCurrentDragRect);
    }

    p.end();
}

TimelineHighlightWidget *BoxScroller::requestHighlighter() {
    if(!mHighlighter) {
        mHighlighter = new TimelineHighlightWidget(true, this, true);
        mHighlighter->resize(size());
    }
    return mHighlighter;
}

void BoxScroller::resizeEvent(QResizeEvent *e) {
    if(mHighlighter) mHighlighter->resize(e->size());
    if(mPickWhipOverlay) mPickWhipOverlay->setGeometry(rect());
    ScrollWidgetVisiblePart::resizeEvent(e);
}

void BoxScroller::mousePressEvent(QMouseEvent *event)
{
    for (QWidget *p = parentWidget(); p; p = p->parentWidget()) {
        if (auto *timeline = qobject_cast<TimelineWidget*>(p)) {
            timeline->setFocus(Qt::MouseFocusReason);
            break;
        }
    }
    const auto previewState = RenderHandler::sInstance->currentPreviewState();
    if (previewState == PreviewState::playing ||
        previewState == PreviewState::rendering) {
        RenderHandler::sInstance->interruptPreview();
    }
    if (event->button() == Qt::RightButton) {
        bool clickedWidget = false;
        const auto &wids = widgets();
        for (const auto &wid : wids) {
            if (wid && !wid->isHidden() && wid->geometry().contains(event->pos())) {
                clickedWidget = true;
                break;
            }
        }
        if (!clickedWidget) {
            for (QWidget *p = parentWidget(); p; p = p->parentWidget()) {
                if (auto *timeline = qobject_cast<TimelineWidget*>(p)) {
                    timeline->showAeTimelineContextMenu(event->globalPos());
                    return;
                }
            }
        }
    }
    if (event->button() == Qt::LeftButton &&
        !(event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier))) {
        bool clickedWidget = false;
        const auto &wids = widgets();
        for (const auto &wid : wids) {
            if (wid && !wid->isHidden() && wid->geometry().contains(event->pos())) {
                clickedWidget = true;
                break;
            }
        }
        if (!clickedWidget) {
            if (hasPendingPickWhip()) {
                cancelPickWhip();
                if (auto *win = MainWindow::sGetInstance()) {
                    if (win->statusBar()) {
                        win->statusBar()->showMessage(tr("Pick-whip cancelled."), 2000);
                    }
                }
            }
            if (mCurrentScene) {
                mCurrentScene->clearBoxesSelection();
                Document::sInstance->actionFinished();
                update();
            }
        }
    }
    ScrollWidgetVisiblePart::mousePressEvent(event);
}

void BoxScroller::mouseMoveEvent(QMouseEvent *event)
{
    if (hasPendingPickWhip()) {
        updatePickWhipPointer(mapToGlobal(event->pos()));
    }
    ScrollWidgetVisiblePart::mouseMoveEvent(event);
}

void BoxScroller::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        finishLayerRectSelection(mapToGlobal(event->pos()));
    }
    ScrollWidgetVisiblePart::mouseReleaseEvent(event);
}

void BoxScroller::leaveEvent(QEvent *event)
{
    clearPickWhipHover();
    ScrollWidgetVisiblePart::leaveEvent(event);
}

bool BoxScroller::tryDropIntoAbs(SWT_Abstraction* const abs,
                                 const int idInAbs,
                                 DropTarget& dropTarget) {
    if(!abs) return false;
    const auto target = abs->getTarget();
    const int id = qBound(0, idInAbs, abs->childrenCount());
    if(!target->SWT_dropIntoSupport(id, mCurrentMimeData)) return false;
    dropTarget = DropTarget{abs, id, DropType::into};
    return true;
}

BoxScroller::DropTarget BoxScroller::getClosestDropTarget(const int yPos) {
    const auto mainAbs = getMainAbstration();
    if(!mainAbs) return DropTarget();
    const int idAtPos = yPos / eSizesUI::widget;
    DropTarget target;
    const auto& wids = widgets();
    const int nWidgets = wids.count();
    if(idAtPos >= 0 && idAtPos < nWidgets) {
        const auto bsw = static_cast<BoxSingleWidget*>(wids.at(idAtPos));
        if(bsw->isHidden()) {
            const int nChildren = mainAbs->childrenCount();
            if(tryDropIntoAbs(mainAbs, nChildren, target)) {
                mCurrentDragRect = QRect(0, visibleCount()*eSizesUI::widget, width(), 1);
                return target;
            }
        } else if(bsw->getTargetAbstraction()) {
            const auto abs = bsw->getTargetAbstraction();
            const bool above = yPos % eSizesUI::widget < eSizesUI::widget*0.5;
            bool dropOn = false;
            {
                const qreal posFrac = qreal(yPos)/eSizesUI::widget;
                if(qAbs(qRound(posFrac) - posFrac) > 0.333) dropOn = true;
                if(!above && abs->contentVisible() &&
                   abs->childrenCount() > 0) dropOn = true;
            }
            for(const bool iDropOn : {dropOn, !dropOn}) {
                if(iDropOn) {
                    if(abs->contentVisible() &&
                       abs->getTarget()->SWT_dropSupport(mCurrentMimeData)) {
                        mCurrentDragRect = bsw->rect().translated(bsw->pos());
                        return {abs, 0, DropType::on};
                    }
                } else {
                    const auto parentAbs = abs->getParent();
                    if(parentAbs) {
                        const int id = abs->getIdInParent() + (above ? 0 : 1);
                        if(tryDropIntoAbs(parentAbs, id, target)) {
                            const int y = bsw->y() + (above ? 0 : abs->getHeight());
                            mCurrentDragRect = QRect(bsw->x(), y,  width(), 1);
                            return target;
                        }
                    }
                }
            }
        }
    }
    for(int i = idAtPos - 1; i >= 0; i--) {
        const auto bsw = static_cast<BoxSingleWidget*>(wids.at(i));
        if(!bsw->isHidden() && bsw->getTargetAbstraction()) {
            const auto abs = bsw->getTargetAbstraction();
            if(abs->contentVisible() &&
               abs->getTarget()->SWT_dropSupport(mCurrentMimeData)) {
                mCurrentDragRect = bsw->rect().translated(bsw->pos());
                return {abs, 0, DropType::on};
            }
            const auto parentAbs = abs->getParent();
            if(parentAbs) {
                const int id = abs->getIdInParent() + 1;
                if(tryDropIntoAbs(parentAbs, id, target)) {
                    const int y = bsw->y() + abs->getHeight();
                    mCurrentDragRect = QRect(bsw->x(), y, width(), 1);
                    return target;
                }
            }
        }
    }

    for(int i = idAtPos + 1; i < wids.count(); i++) {
        const auto bsw = static_cast<BoxSingleWidget*>(wids.at(i));
        if(!bsw->isHidden() && bsw->getTargetAbstraction()) {
            const auto abs = bsw->getTargetAbstraction();
            if(abs->contentVisible() && tryDropIntoAbs(abs, 0, target)) {
                mCurrentDragRect = QRect(bsw->x() + eSizesUI::widget,
                                         bsw->y() + eSizesUI::widget,
                                         width(), 1);
                return target;
            }
        }
    }
    return DropTarget();
}

void BoxScroller::stopScrolling() {
    if(mScrollTimer->isActive()) {
        mScrollTimer->disconnect();
        mScrollTimer->stop();
    }
}

void BoxScroller::dropEvent(QDropEvent *event) {
    stopScrolling();
    mCurrentMimeData = event->mimeData();
    mLastDragMoveY = event->pos().y();
    updateDropTarget();
    if(mDropTarget.isValid()) {
        const auto targetAbs = mDropTarget.fTargetParent;
        const auto target = targetAbs->getTarget();
        if(mDropTarget.fDropType == DropType::on) {
            target->SWT_drop(mCurrentMimeData);
        } else if(mDropTarget.fDropType == DropType::into) {
            target->SWT_dropInto(mDropTarget.fTargetId, mCurrentMimeData);
        }
        planScheduleUpdateVisibleWidgetsContent();
        Document::sInstance->actionFinished();
    }
    mCurrentMimeData = nullptr;
    mDropTarget.reset();
}

void BoxScroller::dragEnterEvent(QDragEnterEvent *event) {
    const auto mimeData = event->mimeData();
    mLastDragMoveY = event->pos().y();
    mCurrentMimeData = mimeData;
    updateDropTarget();
    //mDragging = true;
    if(mCurrentMimeData) event->acceptProposedAction();
    update();
}

void BoxScroller::dragLeaveEvent(QDragLeaveEvent *event) {
    mCurrentMimeData = nullptr;
    mDropTarget.reset();
    stopScrolling();
    event->accept();
    update();
}

void BoxScroller::dragMoveEvent(QDragMoveEvent *event) {
    event->acceptProposedAction();
    const int yPos = event->pos().y();

    if(yPos < 30) {
        if(!mScrollTimer->isActive()) {
            connect(mScrollTimer, &QTimer::timeout,
                    this, &BoxScroller::scrollUp);
            mScrollTimer->start(300);
        }
    } else if(yPos > height() - 30) {
        if(!mScrollTimer->isActive()) {
            connect(mScrollTimer, &QTimer::timeout,
                    this, &BoxScroller::scrollDown);
            mScrollTimer->start(300);
        }
    } else {
        mScrollTimer->disconnect();
        mScrollTimer->stop();
    }
    mLastDragMoveY = yPos;

    updateDropTarget();
    update();
}

void BoxScroller::updateDropTarget() {
    mDropTarget = getClosestDropTarget(mLastDragMoveY);
}

void BoxScroller::scrollUp() {
    parentWidget()->scrollParentAreaBy(-eSizesUI::widget);
    updateDropTarget();
    update();
}

void BoxScroller::scrollDown() {
    parentWidget()->scrollParentAreaBy(eSizesUI::widget);
    updateDropTarget();
    update();
}
