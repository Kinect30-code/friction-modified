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

#include "uilayout.h"
#include "appsupport.h"
#include "themesupport.h"
#include "Private/esettings.h"

#include <QApplication>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QHBoxLayout>
#include <QDebug>
#include <QCloseEvent>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QTimer>
#include <QTabBar>

#define UI_CONF_GROUP "uiLayout"
#define UI_CONF_KEY_POS "pos_%1"
#define UI_CONF_KEY_INDEX "index_%1"
#define UI_CONF_KEY_FLOATING "floating_%1"
#define UI_CONF_KEY_FLOATING_GEOMETRY "floatingGeometry_%1"
#define UI_CONF_KEY_MAIN "uiMain"
#define UI_CONF_KEY_COLUMNS "uiColumns"
#define UI_CONF_KEY_LEFT "uiLeft"
#define UI_CONF_KEY_MIDDLE "uiMiddle"
#define UI_CONF_KEY_RIGHT "uiRight"
#define UI_CONF_KEY_TOP "uiTop"
#define UI_CONF_KEY_BOTTOM_CURRENT "uiBottomCurrent"
#define UI_CONF_KEY_COLLAPSED "collapsed_%1"

class FloatingDockWindow final : public QWidget
{
    Q_OBJECT
public:
    explicit FloatingDockWindow(const QString &label,
                                QWidget *hostWindow,
                                QWidget *parent = nullptr)
        : QWidget(parent)
        , mLabel(label)
        , mHostWindow(hostWindow)
    {
        setAttribute(Qt::WA_DeleteOnClose, false);
        setWindowFlag(Qt::Tool, true);
        setWindowFlag(Qt::WindowStaysOnTopHint, false);
        setWindowTitle(label);
        mLayout = new QVBoxLayout(this);
        mLayout->setContentsMargins(0, 0, 0, 0);
        mLayout->setSpacing(0);
    }

    void setInteractiveDrag(bool active)
    {
        mInteractiveDrag = active;
        if (!active) {
            mSnapTimer.start(40);
        }
    }

    void setDockWidget(QWidget *dock)
    {
        if (!dock) { return; }
        mLayout->addWidget(dock);
    }

signals:
    void requestDockBack(const QString &label,
                         int pos = -1);

protected:
    void closeEvent(QCloseEvent *event) override
    {
        if (isVisible()) {
            mSnapTimer.stop();
        }
        event->accept();
    }

    void moveEvent(QMoveEvent *event) override
    {
        QWidget::moveEvent(event);
        if (!isVisible() || mInteractiveDrag) { return; }
        mSnapTimer.start(140);
    }

private:
    void trySnapToHost()
    {
        if (!mHostWindow || !isVisible()) { return; }

        const QRect hostRect = mHostWindow->frameGeometry();
        if (!hostRect.isValid()) { return; }

        const QRect selfRect = frameGeometry();
        constexpr int threshold = 120;
        constexpr int minOverlap = 48;

        int bestDistance = threshold + 1;
        int bestPos = -1;

        const auto overlapLength = [](const int a1,
                                      const int a2,
                                      const int b1,
                                      const int b2) {
            return qMax(0, qMin(a2, b2) - qMax(a1, b1));
        };

        const int overlapX = overlapLength(selfRect.left(),
                                           selfRect.right(),
                                           hostRect.left(),
                                           hostRect.right());
        const int overlapY = overlapLength(selfRect.top(),
                                           selfRect.bottom(),
                                           hostRect.top(),
                                           hostRect.bottom());

        const auto consider = [&](const int distance,
                                  const int overlap,
                                  const UIDock::Position pos) {
            if (overlap < minOverlap || distance > threshold || distance >= bestDistance) { return; }
            bestDistance = distance;
            bestPos = static_cast<int>(pos);
        };

        consider(qAbs(selfRect.left() - hostRect.left()),
                 overlapY,
                 UIDock::Position::Left);
        consider(qAbs(selfRect.right() - hostRect.right()),
                 overlapY,
                 UIDock::Position::Right);
        consider(qAbs(selfRect.top() - hostRect.top()),
                 overlapX,
                 UIDock::Position::Up);
        consider(qAbs(selfRect.bottom() - hostRect.bottom()),
                 overlapX,
                 UIDock::Position::Down);

        if (bestPos >= 0) {
            emit requestDockBack(mLabel, bestPos);
        }
    }

    QString mLabel;
    QWidget *mHostWindow = nullptr;
    QVBoxLayout *mLayout = nullptr;
    QTimer mSnapTimer;
    bool mInteractiveDrag = false;

private slots:
    void onSnapTimeout()
    {
        trySnapToHost();
    }

public:
    void initializeSnapTimer()
    {
        mSnapTimer.setSingleShot(true);
        connect(&mSnapTimer, &QTimer::timeout,
                this, &FloatingDockWindow::onSnapTimeout);
    }
};

