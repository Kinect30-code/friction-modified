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

#include "boxtargetwidget.h"
#include <QMimeData>
#include <QPainter>
#include <QMenu>
#include <QAction>
#include "canvas.h"
#include "Properties/boxtargetproperty.h"
#include "GUI/mainwindow.h"
#include "Private/document.h"
#include "Properties/emimedata.h"
#include "themesupport.h"

namespace {

struct TargetMenuEntry {
    BoundingBox *box = nullptr;
    QString label;
};

QString displayNameForTarget(BoundingBox *box, Canvas *scene)
{
    if (!box) {
        return QString();
    }

    QStringList parts;
    parts.append(box->prp_getName());
    auto *current = box->getParentGroup();
    while (current && current != scene) {
        parts.prepend(current->prp_getName());
        current = current->getParentGroup();
    }
    return parts.join(QStringLiteral(" / "));
}

QList<TargetMenuEntry> collectTargetEntries(Document *document,
                                            Canvas *currentScene)
{
    QList<TargetMenuEntry> entries;
    if (!document || !currentScene) {
        return entries;
    }

    entries.append({
                       currentScene,
                       QStringLiteral("[Scene] %1").arg(currentScene->prp_getName())
                   });

    const auto &boxes = currentScene->getContainedBoxes();
    for (auto *box : boxes) {
        if (!box) {
            continue;
        }
        entries.append({box, box->prp_getName()});
    }

    return entries;
}

void configureActionState(QAction *action,
                          BoundingBox *currentTarget,
                          BoundingBox *entryTarget)
{
    if (!action || currentTarget != entryTarget) {
        return;
    }
    action->setCheckable(true);
    action->setChecked(true);
    action->setDisabled(true);
}

QAction *addTargetAction(QMenu *menu,
                         const TargetMenuEntry &entry,
                         BoundingBox *currentTarget,
                         const std::function<void(BoundingBox*)> &onSelect)
{
    if (!menu) {
        return nullptr;
    }
    auto *action = menu->addAction(entry.label);
    QObject::connect(action, &QAction::triggered, menu, [entry, onSelect]() {
        onSelect(entry.box);
    });
    configureActionState(action, currentTarget, entry.box);
    return action;
}

}

BoxTargetWidget::BoxTargetWidget(QWidget *parent) : QWidget(parent) {
    setAcceptDrops(true);
    setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
    setMinimumWidth(110);
    setMaximumWidth(QWIDGETSIZE_MAX);
}

QSize BoxTargetWidget::sizeHint() const {
    return {220, qMax(22, height())};
}

QSize BoxTargetWidget::minimumSizeHint() const {
    return {110, 22};
}

void BoxTargetWidget::setTargetProperty(BoxTargetProperty *property) {
    auto& conn = mProperty.assign(property);
    if(property) conn << connect(property, &BoxTargetProperty::targetSet,
                                 this, qOverload<>(&QWidget::update));
    update();
}

void BoxTargetWidget::dropEvent(QDropEvent *event) {
    if(!mProperty) return;
    const auto mimeData = event->mimeData();
    mProperty->SWT_drop(mimeData);
    mDragging = false;
    update();
    Document::sInstance->actionFinished();
}

void BoxTargetWidget::dragEnterEvent(QDragEnterEvent *event) {
    if(!mProperty) return;
    const auto mimeData = event->mimeData();
    const bool support = mProperty->SWT_dropSupport(mimeData);
    if(!support) return;
    event->acceptProposedAction();
    mDragging = true;
    update();
}

void BoxTargetWidget::dragMoveEvent(QDragMoveEvent *event) {
    event->acceptProposedAction();
}

void BoxTargetWidget::dragLeaveEvent(QDragLeaveEvent *event) {
    mDragging = false;
    update();
    event->accept();
}

void BoxTargetWidget::mousePressEvent(QMouseEvent *event) {
    if(!mProperty) return;
    if(event->button() == Qt::LeftButton) {
        const auto parentBox = mProperty->getFirstAncestor<BoundingBox>();
        if(!parentBox) return;
        auto *scene = parentBox->getParentScene();
        if(!scene) return;
        const auto entries = collectTargetEntries(Document::sInstance, scene);
        QMenu menu(this);

        const auto currentTarget = mProperty->getTarget();
        {
            const auto act = menu.addAction("-none-");
            connect(act, &QAction::triggered, this, [this]() {
                mProperty->setTargetAction(nullptr);
                Document::sInstance->actionFinished();
            });
            configureActionState(act, currentTarget, nullptr);
        }
        menu.addSeparator();

        QMenu *currentSceneMenu = nullptr;
        QMenu *sceneMenu = nullptr;
        QHash<Canvas*, QMenu*> otherSceneMenus;

        const auto selectTarget = [this](BoundingBox *box) {
            mProperty->setTargetAction(box);
            Document::sInstance->actionFinished();
        };

        for (const auto &entry : entries) {
            auto *box = entry.box;
            if(box == parentBox) continue;
            const auto& validator = mProperty->validator();
            if(validator && !validator(box)) continue;

            if (enve_cast<Canvas*>(box)) {
                if (!sceneMenu) {
                    sceneMenu = menu.addMenu(tr("Scene"));
                }
                addTargetAction(sceneMenu, entry, currentTarget, selectTarget);
            } else {
                if (!currentSceneMenu) {
                    currentSceneMenu = menu.addMenu(tr("Layers"));
                }
                addTargetAction(currentSceneMenu, entry, currentTarget, selectTarget);
            }
        }
        menu.exec(mapToGlobal(QPoint(0, height())));
    } else if(event->button() == Qt::RightButton) {

    }
}

void BoxTargetWidget::paintEvent(QPaintEvent *) {
    if(!mProperty) return;
    QPainter p(this);
    if(mProperty->SWT_isDisabled()) p.setOpacity(.5);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(ThemeSupport::getThemeButtonBorderColor());
    if(mDragging) {
        p.setPen(ThemeSupport::getThemeHighlightSelectedColor());
    } else {
        p.setPen(ThemeSupport::getThemeButtonBaseColor());
    }
    p.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 5., 5.);

    p.setPen(Qt::white);
    const auto target = mProperty->getTarget();
    QString text = target ? target->prp_getName() : QStringLiteral("-none-");
    if (const auto targetScene = enve_cast<Canvas*>(target)) {
        text = QStringLiteral("[Scene] %1").arg(targetScene->prp_getName());
    } else if (target) {
        text = displayNameForTarget(target, target->getParentScene());
    }
    if(!target) {
        p.drawText(rect().adjusted(8, 0, -8, 0),
                   Qt::AlignVCenter | Qt::AlignLeft,
                   text);
    } else {
        p.drawText(rect().adjusted(8, 0, -8, 0),
                   Qt::AlignVCenter | Qt::AlignLeft,
                   QFontMetrics(font()).elidedText(text, Qt::ElideRight,
                                                   qMax(20, width() - 16)));
    }

    p.end();
}
