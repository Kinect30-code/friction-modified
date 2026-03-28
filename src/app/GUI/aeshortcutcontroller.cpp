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

#include "aeshortcutcontroller.h"

#include "aeshortcutdefaults.h"
#include "mainwindow.h"
#include "canvaswindow.h"
#include "GUI/timelinedockwidget.h"
#include "misc/keyfocustarget.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QStatusBar>
#include <QTextEdit>
#include <QKeyEvent>
#include <QCursor>

AeShortcutController::AeShortcutController(MainWindow &mainWindow,
                                           Document &document,
                                           Actions &actions)
    : QObject(&mainWindow)
    , mMainWindow(mainWindow)
    , mDocument(document)
    , mActions(actions)
{
    mPendingUTimer.setSingleShot(true);
    mPendingUTimer.setInterval(350);
}

bool AeShortcutController::process(QKeyEvent *event)
{
    if (!event || event->type() != QEvent::KeyPress || event->isAutoRepeat()) {
        return false;
    }

    if (shouldIgnoreBecauseOfFocusedEditor(event)) {
        return false;
    }

    if (handleRevealShortcut(event)) {
        return true;
    }

    if (isTimelineContext() &&
        !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
        if (auto *timeline = mMainWindow.getTimeLineWidget()) {
            if (event->key() == Qt::Key_B) {
                timeline->setIn();
                return true;
            }
            if (event->key() == Qt::Key_N) {
                timeline->setOut();
                return true;
            }
        }
    }

    if (mPendingUTimer.isActive()) {
        mPendingUTimer.stop();
    }

    if (handleGlobalShortcut(event)) {
        return true;
    }

    return handleToolShortcut(event);
}

bool AeShortcutController::handleToolShortcut(QKeyEvent *event)
{
    if (matches(event, QStringLiteral("boxTransform"), QStringLiteral("V"))) {
        mActions.setMovePathMode();
        showStatusMessage(tr("AE: Selection Tool"));
        return true;
    }

    if (matches(event, QStringLiteral("rectMode"), QStringLiteral("Q"))) {
        if (mShapeMode == CanvasMode::rectCreate) {
            mActions.setRectangleMode();
            mShapeMode = CanvasMode::circleCreate;
            showStatusMessage(tr("AE: Rectangle Tool"));
        } else {
            mActions.setCircleMode();
            mShapeMode = CanvasMode::rectCreate;
            showStatusMessage(tr("AE: Ellipse Tool"));
        }
        return true;
    }

    if (matches(event, QStringLiteral("drawPath"), QStringLiteral("G"))) {
        mActions.setDrawPathMode();
        showStatusMessage(tr("AE: Pen Tool"));
        return true;
    }

    if (matches(event, QStringLiteral("aeToolRotate"), QStringLiteral("W"))) {
        mActions.setMovePathMode();
        showStatusMessage(tr("AE: Rotate Tool uses the selection gizmo"));
        return true;
    }

    if (matches(event, QStringLiteral("pointTransform"), QStringLiteral("Y"))) {
        mActions.setMovePointMode();
        showStatusMessage(tr("AE: Anchor Point Tool mapped to point editing"));
        return true;
    }

    if (matches(event, QStringLiteral("aeToolHand"), QStringLiteral("H"))) {
        showStatusMessage(tr("AE: Pan with middle mouse button in the viewer"));
        return true;
    }

    if (matches(event, QStringLiteral("aeToolZoom"), QStringLiteral("Z"))) {
        showStatusMessage(tr("AE: Zoom with mouse wheel or +/- in the viewer"));
        return true;
    }

    if (matches(event, QStringLiteral("textMode"), QStringLiteral("Ctrl+T"))) {
        if (mDocument.fCanvasMode == CanvasMode::textCreate) {
            mDocument.fAeVerticalText = !mDocument.fAeVerticalText;
            showStatusMessage(mDocument.fAeVerticalText
                                  ? tr("AE: Vertical Text Tool")
                                  : tr("AE: Horizontal Text Tool"));
        } else {
            mDocument.fAeVerticalText = false;
            mActions.setTextMode();
            showStatusMessage(tr("AE: Horizontal Text Tool"));
        }
        return true;
    }

    return false;
}