class DockHeaderWidget final : public QWidget
{
    Q_OBJECT
public:
    explicit DockHeaderWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setCursor(Qt::OpenHandCursor);
    }

signals:
    void dragStarted(const QPoint &globalPos,
                     const QPoint &localPos);
    void dragMoved(const QPoint &globalPos);
    void dragFinished();

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton) {
            QWidget::mousePressEvent(event);
            return;
        }
        mPressed = true;
        mDragStarted = false;
        mPressPos = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!mPressed) {
            QWidget::mouseMoveEvent(event);
            return;
        }

        if (!mDragStarted &&
            (event->pos() - mPressPos).manhattanLength() >= QApplication::startDragDistance()) {
            mDragStarted = true;
            emit dragStarted(event->globalPos(), mPressPos);
        }
        if (mDragStarted) {
            emit dragMoved(event->globalPos());
        }
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        const bool hadDrag = mDragStarted;
        mPressed = false;
        mDragStarted = false;
        setCursor(Qt::OpenHandCursor);
        if (hadDrag) {
            emit dragFinished();
            event->accept();
            return;
        }
        QWidget::mouseReleaseEvent(event);
    }

private:
    bool mPressed = false;
    bool mDragStarted = false;
    QPoint mPressPos;
};

UIDock::UIDock(QWidget *parent,
               QWidget *widget,
               const QString &label,
               const Position &pos,
               const bool &showLabel,
               const bool &showHeader,
               const bool &darkHeader)
    : QWidget{parent}
    , mLayout(nullptr)
    , mLabel(label)
    , mPos(pos)
    , mIndex(-1)
{
    setObjectName(mLabel);
    setContentsMargins(0, 0, 0, 0);
    mLayout = new QVBoxLayout(this);
    mLayout->setContentsMargins(0, 0, 0, 0);
    mLayout->setMargin(0);
    mLayout->setSpacing(0);

    if (showHeader) {
        const auto headerWidget = new DockHeaderWidget(this);
        mHeaderWidget = headerWidget;
        const auto headerLayout = new QHBoxLayout(headerWidget);
        headerWidget->setObjectName(QStringLiteral("UIDockHeader"));

        if (darkHeader) {
            headerWidget->setAutoFillBackground(true);
            headerWidget->setPalette(ThemeSupport::getDarkPalette());
        }

        headerWidget->setContentsMargins(0, 0, 0, 0);
        headerWidget->setSizePolicy(QSizePolicy::Expanding,
                                    QSizePolicy::Fixed);
        headerLayout->setContentsMargins(0, 0, 0, 0);
        headerLayout->setMargin(0);
        headerLayout->setSpacing(0);

        const auto collapseButton = new QToolButton(this);
        const auto leftButton = new QPushButton(this);
        const auto rightButton = new QPushButton(this);
        const auto upButton = new QPushButton(this);
        const auto downButton = new QPushButton(this);
        const auto floatButton = new QPushButton(this);
        mCollapseButton = collapseButton;

        collapseButton->setArrowType(Qt::DownArrow);
        collapseButton->setAutoRaise(true);
        leftButton->setIcon(QIcon::fromTheme("leftarrow"));
        rightButton->setIcon(QIcon::fromTheme("rightarrow"));
        upButton->setIcon(QIcon::fromTheme("uparrow"));
        downButton->setIcon(QIcon::fromTheme("downarrow"));
        floatButton->setIcon(QIcon::fromTheme("window-new"));

        collapseButton->setFocusPolicy(Qt::NoFocus);
        leftButton->setFocusPolicy(Qt::NoFocus);
        rightButton->setFocusPolicy(Qt::NoFocus);
        upButton->setFocusPolicy(Qt::NoFocus);
        downButton->setFocusPolicy(Qt::NoFocus);
        floatButton->setFocusPolicy(Qt::NoFocus);

        collapseButton->setObjectName("FlatButton");
        leftButton->setObjectName("FlatButton");
        rightButton->setObjectName("FlatButton");
        upButton->setObjectName("FlatButton");
        downButton->setObjectName("FlatButton");
        floatButton->setObjectName("FlatButton");

        collapseButton->setToolTip(tr("Collapse panel"));
        leftButton->setToolTip(tr("Left"));
        rightButton->setToolTip(tr("Right"));
        upButton->setToolTip(tr("Up"));
        downButton->setToolTip(tr("Down"));
        floatButton->setToolTip(tr("Float panel"));

        collapseButton->setSizePolicy(QSizePolicy::Fixed,
                                      QSizePolicy::Fixed);
        leftButton->setSizePolicy(QSizePolicy::Fixed,
                                  QSizePolicy::Fixed);
        rightButton->setSizePolicy(QSizePolicy::Fixed,
                                   QSizePolicy::Fixed);
        upButton->setSizePolicy(QSizePolicy::Fixed,
                                QSizePolicy::Fixed);
        downButton->setSizePolicy(QSizePolicy::Fixed,
                                  QSizePolicy::Fixed);
        floatButton->setSizePolicy(QSizePolicy::Fixed,
                                   QSizePolicy::Fixed);

        headerLayout->addWidget(collapseButton);
        headerLayout->addWidget(leftButton);
        headerLayout->addWidget(rightButton);
        headerLayout->addWidget(floatButton);
        headerLayout->addStretch();

        if (showLabel) {
            const auto dockLabel = new QLabel(this);
            mDockLabel = dockLabel;
            dockLabel->setObjectName(QStringLiteral("UIDockLabel"));
            dockLabel->setText(mLabel);
            headerLayout->addWidget(dockLabel);
            headerLayout->addStretch();
        }

        headerLayout->addWidget(upButton);
        headerLayout->addWidget(downButton);

        mLayout->addWidget(headerWidget);

        mSideCollapseWidgets << leftButton
                             << rightButton
                             << floatButton
                             << upButton
                             << downButton;
        if (mDockLabel) { mSideCollapseWidgets << mDockLabel; }

        connect(collapseButton, &QToolButton::clicked,
                this, [this]() { setCollapsed(!mCollapsed); });
        connect(leftButton, &QPushButton::clicked,
                this, [this]() { emit changePosition(mPos, Position::Left); });
        connect(rightButton, &QPushButton::clicked,
                this, [this]() { emit changePosition(mPos, Position::Right); });
        connect(upButton, &QPushButton::clicked,
                this, [this]() { emit changePosition(mPos, Position::Up); });
        connect(downButton, &QPushButton::clicked,
                this, [this]() { emit changePosition(mPos, Position::Down); });
        connect(floatButton, &QPushButton::clicked,
                this, [this]() { emit requestFloating(!mFloating); });
        connect(headerWidget, &DockHeaderWidget::dragStarted,
                this, [this](const QPoint &globalPos, const QPoint &localPos) {
            emit beginInteractiveDrag(globalPos, localPos);
        });
        connect(headerWidget, &DockHeaderWidget::dragMoved,
                this, [this](const QPoint &globalPos) {
            emit updateInteractiveDrag(globalPos);
        });
        connect(headerWidget, &DockHeaderWidget::dragFinished,
                this, [this]() { emit endInteractiveDrag(); });
    }

    mContentWidget = new QWidget(this);
    mContentLayout = new QVBoxLayout(mContentWidget);
    mContentLayout->setContentsMargins(0, 0, 0, 0);
    mContentLayout->setSpacing(0);
    mLayout->addWidget(mContentWidget);
    addWidget(widget);
    updateCollapseUi();
}

