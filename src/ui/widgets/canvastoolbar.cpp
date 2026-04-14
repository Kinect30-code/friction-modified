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

#include "canvastoolbar.h"
#include "GUI/global.h"
#include "Private/document.h"

#include <QLabel>

using namespace Friction::Ui;

CanvasToolBar::CanvasToolBar(QWidget *parent)
    : QToolBar(parent)
    , mSpinWidth(nullptr)
    , mSpinHeight(nullptr)
    , mMemoryLabel(nullptr)
    , mIconsOnly(AppSupport::getSettings("ui",
                                         "CanvasToolbarIconsOnly",
                                         false).toBool())
{
    eSizesUI::widget.add(this, [this](const int size) {
        this->setIconSize({size, size});
    });

    setEnabled(false);
    setWindowTitle(tr("Composition Toolbar"));
    setObjectName("CanvasToolBar");
    setToolButtonStyle(mIconsOnly ?
                           Qt::ToolButtonIconOnly :
                           Qt::ToolButtonTextBesideIcon);
    setAllowedAreas(Qt::AllToolBarAreas);
    setFloatable(true);

    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested,
            this, &CanvasToolBar::showContextMenu);

#ifdef Q_OS_MAC
    setStyleSheet(QString("font-size: %1pt;").arg(font().pointSize()));
#endif

    {
        const auto space = new QWidget(this);
        space->setSizePolicy(QSizePolicy::Expanding,
                             QSizePolicy::Expanding);
        addWidget(space);
    }

    mMemoryLabel = addAction(QIcon::fromTheme("memory"), tr("0 MB"));
    mMemoryLabel->setToolTip(tr("0 MB used"));

    setupDimensions();
}

void CanvasToolBar::setCurrentCanvas(Canvas * const target)
{
    mCanvas.assign(target);
    if (target) {
        mCanvas << connect(mSpinWidth,
                           QOverload<int>::of(&QSpinBox::valueChanged),
                           this, [this, target]() {
            setDimension({mSpinWidth->value(), mSpinHeight->value()},
                          target);
        });
        mCanvas << connect(mSpinHeight,
                           QOverload<int>::of(&QSpinBox::valueChanged),
                           this, [this, target]() {
            setDimension({mSpinWidth->value(), mSpinHeight->value()},
                         target);
        });
        mCanvas << connect(target, &Canvas::dimensionsChanged,
                           this, [this](int width, int height) {
            updateDimension({width, height});
        });
    }
    updateWidgets(target);
}

void CanvasToolBar::setMemoryUsage(const intMB &usage)
{
    mMemoryLabel->setToolTip(tr("%1 MB used").arg(usage.fValue));
    mMemoryLabel->setText(tr("%1 MB").arg(usage.fValue));
}

void CanvasToolBar::setupDimensions()
{
    mSpinWidth = new QSpinBox(this);
    mSpinWidth->setFocusPolicy(Qt::ClickFocus);
    mSpinWidth->setObjectName("ComboSpinBox");
    mSpinWidth->setMinimum(1);
    mSpinWidth->setMaximum(99999);
    mSpinWidth->setKeyboardTracking(false);

    mSpinHeight = new QSpinBox(this);
    mSpinHeight->setFocusPolicy(Qt::ClickFocus);
    mSpinHeight->setObjectName("ComboSpinBox");
    mSpinHeight->setMinimum(1);
    mSpinHeight->setMaximum(99999);
    mSpinHeight->setKeyboardTracking(false);

    addAction(QIcon::fromTheme("width"),
              tr("Width"));
    addWidget(mSpinWidth);

    addAction(QIcon::fromTheme("height"),
              tr("Height"));

    addWidget(mSpinHeight);
}

void CanvasToolBar::addSpacer()
{
    const auto space = new QWidget(this);
    space->setSizePolicy(QSizePolicy::Expanding,
                         QSizePolicy::Minimum);
    addWidget(space);
}

void CanvasToolBar::updateWidgets(Canvas * const target)
{
    if (!target) {
        setEnabled(false);
        return;
    }
    setEnabled(true);

    updateDimension(target->getCanvasSize());
}

void CanvasToolBar::updateDimension(const QSize dim)
{
    mSpinWidth->blockSignals(true);
    mSpinWidth->setValue(dim.width());
    mSpinWidth->blockSignals(false);

    mSpinHeight->blockSignals(true);
    mSpinHeight->setValue(dim.height());
    mSpinHeight->blockSignals(false);
}

void CanvasToolBar::setDimension(const QSize dim,
                                 Canvas * const target)
{
    if (!target) { return; }

    target->setCanvasSize(dim.width(),
                          dim.height());
    Document::sInstance->actionFinished();
}

void CanvasToolBar::showContextMenu(const QPoint &pos)
{
    QMenu menu(this);
    {
        const auto act = menu.addAction(QIcon::fromTheme("window"),
                                        windowTitle());
        act->setEnabled(false);
        menu.addSeparator();
    }
    {
        const auto act = menu.addAction(tr("Labels"));
        act->setCheckable(true);
        act->setChecked(!mIconsOnly);
        connect(act, &QAction::triggered,
                this, [this](bool checked) {
            mIconsOnly = !checked;
            setToolButtonStyle(mIconsOnly ?
                                   Qt::ToolButtonIconOnly :
                                   Qt::ToolButtonTextBesideIcon);
            update();
            AppSupport::setSettings("ui",
                                    "CanvasToolbarIconsOnly",
                                    mIconsOnly);
        });
    }
    menu.exec(mapToGlobal(pos));
}