bool AeShortcutController::handleGlobalShortcut(QKeyEvent *event)
{
    const auto scene = *mDocument.fActiveScene;

    if (matches(event, QStringLiteral("aeCenterPivot"), QStringLiteral("Ctrl+Home"))) {
        if (scene) {
            scene->centerPivotForSelected();
            showStatusMessage(tr("AE: Center Pivot"));
            return true;
        }
    }

    if (matches(event, QStringLiteral("aeAlignCenter"), QStringLiteral("Ctrl+Alt+Home"))) {
        if (scene) {
            scene->alignSelectedBoxes(Qt::AlignHCenter,
                                      AlignPivot::geometry,
                                      AlignRelativeTo::scene);
            scene->alignSelectedBoxes(Qt::AlignVCenter,
                                      AlignPivot::geometry,
                                      AlignRelativeTo::scene);
            showStatusMessage(tr("AE: Align to Composition Center"));
            return true;
        }
    }

    if (matches(event, QStringLiteral("aeCompSettings"), QStringLiteral("Ctrl+K"))) {
        if (mActions.sceneSettingsAction) {
            (*mActions.sceneSettingsAction)();
            return true;
        }
    }

    if (matches(event, QStringLiteral("aeAddRenderQueue"), QStringLiteral("Ctrl+M"))) {
        mMainWindow.addCanvasToRenderQue();
        return true;
    }

    if (matches(event, QStringLiteral("aePrecompose"), QStringLiteral("Ctrl+Shift+C"))) {
        if (scene && scene->precomposeSelectedBoxes()) {
            showStatusMessage(tr("AE: Pre-compose"));
            return true;
        }
    }

    if (matches(event, QStringLiteral("aeTimeRemap"), QStringLiteral("Ctrl+Alt+T"))) {
        if (auto *timeline = mMainWindow.getTimeLineWidget()) {
            return timeline->toggleSelectedTimeRemapping();
        }
    }

    if (matches(event, QStringLiteral("aeFreezeFrame"), QStringLiteral("Ctrl+Alt+Shift+F"))) {
        if (auto *timeline = mMainWindow.getTimeLineWidget()) {
            return timeline->freezeSelectedFramesAtCurrentTime();
        }
    }

    if (matches(event, QStringLiteral("aeFitComp"), QStringLiteral("Shift+/"))) {
        if (const auto canvasWindow = currentCanvasWindow()) {
            canvasWindow->fitCanvasToSize();
            return true;
        }
    }

    return false;
}