UIDock::~UIDock()
{
    writeSettings();
}

void UIDock::setPosition(const Position &pos)
{
    mPos = pos;
    applyCollapseState(false);
}

UIDock::Position UIDock::getPosition()
{
    return mPos;
}

void UIDock::setIndex(const int &index)
{
    mIndex = index;
}

int UIDock::getIndex()
{
    return mIndex;
}

const QString UIDock::getLabel()
{
    return mLabel;
}

const QString UIDock::getId()
{
    return AppSupport::filterTextAZW(mLabel);
}

void UIDock::addWidget(QWidget *widget)
{
    if (!widget) { return; }
    if (mContentLayout) {
        mContentLayout->addWidget(widget);
    } else {
        mLayout->addWidget(widget);
    }
}

void UIDock::setFloating(bool floating)
{
    mFloating = floating;
}

void UIDock::setCollapsed(bool collapsed)
{
    if (mCollapsed == collapsed) { return; }
    mCollapsed = collapsed;
    applyCollapseState(true);
    writeSettings();
}

void UIDock::updateCollapseUi()
{
    const bool sideStripCollapsed =
        mCollapsed && (mPos == Position::Left || mPos == Position::Right);

    if (const auto button = qobject_cast<QToolButton*>(mCollapseButton)) {
        button->setArrowType(mCollapsed ? Qt::RightArrow : Qt::DownArrow);
        button->setToolTip(mCollapsed ? tr("Expand panel")
                                      : tr("Collapse panel"));
    }

    for (auto *widget : mSideCollapseWidgets) {
        if (widget) { widget->setVisible(!sideStripCollapsed); }
    }
}

