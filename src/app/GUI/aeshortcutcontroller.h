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

#ifndef FRICTION_AESHORTCUTCONTROLLER_H
#define FRICTION_AESHORTCUTCONTROLLER_H

#include <QObject>

#include "Private/document.h"
#include "actions.h"
#include "GUI/BoxesList/boxscrollwidget.h"

class MainWindow;
class CanvasWindow;
class QKeyEvent;
class QWidget;

class AeShortcutController : public QObject
{
    Q_OBJECT

public:
    AeShortcutController(MainWindow &mainWindow,
                         Document &document,
                         Actions &actions);

    bool process(QKeyEvent *event);

private:
    bool handleToolShortcut(QKeyEvent *event);
    bool handleGlobalShortcut(QKeyEvent *event);
    bool handleRevealShortcut(QKeyEvent *event);
    bool shouldIgnoreBecauseOfFocusedEditor(QKeyEvent *event) const;
    bool isTimelineContext() const;
    bool belongsToTimeline(QWidget *widget) const;
    bool matches(QKeyEvent *event,
                 const QString &id,
                 const QString &defaultSequence) const;
    void showStatusMessage(const QString &message, int timeout = 3000) const;
    CanvasWindow *currentCanvasWindow() const;
    void applyRevealPreset(BoxScrollWidget::AeRevealPreset preset);

    MainWindow &mMainWindow;
    Document &mDocument;
    Actions &mActions;
    CanvasMode mShapeMode = CanvasMode::rectCreate;
};

#endif // FRICTION_AESHORTCUTCONTROLLER_H