bool AeShortcutController::handleRevealShortcut(QKeyEvent *event)
{
    if (!isTimelineContext()) {
        return false;
    }

    const bool plainKey = !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));

    if ((plainKey && event->key() == Qt::Key_A) ||
        matches(event, QStringLiteral("aeRevealAnchor"), QStringLiteral("A"))) {
        applyRevealPreset(BoxScrollWidget::AeRevealPreset::AnchorPoint);
        return true;
    }

    if ((plainKey && event->key() == Qt::Key_P) ||
        matches(event, QStringLiteral("aeRevealPosition"), QStringLiteral("P"))) {
        applyRevealPreset(BoxScrollWidget::AeRevealPreset::Position);
        return true;
    }

    if ((plainKey && event->key() == Qt::Key_S) ||
        matches(event, QStringLiteral("aeRevealScale"), QStringLiteral("S"))) {
        applyRevealPreset(BoxScrollWidget::AeRevealPreset::Scale);
        return true;
    }

    if ((plainKey && event->key() == Qt::Key_R) ||
        matches(event, QStringLiteral("aeRevealRotation"), QStringLiteral("R"))) {
        applyRevealPreset(BoxScrollWidget::AeRevealPreset::Rotation);
        return true;
    }

    if ((plainKey && event->key() == Qt::Key_T) ||
        matches(event, QStringLiteral("aeRevealOpacity"), QStringLiteral("T"))) {
        applyRevealPreset(BoxScrollWidget::AeRevealPreset::Opacity);
        return true;
    }

    if ((plainKey && event->key() == Qt::Key_M) ||
        matches(event, QStringLiteral("aeRevealMasks"), QStringLiteral("M"))) {
        applyRevealPreset(BoxScrollWidget::AeRevealPreset::Masks);
        return true;
    }

    if ((plainKey && event->key() == Qt::Key_U) ||
        matches(event, QStringLiteral("aeRevealAnimated"), QStringLiteral("U"))) {
        if (mPendingUTimer.isActive()) {
            mPendingUTimer.stop();
            applyRevealPreset(BoxScrollWidget::AeRevealPreset::Modified);
        } else {
            applyRevealPreset(BoxScrollWidget::AeRevealPreset::Keyframed);
            mPendingUTimer.start();
        }
        return true;
    }

    return false;
}

bool AeShortcutController::isTimelineContext() const
{
    const auto *timeline = mMainWindow.getTimeLineWidget();
    if (timeline) {
        const QRect timelineGlobalRect(timeline->mapToGlobal(QPoint(0, 0)),
                                       timeline->size());
        if (timelineGlobalRect.contains(QCursor::pos())) {
            return true;
        }
    }
    if (belongsToTimeline(QApplication::focusWidget())) {
        return true;
    }
    if (belongsToTimeline(QApplication::widgetAt(QCursor::pos()))) {
        return true;
    }
    return timeline && timeline->underMouse();
}

bool AeShortcutController::belongsToTimeline(QWidget *widget) const
{
    const auto *timeline = mMainWindow.getTimeLineWidget();
    if (!timeline) {
        return false;
    }

    while (widget) {
        if (widget == timeline) {
            return true;
        }
        widget = widget->parentWidget();
    }
    return false;
}

bool AeShortcutController::shouldIgnoreBecauseOfFocusedEditor(QKeyEvent *event) const
{
    Q_UNUSED(event)
    if (isTimelineContext()) {
        return false;
    }
    const auto focus = QApplication::focusWidget();
    if (!focus) {
        return false;
    }

    if (qobject_cast<QLineEdit*>(focus) ||
        qobject_cast<QAbstractSpinBox*>(focus) ||
        qobject_cast<QTextEdit*>(focus) ||
        qobject_cast<QPlainTextEdit*>(focus) ||
        focus->inherits("QsciScintilla")) {
        return true;
    }

    return false;
}

bool AeShortcutController::matches(QKeyEvent *event,
                                   const QString &id,
                                   const QString &defaultSequence) const
{
    if (!event) {
        return false;
    }

    const auto configured = aeShortcutSequence(id, defaultSequence);
    if (configured.isEmpty()) {
        return false;
    }

    const QKeySequence pressed(event->modifiers() | event->key());
    return configured.matches(pressed) == QKeySequence::ExactMatch;
}

void AeShortcutController::showStatusMessage(const QString &message,
                                             int timeout) const
{
    if (mMainWindow.statusBar()) {
        mMainWindow.statusBar()->showMessage(message, timeout);
    }
}

CanvasWindow *AeShortcutController::currentCanvasWindow() const
{
    return dynamic_cast<CanvasWindow*>(KeyFocusTarget::KFT_getCurrentTarget());
}

void AeShortcutController::applyRevealPreset(BoxScrollWidget::AeRevealPreset preset)
{
    if (auto *timeline = mMainWindow.getTimeLineWidget()) {
        timeline->applyAeRevealPreset(preset);
    }
}