int UIDock::currentExtent() const
{
    const bool collapseWidth = (mPos == Position::Left || mPos == Position::Right);
    const int current = collapseWidth ? width() : height();
    if (current > 0) { return current; }
    const QSize hint = sizeHint();
    return collapseWidth ? hint.width() : hint.height();
}

int UIDock::collapsedExtent() const
{
    const bool collapseWidth = (mPos == Position::Left || mPos == Position::Right);
    if (mHeaderWidget) {
        const QSize hint = mHeaderWidget->minimumSizeHint();
        return qMax(24, collapseWidth ? hint.width() : hint.height());
    }
    return 24;
}

void UIDock::applyCollapseState(bool adjustSplitter)
{
    updateCollapseUi();
    if (mContentWidget) { mContentWidget->setVisible(!mCollapsed); }

    setMinimumWidth(0);
    setMaximumWidth(QWIDGETSIZE_MAX);
    setMinimumHeight(0);
    setMaximumHeight(QWIDGETSIZE_MAX);

    const bool collapseWidth = (mPos == Position::Left || mPos == Position::Right);
    const int collapsedSize = collapsedExtent();
    if (mCollapsed) {
        const int extent = currentExtent();
        if (extent > collapsedSize + 8) {
            mExpandedExtent = extent;
        }
        if (collapseWidth) {
            setMinimumWidth(collapsedSize);
            setMaximumWidth(collapsedSize);
        } else {
            setMinimumHeight(collapsedSize);
            setMaximumHeight(collapsedSize);
        }
    }

    updateGeometry();

    if (!adjustSplitter) { return; }

    const int expandedDefault = collapseWidth ? 300 : 260;
    const int targetExtent = mCollapsed
                                 ? collapsedSize
                                 : qMax(mExpandedExtent, expandedDefault);

    QWidget *splitterChild = this;
    QWidget *splitterParent = parentWidget();
    while (splitterParent && !qobject_cast<QSplitter*>(splitterParent)) {
        splitterChild = splitterParent;
        splitterParent = splitterParent->parentWidget();
    }

    if (const auto splitter = qobject_cast<QSplitter*>(splitterParent)) {
        auto sizes = splitter->sizes();
        const int index = splitter->indexOf(splitterChild);
        if (index >= 0 && index < sizes.size()) {
            int currentSize = sizes.at(index);
            if (currentSize <= 0) { currentSize = currentExtent(); }
            const int delta = targetExtent - currentSize;
            sizes[index] = targetExtent;

            if (delta != 0) {
                int adjustIndex = -1;
                int largestSize = -1;
                for (int i = 0; i < sizes.size(); ++i) {
                    if (i == index) { continue; }
                    const auto candidate = splitter->widget(i);
                    if (!candidate || !candidate->isVisible()) { continue; }
                    if (sizes.at(i) > largestSize) {
                        largestSize = sizes.at(i);
                        adjustIndex = i;
                    }
                }
                if (adjustIndex >= 0) {
                    sizes[adjustIndex] = qMax(0, sizes.at(adjustIndex) - delta);
                }
            }

            splitter->setSizes(sizes);
        }
        return;
    }

    if (isFloating()) {
        const auto floatingWindow = parentWidget();
        if (!floatingWindow || floatingWindow == this) { return; }
        if (collapseWidth) {
            floatingWindow->resize(targetExtent, floatingWindow->height());
        } else {
            floatingWindow->resize(floatingWindow->width(), targetExtent);
        }
    }
}

void UIDock::writeSettings()
{
    qDebug() << "==> write dock conf" << mLabel << mPos << mIndex;
    if (eSettings::instance().fRestoreDefaultUi) {
        AppSupport::clearSettings(UI_CONF_GROUP);
        return;
    }
    AppSupport::setSettings(UI_CONF_GROUP,
                            QString(UI_CONF_KEY_POS).arg(getId()),
                            mPos);
    AppSupport::setSettings(UI_CONF_GROUP,
                            QString(UI_CONF_KEY_INDEX).arg(getId()),
                            mIndex);
    AppSupport::setSettings(UI_CONF_GROUP,
                            QString(UI_CONF_KEY_COLLAPSED).arg(getId()),
                            mCollapsed);
}

