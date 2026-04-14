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

#ifndef BOXSCROLLWIDGETVISIBLEPART_H
#define BOXSCROLLWIDGETVISIBLEPART_H

#include <QWidget>
#include "optimalscrollarena/scrollwidgetvisiblepart.h"
#include "singlewidgettarget.h"
#include "framerange.h"

class BoxSingleWidget;
class TimelineMovable;
class Key;
class KeysView;
class Canvas;
class TimelineHighlightWidget;
class BoundingBox;
class PickWhipOverlay;
class eBoxOrSound;
class QRubberBand;

class BoxScroller : public ScrollWidgetVisiblePart {
public:
    enum class PickWhipMode {
        none,
        parent,
        matte
    };
    explicit BoxScroller(ScrollWidget * const parent);

    QWidget *createNewSingleWidget() override;

    void updateDropTarget();

    void stopScrolling();
    void scrollUp();
    void scrollDown();

    KeysView *getKeysView() const
    { return mKeysView; }

    Canvas* currentScene() const
    { return mCurrentScene; }

    void setCurrentScene(Canvas* const scene)
    { mCurrentScene = scene; }

    void setKeysView(KeysView *keysView)
    { mKeysView = keysView; }

    TimelineHighlightWidget* requestHighlighter();

    void beginPickWhip(BoundingBox *source, PickWhipMode mode,
                       const QPoint &startGlobalPos = QPoint());
    bool hasPendingPickWhip() const { return mPickWhipSource && mPickWhipMode != PickWhipMode::none; }
    bool handlePickWhipTarget(BoundingBox *target);
    void cancelPickWhip();
    void updatePickWhipPointer(const QPoint &globalPos,
                               BoundingBox *hoverTarget = nullptr,
                               const QRect &hoverGlobalRect = QRect());
    void clearPickWhipHover(BoundingBox *target = nullptr);
    void toggleSolo(eBoxOrSound *target);
    bool isSolo(const eBoxOrSound *target) const;
    void beginLayerRectSelection(const QPoint &globalPos,
                                 Qt::KeyboardModifiers modifiers);
    bool updateLayerRectSelection(const QPoint &globalPos);
    bool finishLayerRectSelection(const QPoint &globalPos);
    void cancelLayerRectSelection();
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *e) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
private:
    enum class DropType {
        none, on, into
    };

    struct DropTarget {
        SWT_Abstraction * fTargetParent = nullptr;
        int fTargetId = 0;
        DropType fDropType = DropType::none;

        bool isValid() const {
            return fTargetParent && fDropType != DropType::none;
        }

        void reset() {
            fTargetParent = nullptr;
            fDropType = DropType::none;
        }
    };

    DropTarget getClosestDropTarget(const int yPos);

    bool tryDropIntoAbs(SWT_Abstraction * const abs,
                        const int idInAbs, DropTarget &dropTarget);

    TimelineHighlightWidget* mHighlighter = nullptr;
    Canvas* mCurrentScene = nullptr;

    QRect mCurrentDragRect;
    int mLastDragMoveY;

    QTimer *mScrollTimer = nullptr;
    KeysView *mKeysView = nullptr;

    const QMimeData* mCurrentMimeData = nullptr;

    DropTarget mDropTarget{nullptr, 0, DropType::none};

    BoundingBox *mPickWhipSource = nullptr;
    PickWhipMode mPickWhipMode = PickWhipMode::none;
    QPoint mPickWhipStartGlobalPos;
    QPoint mPickWhipCursorGlobalPos;
    BoundingBox *mPickWhipHoverTarget = nullptr;
    QRect mPickWhipHoverGlobalRect;
    PickWhipOverlay *mPickWhipOverlay = nullptr;
    QRubberBand *mLayerRectRubberBand = nullptr;
    QPoint mLayerRectPressGlobalPos;
    QRect mLayerRectSelectionGlobalRect;
    Qt::KeyboardModifiers mLayerRectSelectionModifiers = Qt::NoModifier;
    bool mLayerRectSelectionActive = false;

    void ensurePickWhipOverlay();
    void updatePickWhipOverlay();

    friend class PickWhipOverlay;
};

#endif // BOXSCROLLWIDGETVISIBLEPART_H