UILayout::UILayout(QWidget *parent)
    : QSplitter{parent}
    , mColumns(nullptr)
    , mLeft(nullptr)
    , mMiddle(nullptr)
    , mRight(nullptr)
    , mTop(nullptr)
    , mBottomTabs(nullptr)
{
    setOrientation(Qt::Vertical);

    mColumns = new QSplitter(this);
    mColumns->setOrientation(Qt::Horizontal);
    addWidget(mColumns);

    mLeft = new QSplitter(mColumns);
    mLeft->setOrientation(Qt::Vertical);
    mColumns->addWidget(mLeft);

    mMiddle = new QSplitter(mColumns);
    mMiddle->setOrientation(Qt::Vertical);
    mColumns->addWidget(mMiddle);

    mRight = new QSplitter(mColumns);
    mRight->setOrientation(Qt::Vertical);
    mColumns->addWidget(mRight);

    mTop = new QSplitter(this);
    mTop->setOrientation(Qt::Horizontal);
    mMiddle->addWidget(mTop);

    mBottomTabs = new QTabWidget(this);
    mBottomTabs->setObjectName("AePanelTabs");
    mBottomTabs->setDocumentMode(true);
    mBottomTabs->setTabsClosable(false);
    mBottomTabs->setMovable(false);
    mBottomTabs->tabBar()->setFocusPolicy(Qt::NoFocus);
    mBottomTabs->tabBar()->setExpanding(false);
    mBottomTabs->setContentsMargins(0, 0, 0, 0);
    mBottomTabs->setTabPosition(QTabWidget::North);
    addWidget(mBottomTabs);

    setCollapsible(indexOf(mColumns), false);
    setCollapsible(indexOf(mBottomTabs), false);

    mColumns->setCollapsible(mColumns->indexOf(mLeft), false);
    mColumns->setCollapsible(mColumns->indexOf(mMiddle), false);
    mColumns->setCollapsible(mColumns->indexOf(mRight), false);
    mMiddle->setCollapsible(mMiddle->indexOf(mTop), false);
}

UILayout::~UILayout()
{
    updateDocks();
    writeSettings();
}

void UILayout::readSettings()
{
    const bool firstrun = AppSupport::getSettings(UI_CONF_GROUP, UI_CONF_KEY_COLUMNS).isNull();
    const bool mainRestored =
        restoreState(AppSupport::getSettings(UI_CONF_GROUP, UI_CONF_KEY_MAIN).toByteArray());
    const bool columnsRestored =
        mColumns->restoreState(AppSupport::getSettings(UI_CONF_GROUP, UI_CONF_KEY_COLUMNS).toByteArray());
    mLeft->restoreState(AppSupport::getSettings(UI_CONF_GROUP, UI_CONF_KEY_LEFT).toByteArray());
    mMiddle->restoreState(AppSupport::getSettings(UI_CONF_GROUP, UI_CONF_KEY_MIDDLE).toByteArray());
    mRight->restoreState(AppSupport::getSettings(UI_CONF_GROUP, UI_CONF_KEY_RIGHT).toByteArray());
    mTop->restoreState(AppSupport::getSettings(UI_CONF_GROUP, UI_CONF_KEY_TOP).toByteArray());

    if (firstrun || !mainRestored || !columnsRestored) {
        applyAeDefaultWorkspace();
    }

    const int currentBottomTab =
        AppSupport::getSettings(UI_CONF_GROUP,
                                UI_CONF_KEY_BOTTOM_CURRENT,
                                0).toInt();
    if (currentBottomTab >= 0 && currentBottomTab < mBottomTabs->count()) {
        mBottomTabs->setCurrentIndex(currentBottomTab);
    }
    updateBottomTabsVisibility();
}

void UILayout::writeSettings()
{
    AppSupport::setSettings(UI_CONF_GROUP, UI_CONF_KEY_MAIN, saveState());
    AppSupport::setSettings(UI_CONF_GROUP, UI_CONF_KEY_COLUMNS, mColumns->saveState());
    AppSupport::setSettings(UI_CONF_GROUP, UI_CONF_KEY_LEFT, mLeft->saveState());
    AppSupport::setSettings(UI_CONF_GROUP, UI_CONF_KEY_MIDDLE, mMiddle->saveState());
    AppSupport::setSettings(UI_CONF_GROUP, UI_CONF_KEY_RIGHT, mRight->saveState());
    AppSupport::setSettings(UI_CONF_GROUP, UI_CONF_KEY_TOP, mTop->saveState());
    AppSupport::setSettings(UI_CONF_GROUP,
                            UI_CONF_KEY_BOTTOM_CURRENT,
                            mBottomTabs ? mBottomTabs->currentIndex() : 0);
    for (auto it = mDocks.constBegin(); it != mDocks.constEnd(); ++it) {
        const auto label = it.key();
        AppSupport::setSettings(UI_CONF_GROUP,
                                QString(UI_CONF_KEY_FLOATING).arg(AppSupport::filterTextAZW(label)),
                                mFloatingWindows.contains(label));
        if (const auto window = mFloatingWindows.value(label, nullptr)) {
            AppSupport::setSettings(UI_CONF_GROUP,
                                    QString(UI_CONF_KEY_FLOATING_GEOMETRY).arg(AppSupport::filterTextAZW(label)),
                                    window->saveGeometry());
        }
    }
}

void UILayout::applyAeDefaultWorkspace()
{
    setSizes({860, 340});
    mColumns->setSizes({360, 1320, 320});
    mLeft->setSizes({620, 320});
    mMiddle->setSizes({900});
    mRight->setSizes({420, 260, 220});
    mTop->setSizes({1280});
}

void UILayout::addDocks(std::vector<Item> items)
{
    if (items.size() <= 0) { return; }
    std::vector<Item> leftDock, rightDock, topDock, bottomDock;

    for (auto item : items) {
        if (!item.widget) { continue; }
        qDebug() << "==> setup new dock" << item.label;
        QString keyPos = QString(UI_CONF_KEY_POS).arg(AppSupport::filterTextAZW(item.label));
        QString keyIndex = QString(UI_CONF_KEY_INDEX).arg(AppSupport::filterTextAZW(item.label));

        const auto confIndex = AppSupport::getSettings(UI_CONF_GROUP, keyIndex);
        if (confIndex.isValid()) { item.index = confIndex.toInt(); }

        const auto confPos = AppSupport::getSettings(UI_CONF_GROUP, keyPos);
        if (confPos.isValid()) { item.pos = confPos.toInt(); }

        switch (item.pos) {
        case UIDock::Position::Left:
            leftDock.push_back(item);
            break;
        case UIDock::Position::Right:
            rightDock.push_back(item);
            break;
        case UIDock::Position::Up:
            topDock.push_back(item);
            break;
        case UIDock::Position::Down:
            bottomDock.push_back(item);
            break;
        }
    }

    if (leftDock.size() > 0) { std::sort(leftDock.begin(), leftDock.end()); }
    if (rightDock.size() > 0) { std::sort(rightDock.begin(), rightDock.end()); }
    if (topDock.size() > 0) { std::sort(topDock.begin(), topDock.end()); }
    if (bottomDock.size() > 0) { std::sort(bottomDock.begin(), bottomDock.end()); }

    for (const auto &item : leftDock) {
        qDebug() << "==> add to left dock" << item.label << item.index;
        addDock(item);
    }
    for (const auto &item : rightDock) {
        qDebug() << "==> add to right dock" << item.label << item.index;
        addDock(item);
    }
    for (const auto &item : topDock) {
        qDebug() << "==> add to top dock" << item.label << item.index;
        addDock(item);
    }
    for (const auto &item : bottomDock) {
        qDebug() << "==> add to bottom dock" << item.label << item.index;
        addDock(item);
    }

    updateDocks();
    readSettings();

    for (const auto &item : items) {
        const auto floating =
            AppSupport::getSettings(UI_CONF_GROUP,
                                    QString(UI_CONF_KEY_FLOATING).arg(AppSupport::filterTextAZW(item.label)),
                                    false).toBool();
        if (floating) {
            setDockFloating(item.label, true);
        }
    }
}

void UILayout::setDockVisible(const QString &label,
                              bool visible)
{
    mDockVisibility.insert(label, visible);
    emit updateDockVisibility(label, visible);
}

void UILayout::setDockFloating(const QString &label,
                               bool floating,
                               int targetPos)
{
    const auto dock = mDocks.value(label, nullptr);
    if (!dock) { return; }

    if (floating) {
        if (mFloatingWindows.contains(label)) {
            if (const auto window = mFloatingWindows.value(label, nullptr)) {
                window->show();
                window->raise();
                window->activateWindow();
            }
            dock->setFloating(true);
            return;
        }

        const auto window = new FloatingDockWindow(label, this->window(), this->window());
        window->initializeSnapTimer();
        connect(window, &FloatingDockWindow::requestDockBack,
                this, [this](const QString &dockLabel, int dockPos) {
            setDockFloating(dockLabel, false, dockPos);
        });
        mFloatingWindows.insert(label, window);

        const int tabIndex = mBottomTabs->indexOf(dock);
        if (tabIndex >= 0) {
            mBottomTabs->removeTab(tabIndex);
            updateBottomTabsVisibility();
        }
        dock->setParent(window);
        dock->setFloating(true);
        window->setDockWidget(dock);
        const auto geometry = AppSupport::getSettings(
                                  UI_CONF_GROUP,
                                  QString(UI_CONF_KEY_FLOATING_GEOMETRY).arg(AppSupport::filterTextAZW(label)))
                                  .toByteArray();
        if (!geometry.isEmpty()) {
            window->restoreGeometry(geometry);
        } else {
            window->resize(qMax(300, dock->width()),
                           qMax(220, dock->height()));
        }
        window->show();
        window->raise();
        window->activateWindow();
        setDockPageVisible(dock, mDockVisibility.value(label, true));
        updateDocks();
        return;
    }

    const auto window = mFloatingWindows.take(label);
    if (!window) {
        dock->setFloating(false);
        mFloatingDragOffsets.remove(label);
        return;
    }

    if (window) {
        AppSupport::setSettings(UI_CONF_GROUP,
                                QString(UI_CONF_KEY_FLOATING_GEOMETRY).arg(AppSupport::filterTextAZW(label)),
                                window->saveGeometry());
    }
    dock->setParent(nullptr);
    dock->setFloating(false);
    const auto pos = targetPos >= 0
                         ? static_cast<UIDock::Position>(targetPos)
                         : dock->getPosition();
    insertDock(dock, pos, dock->getIndex());
    window->hide();
    window->deleteLater();
    dock->show();
    mFloatingDragOffsets.remove(label);
    updateDocks();
}

bool UILayout::isDockFloating(const QString &label) const
{
    return mFloatingWindows.contains(label);
}

bool UILayout::isDockVisible(const QString &label) const
{
    return mDockVisibility.value(label, true);
}

void UILayout::addDockWidget(const QString &label, QWidget *widget)
{
    if (!widget) { return; }
    emit updateDockWidget(label, widget);
}

void UILayout::addDock(const Item &item)
{
    if (!item.widget) { return; }
    qDebug() << "==> adding dock" << item.label << item.pos << item.index;
    const auto dock = new UIDock(this,
                                 item.widget,
                                 item.label,
                                 static_cast<UIDock::Position>(item.pos),
                                 item.showLabel,
                                 item.showHeader,
                                 item.darkHeader);
    mDocks.insert(item.label, dock);
    mDockVisibility.insert(item.label, item.widget->isVisible());
    insertDock(dock, static_cast<UIDock::Position>(item.pos), item.index);
    connectDock(dock);
    setDockPageVisible(dock, mDockVisibility.value(item.label, true));
    const bool collapsed =
        AppSupport::getSettings(UI_CONF_GROUP,
                                QString(UI_CONF_KEY_COLLAPSED).arg(dock->getId()),
                                false).toBool();
    if (collapsed) {
        QTimer::singleShot(0,
                           dock,
                           [dock]() { dock->setCollapsed(true); });
    }
}

void UILayout::connectDock(UIDock *dock)
{
    if (!dock) { return; }
    connect(this,
            &UILayout::updateDockVisibility,
            this,
            [this, dock](const QString &label,
                         bool visible) {
                if (dock->getLabel() != label) { return; }
                setDockPageVisible(dock, visible);
                if (const auto window = dock->window()) {
                    if (dock->isFloating() && window != dock) {
                        window->setVisible(visible);
                    }
                }
            });
    connect(this,
            &UILayout::updateDockWidget,
            this,
            [this, dock](const QString &label,
                         QWidget *widget) {
                if (dock->getLabel() == label) {
                    dock->addWidget(widget);
                    mDockVisibility.insert(label, true);
                    setDockPageVisible(dock, true);
                }
            });
    connect(dock,
            &UIDock::changePosition,
            this,
            [this, dock](const UIDock::Position &,
                         const UIDock::Position &trigger)
            {
                if (dock->getPosition() == trigger) { return; }
                insertDock(dock, trigger, -1);
                updateDocks();
            });
    connect(dock, &UIDock::requestFloating,
            this, [this, dock](bool floating) {
        setDockFloating(dock->getLabel(), floating);
    });
    connect(dock, &UIDock::beginInteractiveDrag,
            this, [this, dock](const QPoint &globalPos, const QPoint &localPos) {
        const QString label = dock->getLabel();
        if (!dock->isFloating()) {
            setDockFloating(label, true);
        }
        const auto window = qobject_cast<FloatingDockWindow*>(mFloatingWindows.value(label, nullptr));
        if (!window) { return; }
        window->setInteractiveDrag(true);
        QPoint offset = window->mapFromGlobal(globalPos);
        if (!window->rect().contains(offset)) {
            offset = localPos;
        }
        mFloatingDragOffsets.insert(label, offset);
        window->move(globalPos - offset);
    });
    connect(dock, &UIDock::updateInteractiveDrag,
            this, [this, dock](const QPoint &globalPos) {
        const QString label = dock->getLabel();
        const auto window = qobject_cast<FloatingDockWindow*>(mFloatingWindows.value(label, nullptr));
        if (!window) { return; }
        const QPoint offset = mFloatingDragOffsets.value(label, QPoint(24, 12));
        window->move(globalPos - offset);
    });
    connect(dock, &UIDock::endInteractiveDrag,
            this, [this, dock]() {
        const QString label = dock->getLabel();
        const auto window = qobject_cast<FloatingDockWindow*>(mFloatingWindows.value(label, nullptr));
        if (window) {
            window->setInteractiveDrag(false);
        }
        mFloatingDragOffsets.remove(label);
    });
}

void UILayout::updateDock(QSplitter *container,
                          const UIDock::Position &pos)
{
    if (!container) { return; }
    for (int i = 0; i < container->count(); ++i) {
        UIDock *dock = qobject_cast<UIDock*>(container->widget(i));
        if (!dock) { continue; }
        container->setCollapsible(container->indexOf(dock), false);
        dock->setPosition(pos);
        dock->setIndex(container->indexOf(dock));
        qDebug() << "==> update dock" << dock->getLabel() << dock->getPosition() << dock->getIndex();
    }
}

void UILayout::updateDocks()
{
    updateDock(mLeft, UIDock::Position::Left);
    updateDock(mRight, UIDock::Position::Right);
    updateDock(mTop, UIDock::Position::Up);
    for (int i = 0; i < mBottomTabs->count(); ++i) {
        UIDock *dock = qobject_cast<UIDock*>(mBottomTabs->widget(i));
        if (!dock) { continue; }
        dock->setPosition(UIDock::Position::Down);
        dock->setIndex(i);
        qDebug() << "==> update dock" << dock->getLabel() << dock->getPosition() << dock->getIndex();
    }
    updateBottomTabsVisibility();
}

QSplitter *UILayout::containerForPosition(const UIDock::Position &pos) const
{
    switch (pos) {
    case UIDock::Position::Left:
        return mLeft;
    case UIDock::Position::Right:
        return mRight;
    case UIDock::Position::Up:
        return mTop;
    case UIDock::Position::Down:
        return nullptr;
    }
    return nullptr;
}

void UILayout::insertDock(UIDock *dock,
                          const UIDock::Position &pos,
                          int index)
{
    if (!dock) { return; }
    const int bottomIndex = mBottomTabs->indexOf(dock);
    if (bottomIndex >= 0) {
        mBottomTabs->removeTab(bottomIndex);
    }

    if (pos == UIDock::Position::Down) {
        dock->setParent(nullptr);
        dock->setPosition(pos);
        if (index >= 0 && index < mBottomTabs->count()) {
            mBottomTabs->insertTab(index, dock, dock->getLabel());
        } else {
            mBottomTabs->addTab(dock, dock->getLabel());
        }
        if (mDockVisibility.value(dock->getLabel(), true)) {
            mBottomTabs->setCurrentWidget(dock);
        }
        setDockPageVisible(dock, mDockVisibility.value(dock->getLabel(), true));
        updateBottomTabsVisibility();
        return;
    }

    const auto container = containerForPosition(pos);
    if (!container) { return; }
    dock->setPosition(pos);
    if (index >= 0 && index < container->count()) {
        container->insertWidget(index, dock);
    } else {
        container->addWidget(dock);
    }
    container->setCollapsible(container->indexOf(dock), false);
    setDockPageVisible(dock, mDockVisibility.value(dock->getLabel(), true));
}

void UILayout::setDockPageVisible(UIDock *dock,
                                  bool visible)
{
    if (!dock) { return; }

    const int tabIndex = mBottomTabs->indexOf(dock);
    if (tabIndex >= 0) {
        mBottomTabs->tabBar()->setTabVisible(tabIndex, visible);
        dock->setVisible(visible);
        if (visible) {
            if (mBottomTabs->currentIndex() < 0 ||
                !mBottomTabs->tabBar()->isTabVisible(mBottomTabs->currentIndex())) {
                mBottomTabs->setCurrentIndex(tabIndex);
            }
        } else if (mBottomTabs->currentIndex() == tabIndex) {
            for (int i = 0; i < mBottomTabs->count(); ++i) {
                if (mBottomTabs->tabBar()->isTabVisible(i)) {
                    mBottomTabs->setCurrentIndex(i);
                    break;
                }
            }
        }
        updateBottomTabsVisibility();
        return;
    }

    dock->setVisible(visible);
}

void UILayout::updateBottomTabsVisibility()
{
    if (!mBottomTabs) { return; }

    bool hasVisibleTabs = false;
    for (int i = 0; i < mBottomTabs->count(); ++i) {
        if (mBottomTabs->tabBar()->isTabVisible(i)) {
            hasVisibleTabs = true;
            break;
        }
    }

    mBottomTabs->setVisible(hasVisibleTabs);
}

#include "uilayout.moc"
