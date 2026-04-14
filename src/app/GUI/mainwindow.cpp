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

#include "mainwindow.h"
#include "GUI/Expressions/expressiondialog.h"
#include "canvas.h"
#include <QKeyEvent>
#include <QApplication>
#include <QDebug>
#include <QDesktopServices>
#include <QUrl>
#include <QStatusBar>
#include <QToolBar>
#include <QMenuBar>
#include <QMessageBox>
#include <QAudioOutput>
#include <QSpacerItem>
#include <QMargins>
#include <iostream>
#include <QClipboard>
#include <QMimeData>
#include <QTreeWidget>
#include <QHeaderView>
#include <QTabWidget>
#include <QLineEdit>
#include <QAbstractSpinBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QTabBar>
#include <QVBoxLayout>

#include "GUI/edialogs.h"
#include "dialogs/applyexpressiondialog.h"
#include "dialogs/markereditordialog.h"
#include "timelinedockwidget.h"
#include "canvaswindow.h"
#include "GUI/BoxesList/boxscrollwidget.h"
#include "clipboardcontainer.h"
#include "optimalscrollarena/scrollarea.h"
#include "GUI/BoxesList/boxscroller.h"
#include "GUI/RenderWidgets/renderwidget.h"
#include "GUI/global.h"
#include "filesourcescache.h"
#include "widgets/fillstrokesettings.h"

#include "Sound/soundcomposition.h"
#include "GUI/BoxesList/boxsinglewidget.h"
#include "memoryhandler.h"
#include "dialogs/scenesettingsdialog.h"
#include "importhandler.h"
#include "GUI/edialogs.h"
#include "eimporters.h"
#include "../../modules/ora/oramodule.h"
#include "dialogs/exportsvgdialog.h"
#include "widgets/alignwidget.h"
#include "widgets/welcomedialog.h"
#include "Boxes/textbox.h"
#include "misc/noshortcutaction.h"
#include "efiltersettings.h"
#include "Settings/settingsdialog.h"
#include "appsupport.h"
#include "themesupport.h"
#include "RasterEffects/rastereffectmenucreator.h"
#include "RasterEffects/rastereffect.h"
#include "BlendEffects/blendeffectmenucreator.h"
#include "BlendEffects/blendeffect.h"
#include "TransformEffects/transformeffectmenucreator.h"
#include "TransformEffects/transformeffect.h"
#include "PathEffects/patheffectmenucreator.h"
#include "PathEffects/patheffect.h"
#include "texteffect.h"
#include "Animators/dynamiccomplexanimator.h"
#include "Properties/emimedata.h"

#include "widgets/assetswidget.h"
#include "dialogs/adjustscenedialog.h"
#include "dialogs/commandpalette.h"

using namespace Friction;

MainWindow *MainWindow::sInstance = nullptr;


namespace {
bool isFocusedEditorWidget(QWidget *widget)
{
    if (!widget) {
        return false;
    }

    return qobject_cast<QLineEdit*>(widget) ||
           qobject_cast<QAbstractSpinBox*>(widget) ||
           qobject_cast<QTextEdit*>(widget) ||
           qobject_cast<QPlainTextEdit*>(widget) ||
           widget->inherits("QsciScintilla");
}

QWidget *editorWidgetFromObject(QObject *object)
{
    while (object) {
        auto *widget = qobject_cast<QWidget*>(object);
        if (widget && isFocusedEditorWidget(widget)) {
            return widget;
        }
        object = object->parent();
    }
    return nullptr;
}

template<typename T>
bool removeEffectProperty(Property *property)
{
    auto *effect = enve_cast<T*>(property);
    if (!effect) {
        return false;
    }

    if (const auto parent = effect->template getParent<DynamicComplexAnimatorBase<T>>()) {
        parent->removeChild(effect->template ref<T>());
        return true;
    }
    return false;
}

bool isEffectProperty(const Property *property)
{
    return enve_cast<const TransformEffect*>(property) ||
           enve_cast<const RasterEffect*>(property) ||
           enve_cast<const BlendEffect*>(property) ||
           enve_cast<const PathEffect*>(property) ||
           enve_cast<const TextEffect*>(property);
}

Property *effectPropertyFromWidget(QWidget *widget)
{
    while (widget) {
        if (const auto row = dynamic_cast<BoxSingleWidget*>(widget)) {
            auto *property = row->targetProperty();
            if (isEffectProperty(property)) {
                return property;
            }
        }
        widget = widget->parentWidget();
    }
    return nullptr;
}

bool deleteEffectProperty(Property *property)
{
    return removeEffectProperty<TransformEffect>(property) ||
           removeEffectProperty<RasterEffect>(property) ||
           removeEffectProperty<BlendEffect>(property) ||
           removeEffectProperty<PathEffect>(property) ||
           removeEffectProperty<TextEffect>(property);
}

enum class EffectsPresetKind {
    None,
    Raster,
    Blend,
    Transform,
    Path
};

enum class EffectsPresetApplyTarget {
    Raster,
    Blend,
    Transform,
    Path,
    FillPath,
    OutlineBasePath,
    OutlinePath
};

class EffectsPresetItem : public QTreeWidgetItem {
public:
    explicit EffectsPresetItem(QTreeWidget *parent,
                               const QString &title)
        : QTreeWidgetItem(parent, QStringList(title)) {}

    explicit EffectsPresetItem(QTreeWidgetItem *parent,
                               const QString &title)
        : QTreeWidgetItem(parent, QStringList(title)) {}

    bool isEffectItem() const { return mKind != EffectsPresetKind::None; }
    EffectsPresetKind kind() const { return mKind; }
    EffectsPresetApplyTarget target() const { return mTarget; }

    void configureRaster(const EffectsPresetApplyTarget target,
                         const RasterEffectMenuCreator::EffectCreator &creator)
    {
        mKind = EffectsPresetKind::Raster;
        mTarget = target;
        mRasterCreator = creator;
        setFlags(flags() | Qt::ItemIsDragEnabled);
    }

    void configureBlend(const BlendEffectMenuCreator::EffectCreator &creator)
    {
        mKind = EffectsPresetKind::Blend;
        mTarget = EffectsPresetApplyTarget::Blend;
        mBlendCreator = creator;
        setFlags(flags() | Qt::ItemIsDragEnabled);
    }

    void configureTransform(const TransformEffectMenuCreator::EffectCreator &creator)
    {
        mKind = EffectsPresetKind::Transform;
        mTarget = EffectsPresetApplyTarget::Transform;
        mTransformCreator = creator;
        setFlags(flags() | Qt::ItemIsDragEnabled);
    }

    void configurePath(const EffectsPresetApplyTarget target,
                       const PathEffectMenuCreator::EffectCreator &creator)
    {
        mKind = EffectsPresetKind::Path;
        mTarget = target;
        mPathCreator = creator;
        setFlags(flags() | Qt::ItemIsDragEnabled);
    }

    qsptr<RasterEffect> createRasterEffect() const
    { return mRasterCreator ? mRasterCreator() : nullptr; }

    qsptr<BlendEffect> createBlendEffect() const
    { return mBlendCreator ? mBlendCreator() : nullptr; }

    qsptr<TransformEffect> createTransformEffect() const
    { return mTransformCreator ? mTransformCreator() : nullptr; }

    qsptr<PathEffect> createPathEffect() const
    { return mPathCreator ? mPathCreator() : nullptr; }

private:
    EffectsPresetKind mKind = EffectsPresetKind::None;
    EffectsPresetApplyTarget mTarget = EffectsPresetApplyTarget::Raster;
    RasterEffectMenuCreator::EffectCreator mRasterCreator;
    BlendEffectMenuCreator::EffectCreator mBlendCreator;
    TransformEffectMenuCreator::EffectCreator mTransformCreator;
    PathEffectMenuCreator::EffectCreator mPathCreator;
};

class OwnedRasterEffectMimeData final : public eMimeData {
public:
    explicit OwnedRasterEffectMimeData(const qsptr<RasterEffect> &effect)
        : eMimeData(QList<RasterEffect*>() << effect.get())
        , mEffects{effect} {}
private:
    QList<qsptr<RasterEffect>> mEffects;
};

class OwnedBlendEffectMimeData final : public eMimeData {
public:
    explicit OwnedBlendEffectMimeData(const qsptr<BlendEffect> &effect)
        : eMimeData(QList<BlendEffect*>() << effect.get())
        , mEffects{effect} {}
private:
    QList<qsptr<BlendEffect>> mEffects;
};

class OwnedTransformEffectMimeData final : public eMimeData {
public:
    explicit OwnedTransformEffectMimeData(const qsptr<TransformEffect> &effect)
        : eMimeData(QList<TransformEffect*>() << effect.get())
        , mEffects{effect} {}
private:
    QList<qsptr<TransformEffect>> mEffects;
};

class OwnedPathEffectMimeData final : public eMimeData {
public:
    explicit OwnedPathEffectMimeData(const qsptr<PathEffect> &effect)
        : eMimeData(QList<PathEffect*>() << effect.get())
        , mEffects{effect} {}
private:
    QList<qsptr<PathEffect>> mEffects;
};

class EffectsPresetsTreeWidget : public QTreeWidget {
public:
    explicit EffectsPresetsTreeWidget(QWidget *parent = nullptr)
        : QTreeWidget(parent) {}

protected:
    Qt::DropActions supportedDropActions() const override
    {
        return Qt::CopyAction;
    }

    QMimeData *mimeData(const QList<QTreeWidgetItem*> items) const override
    {
        if (items.size() != 1) {
            return nullptr;
        }

        const auto item = dynamic_cast<EffectsPresetItem*>(items.constFirst());
        if (!item || !item->isEffectItem()) {
            return nullptr;
        }

        switch (item->kind()) {
        case EffectsPresetKind::Raster: {
            const auto effect = item->createRasterEffect();
            return effect ? static_cast<QMimeData*>(new OwnedRasterEffectMimeData(effect)) : nullptr;
        }
        case EffectsPresetKind::Blend: {
            const auto effect = item->createBlendEffect();
            return effect ? static_cast<QMimeData*>(new OwnedBlendEffectMimeData(effect)) : nullptr;
        }
        case EffectsPresetKind::Transform: {
            const auto effect = item->createTransformEffect();
            return effect ? static_cast<QMimeData*>(new OwnedTransformEffectMimeData(effect)) : nullptr;
        }
        case EffectsPresetKind::Path: {
            const auto effect = item->createPathEffect();
            return effect ? static_cast<QMimeData*>(new OwnedPathEffectMimeData(effect)) : nullptr;
        }
        default:
            return nullptr;
        }
    }
};

EffectsPresetItem *ensureEffectsCategory(QTreeWidget *tree,
                                         QHash<QString, EffectsPresetItem*> &roots,
                                         const QString &rootName,
                                         const QString &subPath = QString())
{
    auto *root = roots.value(rootName, nullptr);
    if (!root) {
        root = new EffectsPresetItem(tree, rootName);
        root->setExpanded(true);
        root->setFlags(root->flags() & ~Qt::ItemIsDragEnabled);
        roots.insert(rootName, root);
    }

    auto *parent = root;
    const auto segments = subPath.split('/', Qt::SkipEmptyParts);
    for (const auto &segment : segments) {
        EffectsPresetItem *childCategory = nullptr;
        for (int i = 0; i < parent->childCount(); ++i) {
            const auto candidate = dynamic_cast<EffectsPresetItem*>(parent->child(i));
            if (candidate && !candidate->isEffectItem() && candidate->text(0) == segment) {
                childCategory = candidate;
                break;
            }
        }
        if (!childCategory) {
            childCategory = new EffectsPresetItem(parent, segment);
            childCategory->setFlags(childCategory->flags() & ~Qt::ItemIsDragEnabled);
        }
        parent = childCategory;
    }

    return parent;
}

bool filterEffectsPresetItem(QTreeWidgetItem *item,
                             const QString &trimmed)
{
    bool childVisible = false;
    for (int i = 0; i < item->childCount(); ++i) {
        childVisible = filterEffectsPresetItem(item->child(i), trimmed) || childVisible;
    }

    const bool selfVisible = trimmed.isEmpty() ||
                             item->text(0).contains(trimmed, Qt::CaseInsensitive);
    const bool visible = selfVisible || childVisible;
    item->setHidden(!visible);
    if (item->childCount() > 0) {
        item->setExpanded(trimmed.isEmpty() ? item->parent() == nullptr : visible);
    }
    return visible;
}
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    processKeyEvent(event);
}

MainWindow::MainWindow(Document& document,
                       Actions& actions,
                       AudioHandler& audioHandler,
                       RenderHandler& renderHandler,
                       const QString &openProject,
                       QWidget * const parent)
    : QMainWindow(parent)
    , mShutdown(false)
    , mWelcomeDialog(nullptr)
    , mStackWidget(nullptr)
    , mTimeline(nullptr)
    , mRenderWidget(nullptr)
    , mToolbar(nullptr)
    , mToolBox(nullptr)
    , mUI(nullptr)
    , mSaveAct(nullptr)
    , mSaveAsAct(nullptr)
    , mSaveBackAct(nullptr)
    , mPreviewSVGAct(nullptr)
    , mExportSVGAct(nullptr)
    , mRenderVideoAct(nullptr)
    , mCloseProjectAct(nullptr)
    , mLinkedAct(nullptr)
    , mImportAct(nullptr)
    , mImportSeqAct(nullptr)
    , mRevertAct(nullptr)
    , mSelectAllAct(nullptr)
    , mInvertSelAct(nullptr)
    , mClearSelAct(nullptr)
    , mAddKeyAct(nullptr)
    , mAddToQueAct(nullptr)
    , mViewFullScreenAct(nullptr)
    , mFontWidget(nullptr)
    , mFontWidgetAct(nullptr)
    , mDocument(document)
    , mActions(actions)
    , mAudioHandler(audioHandler)
    , mRenderHandler(renderHandler)
    , mLayoutHandler(nullptr)
    , mFillStrokeSettings(nullptr)
    , mChangedSinceSaving(false)
    , mEventFilterDisabled(false)
    , mGrayOutWidget(nullptr)
    , mDisplayedFillStrokeSettingsUpdateNeeded(false)
    , mObjectSettingsWidget(nullptr)
    , mObjectSettingsScrollArea(nullptr)
    , mProjectWidget(nullptr)
    , mPropertiesPanel(nullptr)
    , mCenterTabs(nullptr)
    , mBottomTabs(nullptr)
    , mStackIndexScene(0)
    , mStackIndexWelcome(0)
    , mTabColorIndex(0)
    , mTabAssetsIndex(0)
    , mTabQueueIndex(0)
    , mColorToolBar(nullptr)
    , mCanvasToolBar(nullptr)
    , mBackupOnSave(false)
    , mAutoSave(false)
    , mAutoSaveTimeout(0)
    , mAutoSaveTimer(nullptr)
    , mAboutWidget(nullptr)
    , mAboutWindow(nullptr)
    , mViewTimelineAct(nullptr)
    , mTimelineWindow(nullptr)
    , mTimelineWindowAct(nullptr)
    , mViewFillStrokeAct(nullptr)
    , mRenderWindow(nullptr)
    , mRenderWindowAct(nullptr)
    , mToolBarMainAct(nullptr)
    , mToolBarColorAct(nullptr)
{
    Q_ASSERT(!sInstance);
    sInstance = this;

    setWindowIcon(QIcon::fromTheme(AppSupport::getAppID()));
    setContextMenuPolicy(Qt::NoContextMenu);

    setupImporters();
    setupDocument();
    setupAutoSave();
    mAeShortcutController = new AeShortcutController(*this, mDocument, mActions);

    setupMainWidgets();
    setupMemoryWidgets();
    setupPropertiesWidgets();

    setupToolBar();
    setupMenuBar();

    setupStackWidgets();

    readRecentFiles();
    updateRecentMenu();

    installEventFilter(this);
    qApp->installEventFilter(this);

    setupLayout();
    readSettings(openProject);
}

MainWindow::~MainWindow()
{
    mShutdown = true;
    if (qApp) {
        qApp->removeEventFilter(this);
    }
    std::cout << "Closing VECB, please wait ... " << std::endl;
    if (mAutoSaveTimer->isActive()) { mAutoSaveTimer->stop(); }
    writeSettings();
    sInstance = nullptr;
}




BoundingBox *MainWindow::getCurrentBox()
{
    const auto scene = *mDocument.fActiveScene;
    if (!scene) { return nullptr; }

    const auto box = scene->getCurrentBox();
    if (!box) { return nullptr; }

    return box;
}

void MainWindow::checkAutoSaveTimer()
{
    if (mShutdown) { return; }

    if (mAutoSave &&
        mChangedSinceSaving &&
        !mDocument.fEvFile.isEmpty())
    {
        const int projectVersion = AppSupport::getProjectVersion(mDocument.fEvFile);
        const int newProjectVersion = AppSupport::getProjectVersion();
        if (newProjectVersion > projectVersion && projectVersion > 0) {
            QMessageBox::warning(this,
                                 tr("Auto Save canceled"),
                                 tr("Auto Save is not allowed to break"
                                    " project format compatibility (%1 vs. %2)."
                                    " Please save the project to confirm"
                                    " project format changes.").arg(QString::number(newProjectVersion),
                                                                    QString::number(projectVersion)));
            return;
        }
        saveFile(mDocument.fEvFile);
    }
}

void MainWindow::openAboutWindow()
{
    if (!mAboutWidget) {
        mAboutWidget = new AboutWidget(this);
    }
    if (!mAboutWindow) {
        mAboutWindow = new Window(this,
                                  mAboutWidget,
                                  tr("About"),
                                  QString("AboutWindow"),
                                  false,
                                  false);
        mAboutWindow->setMinimumSize(640, 480);
    }
    mAboutWindow->focusWindow();
}

void MainWindow::openTimelineWindow()
{
    AppSupport::setSettings("ui",
                            "TimelineWindow",
                            true);
    if (!mTimelineWindow) {
        mTimelineWindow = new Window(this,
                                     mTimeline,
                                     tr("Layers"),
                                     QString("TimelineWindow"),
                                     true,
                                     true,
                                     true);
        connect(mTimelineWindow, &Window::closed,
                this, [this]() { closedTimelineWindow(); });
    } else {
        mTimelineWindow->addWidget(mTimeline);
    }
    if (mBottomTabs) {
        const auto pages = mBottomSceneTabs;
        for (auto it = pages.constBegin(); it != pages.constEnd(); ++it) {
            if (mTimeline->parentWidget() == it.value()) {
                mTimeline->setParent(nullptr);
            }
            const int idx = mBottomTabs->indexOf(it.value());
            if (idx >= 0) { mBottomTabs->removeTab(idx); }
            delete it.value();
        }
        mBottomSceneTabs.clear();
    }
    mTimelineWindowAct->setChecked(true);
    mTimelineWindow->focusWindow();
}

void MainWindow::closedTimelineWindow()
{
    if (mShutdown) { return; }
    AppSupport::setSettings("ui",
                            "TimelineWindow",
                            false);
    mTimelineWindowAct->setChecked(false);
    if (mBottomTabs) {
        for (const auto &scenePtr : mSceneNavigationChain) {
            ensureBottomSceneTab(scenePtr);
        }
        ensureBottomSceneTab(*mDocument.fActiveScene);
        selectBottomSceneTab(*mDocument.fActiveScene);
    }
    updateWorkspaceTabTitles();
}

void MainWindow::openRenderQueueWindow()
{
    AppSupport::setSettings("ui",
                            "RenderWindow",
                            true);
    mRenderWindowAct->setChecked(true);
    if (mBottomTabs) {
        const int idx = mBottomTabs->indexOf(mRenderWidget);
        if (idx >= 0) { mBottomTabs->removeTab(idx); }
    }
    mRenderWidget->setVisible(true);
    if (!mRenderWindow) {
        mRenderWindow = new Window(this,
                                   mRenderWidget,
                                   tr("Renderer"),
                                   QString("RenderWindow"),
                                   true,
                                   true,
                                   false);
        connect(mRenderWindow, &Window::closed,
                this, [this]() { closedRenderQueueWindow(); });
    } else {
        mRenderWindow->addWidget(mRenderWidget);
    }
    mRenderWindow->focusWindow();
}

void MainWindow::closedRenderQueueWindow()
{
    if (mShutdown) { return; }
    AppSupport::setSettings("ui",
                            "RenderWindow",
                            false);
    mRenderWindowAct->setChecked(false);
    if (mBottomTabs && mBottomTabs->indexOf(mRenderWidget) < 0) {
        mTabQueueIndex = mBottomTabs->addTab(mRenderWidget,
                                             QIcon::fromTheme("render_animation"),
                                             tr("Render Queue"));
        if (auto *tabBar = mBottomTabs->tabBar()) {
            tabBar->setTabButton(mTabQueueIndex, QTabBar::RightSide, nullptr);
            tabBar->setTabButton(mTabQueueIndex, QTabBar::LeftSide, nullptr);
        }
    }
}

void MainWindow::initRenderPresets(const bool reinstall)
{
    const bool doInstall = reinstall ? true : AppSupport::getSettings("settings",
                                                                      "firstRunRenderPresets",
                                                                      true).toBool();
    if (!doInstall) { return; }
    const QString path = AppSupport::getAppOutputProfilesPath();
    if (path.isEmpty() || !QFileInfo(path).isWritable()) { return; }

    QStringList presets;
    presets << "001-friction-preset-mp4-h264.conf";
    presets << "002-friction-preset-mp4-h264-mp3.conf";
    presets << "003-friction-preset-prores-444.conf";
    presets << "004-friction-preset-prores-444-aac.conf";
    presets << "005-friction-preset-png.conf";
    presets << "006-friction-preset-tiff.conf";

    for (const auto &preset : presets) {
        QString filePath(QString("%1/%2").arg(path, preset));
        if (QFile::exists(filePath) && !reinstall) { continue; }
        QFile file(filePath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            QFile res(QString(":/presets/render/%1").arg(preset));
            if (res.open(QIODevice::ReadOnly | QIODevice::Text)) {
                file.write(res.readAll());
                res.close();
            }
            file.close();
        }
    }

    AppSupport::setSettings("settings", "firstRunRenderPresets", false);
}

void MainWindow::askInstallRenderPresets()
{
    const auto result = QMessageBox::question(this,
                                              tr("Install Render Profiles"),
                                              tr("Are you sure you want to install the default render profiles?"
                                                 "<p style='font-weight: normal;font-style: italic'>Note that a restart of the application is required to detect new profiles.</p>"));
    if (result != QMessageBox::Yes) { return; }
    initRenderPresets(true);
}

void MainWindow::askInstallExpressionsPresets()
{
    const auto result = QMessageBox::question(this,
                                              tr("Install default Expressions Presets"),
                                              tr("Are you sure you want to install the default Expressions Presets?"
                                                 "<p style='font-weight: normal;font-style: italic'>Note that:<ul><li>any user modification to default presets will be removed.</li><li>a restart of the application is required to install them all.</li></ul></p>"));
    if (result != QMessageBox::Yes) { return; }
    AppSupport::setSettings("settings", "firstRunExprPresets", true);
}

void MainWindow::askRestoreFillStrokeDefault()
{
    const auto result = QMessageBox::question(this,
                                              tr("Restore default fill and stroke?"),
                                              tr("Are you sure you want to restore fill and stroke defaults?"));
    if (result != QMessageBox::Yes) { return; }

    auto settings = eSettings::sInstance;
    settings->fLastFillFlatEnabled = false;
    settings->fLastStrokeFlatEnabled = true;
    settings->fLastUsedFillColor = Qt::white;
    settings->fLastUsedStrokeColor = ThemeSupport::getThemeObjectColor();
    settings->fLastUsedStrokeWidth = 10.;
}

void MainWindow::askRestoreDefaultUi()
{
    const auto result = QMessageBox::question(this,
                                              tr("Restore default user interface?"),
                                              tr("Are you sure you want to restore default user interface? "
                                                 "You must restart VECB to apply."));
    if (result != QMessageBox::Yes) { return; }
    eSettings::sInstance->fRestoreDefaultUi = true;
}

void MainWindow::openWelcomeDialog()
{
    mStackWidget->setCurrentIndex(mStackIndexWelcome);
}

void MainWindow::closeWelcomeDialog()
{
    mStackWidget->setCurrentIndex(mStackIndexScene);
}

void MainWindow::addCanvasToRenderQue()
{
    if (!mDocument.fActiveScene) { return; }
    if (mRenderWindowAct->isChecked()) { openRenderQueueWindow(); }
    else if (mBottomTabs) { mBottomTabs->setCurrentWidget(mRenderWidget); }
    mRenderWidget->createNewRenderInstanceWidgetForCanvas(mDocument.fActiveScene);
}

void MainWindow::updateSettingsForCurrentCanvas(Canvas* const scene)
{
    if (mColorToolBar) { mColorToolBar->setCurrentCanvas(scene); }
    if (mCanvasToolBar) { mCanvasToolBar->setCurrentCanvas(scene); }

    mObjectSettingsWidget->setCurrentScene(scene);

    if (mPreviewSVGAct) { mPreviewSVGAct->setEnabled(scene); }
    if (mExportSVGAct) { mExportSVGAct->setEnabled(scene); }
    if (mSaveAct) { mSaveAct->setEnabled(scene); }
    if (mSaveAsAct) { mSaveAsAct->setEnabled(scene); }
    if (mSaveBackAct) { mSaveBackAct->setEnabled(scene); }
    if (mAddToQueAct) { mAddToQueAct->setEnabled(scene); }
    if (mRenderVideoAct) { mRenderVideoAct->setEnabled(scene); }
    if (mCloseProjectAct) { mCloseProjectAct->setEnabled(scene); }
    if (mLinkedAct) { mLinkedAct->setEnabled(scene); }
    if (mImportAct) { mImportAct->setEnabled(scene); }
    if (mImportSeqAct) { mImportSeqAct->setEnabled(scene); }
    if (mRevertAct) { mRevertAct->setEnabled(scene); }
    if (mSelectAllAct) { mSelectAllAct->setEnabled(scene); }
    if (mInvertSelAct) { mInvertSelAct->setEnabled(scene); }
    if (mClearSelAct) { mClearSelAct->setEnabled(scene); }
    if (mAddKeyAct) { mAddKeyAct->setEnabled(scene); }
    if (mEffectsMenu) { mEffectsMenu->setEnabled(scene); }

    if (!scene) {
        mObjectSettingsWidget->setMainTarget(nullptr);
        mTimeline->updateSettingsForCurrentCanvas(nullptr);
        return;
    }

    mClipViewToCanvas->blockSignals(true);
    mClipViewToCanvas->setChecked(scene->clipToCanvas());
    mClipViewToCanvas->blockSignals(false);

    mRasterEffectsVisible->blockSignals(true);
    mRasterEffectsVisible->setChecked(scene->getRasterEffectsVisible());
    mRasterEffectsVisible->blockSignals(false);

    mPathEffectsVisible->blockSignals(true);
    mPathEffectsVisible->setChecked(scene->getPathEffectsVisible());
    mPathEffectsVisible->blockSignals(false);

    mTimeline->updateSettingsForCurrentCanvas(scene);
    mObjectSettingsWidget->setMainTarget(scene->getCurrentBox());
}

void MainWindow::setupToolBar()
{
    mToolBox = new Ui::ToolBox(mActions, mDocument, this);
    if (const auto controlsToolbar = mToolBox->getToolBar(Ui::ToolBox::Controls)) {
        controlsToolbar->hide();
    }
    {
        const auto toolbar = mToolBox->getToolBar(Ui::ToolBox::Main);
        if (toolbar) {
            toolbar->setAllowedAreas(Qt::AllToolBarAreas);
            toolbar->setMovable(true);
            toolbar->setFloatable(true);
            addToolBar(Qt::TopToolBarArea, toolbar);
        }
    }

    mColorToolBar = new Ui::ColorToolBar(mDocument, this);
    connect(mColorToolBar, &Ui::ColorToolBar::message,
            this, [this](const QString &msg){ statusBar()->showMessage(msg, 500); });
    mColorToolBar->setAllowedAreas(Qt::AllToolBarAreas);
    mColorToolBar->setMovable(true);
    mColorToolBar->setFloatable(true);
    addToolBar(Qt::TopToolBarArea, mColorToolBar);

    mToolbar = new Ui::ToolBar(tr("Toolbar"),
                               "MainToolBar",
                               this);
    mToolbar->setAllowedAreas(Qt::AllToolBarAreas);
    mToolbar->setMovable(true);
    mToolbar->setFloatable(true);
    addToolBar(Qt::TopToolBarArea, mToolbar);

    mCanvasToolBar = new Ui::CanvasToolBar(this);

    mCanvasToolBar->addSeparator();
    mCanvasToolBar->addAction(QIcon::fromTheme("workspace"),
                              tr("Layout"));
    const auto workspaceLayoutCombo = mLayoutHandler->comboWidget();
    workspaceLayoutCombo->setMaximumWidth(150);
    mCanvasToolBar->addWidget(workspaceLayoutCombo);
    mCanvasToolBar->setAllowedAreas(Qt::AllToolBarAreas);
    mCanvasToolBar->setMovable(true);
    mCanvasToolBar->setFloatable(true);
    addToolBar(Qt::BottomToolBarArea, mCanvasToolBar);

    {
        const auto toolbar = mToolBox->getToolBar(Ui::ToolBox::Interact);
        if (toolbar) {
            toolbar->setAllowedAreas(Qt::AllToolBarAreas);
            toolbar->setMovable(true);
            toolbar->setFloatable(true);
            addToolBar(Qt::BottomToolBarArea, toolbar);
        }
    }

    connect(&mAudioHandler, &AudioHandler::deviceChanged,
            this, [this]() {
        statusBar()->showMessage(tr("Changed audio output: %1").arg(mAudioHandler.getDeviceName()),
                                 10000);
    });
}

MainWindow *MainWindow::sGetInstance()
{
    return sInstance;
}

void MainWindow::setupDocument()
{
    // setup connections
    connect(&mDocument, &Document::evFilePathChanged,
            this, &MainWindow::updateTitle);
    connect(&mDocument, &Document::activeSceneSet,
            this, &MainWindow::updateSettingsForCurrentCanvas);
    connect(&mDocument, &Document::currentBoxChanged,
            this, &MainWindow::setCurrentBox);
    connect(&mDocument, &Document::canvasModeSet,
            this, &MainWindow::updateCanvasModeButtonsChecked);
    connect(&mDocument, &Document::sceneCreated,
            this, &MainWindow::closeWelcomeDialog);
    connect(&mDocument, &Document::openTextEditor,
            this, &MainWindow::openCurrentTextEditorPopup);
    connect(&mDocument, &Document::openMarkerEditor,
            this, &MainWindow::openMarkerEditor);
    connect(&mDocument, &Document::openExpressionDialog,
            this, &MainWindow::openExpressionDialog);
    connect(&mDocument, &Document::openApplyExpressionDialog,
            this, &MainWindow::openApplyExpressionDialog);
    connect(&mDocument, &Document::newVideo,
            this, &MainWindow::handleNewVideoClip);
    connect(&mDocument, &Document::documentChanged,
            this, [this]() {
        setFileChangedSinceSaving(true);
        if (mTimeline) { mTimeline->stopPreview(); }
    });

    // set defaults
    mDocument.setPath("");
    mDocument.fDrawPathManual = false;
    mDocument.setCanvasMode(CanvasMode::boxTransform);
}

void MainWindow::setupImporters()
{
    //ImportHandler::sInstance->addImporter<eXevImporter>();
    ImportHandler::sInstance->addImporter<evImporter>();

    ImportHandler::sInstance->addImporter<eSvgImporter>();
    ImportHandler::sInstance->addImporter<eOraImporter>();
    ImportHandler::sInstance->addImporter<eGltfImporter>();
}

void MainWindow::setupAutoSave()
{
    mAutoSaveTimer = new QTimer(this);
    connect (mAutoSaveTimer, &QTimer::timeout,
            this, &MainWindow::checkAutoSaveTimer);
}

void MainWindow::updateCanvasModeButtonsChecked()
{
    // const CanvasMode mode = mDocument.fCanvasMode;
    // keep around in case we need to trigger something
}

void MainWindow::setResolutionValue(const qreal value)
{
    if (!mDocument.fActiveScene) { return; }
    mDocument.fActiveScene->setResolution(value);
    mDocument.actionFinished();
}

void MainWindow::setFileChangedSinceSaving(const bool changed)
{
    if (changed == mChangedSinceSaving) { return; }
    mChangedSinceSaving = changed;
    updateTitle();
}

SimpleBrushWrapper *MainWindow::getCurrentBrush() const
{
    return nullptr; //mBrushSelectionWidget->getCurrentBrush();
}

void MainWindow::setCurrentBox(BoundingBox *box)
{
    mColorToolBar->setCurrentBox(box);
    mFillStrokeSettings->setCurrentBox(box);
    mFontWidget->setCurrentBox(box);
    if (mObjectSettingsWidget) {
        mObjectSettingsWidget->setMainTarget(box);
    }
    setCurrentBoxFocus(box);
}

void MainWindow::setCurrentBoxFocus(BoundingBox *box)
{
    if (!box) { return; }
    if (enve_cast<TextBox*>(box)) {
        focusFontWidget(mDocument.fCanvasMode == CanvasMode::textCreate);
    } else {
        focusColorWidget();
    }
}

FillStrokeSettingsWidget *MainWindow::getFillStrokeSettings()
{
    return mFillStrokeSettings;
}

bool MainWindow::askForSaving() {
    if (mChangedSinceSaving) {
        const QString title = tr("Save", "AskSaveDialog_Title");
        QFileInfo info(mDocument.fEvFile);
        QString file = info.baseName();
        if (file.isEmpty()) { file = tr("Untitled"); }

        const QString question = tr("Save changes to document \"%1\"?",
                                    "AskSaveDialog_Question");
        const QString questionWithTarget = question.arg(file);
        const QString closeNoSave =  tr("Close without saving",
                                        "AskSaveDialog_Button");
        const QString cancel = tr("Cancel", "AskSaveDialog_Button");
        const QString save = tr("Save", "AskSaveDialog_Button");
        const int buttonId = QMessageBox::question(
                    this, title, questionWithTarget,
                    closeNoSave, cancel, save);
        if (buttonId == 1) {
            return false;
        } else if (buttonId == 2) {
            saveFile();
            return true;
        }
    }
    return true;
}

BoxScrollWidget *MainWindow::getObjectSettingsList()
{
    return mObjectSettingsWidget;
}

void MainWindow::disableEventFilter()
{
    mEventFilterDisabled = true;
}

void MainWindow::enableEventFilter()
{
    mEventFilterDisabled = false;
}

void MainWindow::disable()
{
    disableEventFilter();
    mGrayOutWidget = new QWidget(this);
    mGrayOutWidget->setFixedSize(size());
   // mGrayOutWidget->setStyleSheet(
     //           "QWidget { background-color: rgba(0, 0, 0, 125) }");
    mGrayOutWidget->show();
    mGrayOutWidget->update();
}

void MainWindow::enable()
{
    if (!mGrayOutWidget) { return; }
    enableEventFilter();
    delete mGrayOutWidget;
    mGrayOutWidget = nullptr;
    mDocument.actionFinished();
}

void MainWindow::newFile()
{
    if (mChangedSinceSaving || !mDocument.fEvFile.isEmpty()) {
        const int ask = QMessageBox::question(this,
                                              tr("New Project"),
                                              tr("Are you sure you want to create a new project?"));
        if (ask == QMessageBox::No) { return; }
    }
    if (closeProject()) {
        SceneSettingsDialog::sNewSceneDialog(mDocument, this);
    }
}

bool MainWindow::eventFilter(QObject *obj, QEvent *e)
{
    if (mLock) { if (dynamic_cast<QInputEvent*>(e)) { return true; } }
    if (mEventFilterDisabled) { return QMainWindow::eventFilter(obj, e); }
    const auto type = e->type();
    const auto focusWidget = QApplication::focusWidget();
    const bool focusedEditor = isFocusedEditorWidget(focusWidget);
    const bool eventOnEditor = editorWidgetFromObject(obj) != nullptr;
    if (type == QEvent::KeyPress) {
        if (focusedEditor || eventOnEditor) {
            return QMainWindow::eventFilter(obj, e);
        }
        const auto keyEvent = static_cast<QKeyEvent*>(e);
        const bool effectsDeleteContext =
                isTimelineInputContext() || isEffectControlsInputContext();
        if (keyEvent->key() == Qt::Key_Delete &&
            effectsDeleteContext &&
            deleteSelectedEffectProperties()) {
            return true;
        }
        if (isTimelineInputContext() && mTimeline &&
            mTimeline->processKeyPress(keyEvent)) {
            return true;
        }
        if (keyEvent->key() == Qt::Key_Delete && focusWidget) {
            mEventFilterDisabled = true;
            const bool widHandled =
                    QCoreApplication::sendEvent(focusWidget, keyEvent);
            mEventFilterDisabled = false;
            if (widHandled) { return false; }
        }
        return processKeyEvent(keyEvent);
    } else if (type == QEvent::ShortcutOverride) {
        if (focusedEditor || eventOnEditor) {
            return QMainWindow::eventFilter(obj, e);
        }
        const auto keyEvent = static_cast<QKeyEvent*>(e);
        const int key = keyEvent->key();
        if (key == Qt::Key_Tab) {
            if (enve_cast<QLineEdit*>(focusWidget)) { return true; }
            return true;
        }
        //if (handleCanvasModeKeyPress(mDocument, key)) { return true; }
        if (keyEvent->modifiers() == Qt::ShiftModifier && key == Qt::Key_D) {
            return processKeyEvent(keyEvent);
        }
        if (keyEvent->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier) &&
            key == Qt::Key_D) {
            return processKeyEvent(keyEvent);
        }
        if (isTimelineInputContext()) {
            const bool plainTimelineKey =
                    !(keyEvent->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) &&
                    (key == Qt::Key_A || key == Qt::Key_U || key == Qt::Key_T ||
                     key == Qt::Key_B || key == Qt::Key_N);
            const bool layerOrderTimelineKey =
                    keyEvent->modifiers() == Qt::ControlModifier &&
                    (key == Qt::Key_BracketLeft || key == Qt::Key_BracketRight);
            const bool easeTimelineKey =
                    keyEvent->modifiers() == Qt::ControlModifier &&
                    key == Qt::Key_E;
            if (plainTimelineKey || easeTimelineKey || layerOrderTimelineKey) {
                return true;
            }
        }
        if (keyEvent->modifiers() == Qt::ControlModifier &&
            (key == Qt::Key_C || key == Qt::Key_V ||
             key == Qt::Key_X || key == Qt::Key_D)) {
            return processKeyEvent(keyEvent);
        } else if (key == Qt::Key_A || key == Qt::Key_I ||
                   key == Qt::Key_Delete) {
              return processKeyEvent(keyEvent);
        }
    } else if (type == QEvent::KeyRelease) {
        if (focusedEditor || eventOnEditor) {
            return QMainWindow::eventFilter(obj, e);
        }
        const auto keyEvent = static_cast<QKeyEvent*>(e);
        if (processKeyEvent(keyEvent)) { return true; }
        //finishUndoRedoSet();
    } else if (type == QEvent::MouseButtonRelease) {
        //finishUndoRedoSet();
    }
    return QMainWindow::eventFilter(obj, e);
}

bool MainWindow::deleteSelectedEffectProperties()
{
    Canvas *scene = mDocument.fActiveScene;
    if (!scene) {
        return false;
    }

    const auto deleteCurrentEffect = [](QWidget *widget) {
        return deleteEffectProperty(effectPropertyFromWidget(widget));
    };

    if (deleteCurrentEffect(QApplication::focusWidget()) ||
        deleteCurrentEffect(QApplication::widgetAt(QCursor::pos()))) {
        mDocument.actionFinished();
        return true;
    }

    bool deleted = false;
    scene->execOpOnSelectedProperties<TransformEffect>(
                [&deleted](TransformEffect *effect) {
        if (!effect) { return; }
        deleted = deleteEffectProperty(effect) || deleted;
    });
    scene->execOpOnSelectedProperties<RasterEffect>(
                [&deleted](RasterEffect *effect) {
        if (!effect) { return; }
        deleted = deleteEffectProperty(effect) || deleted;
    });
    scene->execOpOnSelectedProperties<BlendEffect>(
                [&deleted](BlendEffect *effect) {
        if (!effect) { return; }
        deleted = deleteEffectProperty(effect) || deleted;
    });
    scene->execOpOnSelectedProperties<PathEffect>(
                [&deleted](PathEffect *effect) {
        if (!effect) { return; }
        deleted = deleteEffectProperty(effect) || deleted;
    });
    scene->execOpOnSelectedProperties<TextEffect>(
                [&deleted](TextEffect *effect) {
        if (!effect) { return; }
        deleted = deleteEffectProperty(effect) || deleted;
    });

    if (deleted) {
        mDocument.actionFinished();
    }
    return deleted;
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    if (!closeProject()) { e->ignore(); }
    else { mShutdown = true; }
}

bool MainWindow::processKeyEvent(QKeyEvent *event)
{
    if (isActiveWindow() || (mTimelineWindow && mTimelineWindow->isActiveWindow())) {
        if ((event->type() == QEvent::KeyPress ||
             event->type() == QEvent::ShortcutOverride ||
             event->type() == QEvent::KeyRelease) &&
            isFocusedEditorWidget(QApplication::focusWidget())) {
            return false;
        }
        bool returnBool = false;
        if (event->type() == QEvent::KeyPress ||
            event->type() == QEvent::ShortcutOverride) {
            const bool timelineContext = isTimelineInputContext();
            const bool plainLetter =
                    !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) &&
                    event->key() >= Qt::Key_A && event->key() <= Qt::Key_Z;

            if (mAeShortcutController &&
                mAeShortcutController->process(event)) {
                returnBool = true;
            } else if (timelineContext && mTimeline && mTimeline->processKeyPress(event)) {
                returnBool = true;
            } else if (timelineContext && plainLetter) {
                // When the timeline is active, don't leak single-letter keys
                // into the viewer/tool shortcut stack.
                returnBool = true;
            } else {
                returnBool = KeyFocusTarget::KFT_handleKeyEvent(event);
            }
        } else {
            returnBool = KeyFocusTarget::KFT_handleKeyEvent(event);
        }
        mDocument.actionFinished();
        return returnBool;
    }
    return false;
}

bool MainWindow::isTimelineInputContext() const
{
    if (!mTimeline) { return false; }

    const auto belongsToTimeline = [this](QWidget *widget) {
        while (widget) {
            if (widget == mTimeline) { return true; }
            widget = widget->parentWidget();
        }
        return false;
    };

    auto *focusWidget = QApplication::focusWidget();
    if (isFocusedEditorWidget(focusWidget)) {
        return false;
    }
    if (belongsToTimeline(focusWidget)) {
        return true;
    }

    auto *hoveredWidget = QApplication::widgetAt(QCursor::pos());
    if (hoveredWidget && isFocusedEditorWidget(hoveredWidget)) {
        return false;
    }
    if (belongsToTimeline(hoveredWidget)) {
        return true;
    }

    const QRect timelineGlobalRect(mTimeline->mapToGlobal(QPoint(0, 0)),
                                   mTimeline->size());
    if (timelineGlobalRect.contains(QCursor::pos())) {
        return true;
    }

    return mTimeline->underMouse();
}

bool MainWindow::isEffectControlsInputContext() const
{
    if (!mPropertiesPanel && !mObjectSettingsWidget && !mObjectSettingsScrollArea) {
        return false;
    }

    const auto belongsToEffectControls = [this](QWidget *widget) {
        while (widget) {
            if (widget == mPropertiesPanel ||
                widget == mObjectSettingsWidget ||
                widget == mObjectSettingsScrollArea) {
                return true;
            }
            widget = widget->parentWidget();
        }
        return false;
    };

    auto *focusWidget = QApplication::focusWidget();
    if (isFocusedEditorWidget(focusWidget)) {
        return false;
    }
    if (belongsToEffectControls(focusWidget)) {
        return true;
    }

    auto *hoveredWidget = QApplication::widgetAt(QCursor::pos());
    if (hoveredWidget && isFocusedEditorWidget(hoveredWidget)) {
        return false;
    }
    if (belongsToEffectControls(hoveredWidget)) {
        return true;
    }

    if (mPropertiesPanel) {
        const QRect panelGlobalRect(mPropertiesPanel->mapToGlobal(QPoint(0, 0)),
                                    mPropertiesPanel->size());
        if (panelGlobalRect.contains(QCursor::pos())) {
            return true;
        }
        if (mPropertiesPanel->underMouse()) {
            return true;
        }
    }

    return false;
}

#ifdef Q_OS_MAC
bool MainWindow::processBoxesListKeyEvent(QKeyEvent *event)
{
    if (event->type() == QEvent::ShortcutOverride) { return false; }
    const auto mods = event->modifiers();
    const bool ctrlOnly = mods == Qt::ControlModifier;
    if (ctrlOnly && event->key() == Qt::Key_V) {
        if (event->isAutoRepeat()) { return false; }
        (*mActions.pasteAction)();
    } else if (ctrlOnly && event->key() == Qt::Key_C) {
        if (event->isAutoRepeat()) { return false; }
        (*mActions.copyAction)();
    } else if (ctrlOnly && event->key() == Qt::Key_D) {
        if (event->isAutoRepeat()) { return false; }
        (*mActions.duplicateAction)();
    } else if (ctrlOnly && event->key() == Qt::Key_X) {
        if (event->isAutoRepeat()) { return false; }
        (*mActions.cutAction)();
    } else if (event->key() == Qt::Key_Delete) {
        (*mActions.deleteAction)();
    } else { return false; }
    return true;
}
#endif

void MainWindow::readSettings(const QString &openProject)
{
    mUI->readSettings();
    const bool aeWorkspaceApplied = AppSupport::getSettings("ui",
                                                            "AeWorkspaceV3Applied",
                                                            false).toBool();
    if (!aeWorkspaceApplied) {
        mUI->applyAeDefaultWorkspace();
        if (mCenterTabs) { mCenterTabs->setCurrentIndex(0); }
        if (*mDocument.fActiveScene) { selectBottomSceneTab(*mDocument.fActiveScene); }
        AppSupport::setSettings("ui", "AeWorkspaceV3Applied", true);
    }
    restoreState(AppSupport::getSettings("ui",
                                         "state").toByteArray());
    restoreGeometry(AppSupport::getSettings("ui",
                                            "geometry").toByteArray());

    bool isMax = AppSupport::getSettings("ui",
                                         "maximized",
                                         false).toBool();
    bool isFull = AppSupport::getSettings("ui",
                                          "fullScreen",
                                          false).toBool();
    bool isTimelineWindow = AppSupport::getSettings("ui",
                                                    "TimelineWindow",
                                                    false).toBool();
    bool isRenderWindow = AppSupport::getSettings("ui",
                                                  "RenderWindow",
                                                  false).toBool();

    const bool visibleToolBarMain = AppSupport::getSettings("ui",
                                                            "ToolBarMainVisible",
                                                            false).toBool();
    bool visibleToolBarColor = AppSupport::getSettings("ui",
                                                       "ToolBarColorVisible",
                                                       true).toBool();
    const bool aeToolbarV1Pinned = AppSupport::getSettings("ui",
                                                           "AeToolbarV1Pinned",
                                                           false).toBool();
    if (!aeToolbarV1Pinned) {
        visibleToolBarColor = true;
        AppSupport::setSettings("ui", "ToolBarColorVisible", true);
        AppSupport::setSettings("ui", "AeToolbarV1Pinned", true);
    }

    const bool visibleFillStroke = AppSupport::getSettings("ui",
                                                           "FillStrokeVisible",
                                                           true).toBool();

    if (mToolBox) {
        if (const auto toolboxBar = mToolBox->getToolBar(Ui::ToolBox::Main)) {
            toolboxBar->setVisible(true);
        }
        if (const auto controlsToolbar = mToolBox->getToolBar(Ui::ToolBox::Controls)) {
            controlsToolbar->hide();
        }
    }
    if (menuBar()) {
        menuBar()->raise();
        menuBar()->updateGeometry();
    }

    mToolBarMainAct->setChecked(visibleToolBarMain);
    mToolBarColorAct->setChecked(visibleToolBarColor);
    mToolbar->setVisible(visibleToolBarMain);
    mColorToolBar->setVisible(visibleToolBarColor);

    mViewFillStrokeAct->setChecked(visibleFillStroke);
    mUI->setDockVisible("Project", true);
    mUI->setDockVisible("Effect Controls", visibleFillStroke);
    mUI->setDockVisible("Effect Presets", true);
    mUI->setDockVisible("Character", true);
    mUI->setDockVisible("Align", true);
    mUI->setDockVisible("Composition", true);
    mUI->setDockVisible("Layers", true);

#ifdef Q_OS_LINUX
    if (AppSupport::isWayland()) { // Disable fullscreen on wayland
        isFull = false;
        mViewFullScreenAct->setEnabled(false);
    }
#endif

    mViewFullScreenAct->blockSignals(true);
    mViewFullScreenAct->setChecked(isFull);
    mViewFullScreenAct->blockSignals(false);

    mTimelineWindowAct->blockSignals(true);
    mTimelineWindowAct->setChecked(isTimelineWindow);
    mTimelineWindowAct->blockSignals(false);

    mRenderWindowAct->blockSignals(true);
    mRenderWindowAct->setChecked(isRenderWindow);
    mRenderWindowAct->blockSignals(false);

    if (isTimelineWindow) { openTimelineWindow(); }
    if (isRenderWindow) { openRenderQueueWindow(); }

    syncPanelsMenuState();

    if (isFull) { showFullScreen(); }
    else if (isMax) { showMaximized(); }

    updateAutoSaveBackupState();

    initRenderPresets();

    if (!openProject.isEmpty()) {
        QTimer::singleShot(10,
                           this,
                           [this,
                           openProject]() { openFile(openProject); });
    } else { openWelcomeDialog(); }
}

void MainWindow::writeSettings()
{
    if (eSettings::instance().fRestoreDefaultUi) {
        AppSupport::clearSettings("ui");
    } else {
        AppSupport::setSettings("ui", "state", saveState());
        AppSupport::setSettings("ui", "geometry", saveGeometry());
        AppSupport::setSettings("ui", "maximized", isMaximized());
        AppSupport::setSettings("ui", "fullScreen", isFullScreen());
    }

    AppSupport::setSettings("FillStroke", "LastStrokeColor",
                            eSettings::instance().fLastUsedStrokeColor);
    AppSupport::setSettings("FillStroke", "LastStrokeWidth",
                            eSettings::instance().fLastUsedStrokeWidth);
    AppSupport::setSettings("FillStroke", "LastStrokeFlat",
                            eSettings::instance().fLastStrokeFlatEnabled);

    AppSupport::setSettings("FillStroke", "LastFillColor",
                            eSettings::instance().fLastUsedFillColor);
    AppSupport::setSettings("FillStroke", "LastFillFlat",
                            eSettings::instance().fLastFillFlatEnabled);
}

bool MainWindow::isEnabled()
{
    return !mGrayOutWidget;
}

void MainWindow::setupMainWidgets()
{
    BoxSingleWidget::loadStaticPixmaps(eSizesUI::widget);

    mFillStrokeSettings = new FillStrokeSettingsWidget(mDocument, this);
    mFillStrokeSettings->hide();
    mFillStrokeSettings->setAttribute(Qt::WA_DontShowOnScreen, true);

    mLayoutHandler = new LayoutHandler(mDocument,
                                       mAudioHandler,
                                       this);
    mTimeline = new TimelineDockWidget(mDocument,
                                       mLayoutHandler,
                                       this);
    mRenderWidget = new RenderWidget(this);
}

void MainWindow::setupStackWidgets()
{
    mWelcomeDialog = new WelcomeDialog(mRecentMenu,
                                       [this]() { SceneSettingsDialog::sNewSceneDialog(mDocument, this); },
                                       []() { MainWindow::sGetInstance()->openFile(); },
                                       this);

    mStackWidget = new QStackedWidget(this);
    mStackIndexScene = mStackWidget->addWidget(mLayoutHandler->sceneLayout());
    mStackIndexWelcome = mStackWidget->addWidget(mWelcomeDialog);

    mCenterTabs = new QTabWidget(this);
    mCenterTabs->setObjectName("TabWidgetCenter");
    mCenterTabs->setDocumentMode(true);
    mCenterTabs->tabBar()->setFocusPolicy(Qt::NoFocus);
    mCenterTabs->tabBar()->setExpanding(false);
    mCenterTabs->setContentsMargins(0, 0, 0, 0);
    mCenterTabs->setTabPosition(QTabWidget::North);
    mCenterTabs->addTab(mStackWidget, tr("Composition"));

}

void MainWindow::setupMemoryWidgets()
{
    const auto timer = new QTimer(this);
    connect(timer, &QTimer::timeout,
            this, [this]() {
        if (mShutdown || !mCanvasToolBar) { return; }
        mCanvasToolBar->setMemoryUsage(mMemoryUsed);
    });
    timer->start(5000);

    const auto handler = MemoryHandler::sInstance;
    connect(handler, &MemoryHandler::memoryUsed,
            this, [this](intMB used) { mMemoryUsed = used; });
}

void MainWindow::setupPropertiesWidgets()
{
    mObjectSettingsScrollArea = new ScrollArea(this);
    mObjectSettingsScrollArea->setSizePolicy(QSizePolicy::Expanding,
                                             QSizePolicy::Expanding);
    mObjectSettingsScrollArea->setAutoFillBackground(true);
    mObjectSettingsScrollArea->setPalette(ThemeSupport::getDarkPalette());

    mObjectSettingsWidget = new BoxScrollWidget(mDocument,
                                                mObjectSettingsScrollArea);
    mObjectSettingsScrollArea->setWidget(mObjectSettingsWidget);
    mObjectSettingsWidget->setCurrentRule(SWT_BoxRule::all);
    mObjectSettingsWidget->setViewMode(SWT_ViewMode::effectsOnly);
    mObjectSettingsWidget->setCurrentTarget(nullptr, SWT_Target::group);

    // font widget
    mFontWidget = new Ui::FontsWidget(this);
    mFontWidget->setTextInputVisible(false);

    // align widget
    mAlignWidget = new Ui::AlignWidget(this);

    // assets widget
    mProjectWidget = new AssetsWidget(this);
    connect(mProjectWidget, &AssetsWidget::sceneOpenRequested,
            this, &MainWindow::activateSceneWorkspace);
    mPropertiesPanel = mObjectSettingsScrollArea;
    mPropertiesPanel->setObjectName("AeEffectControlsPanel");

    mProjectWidget->setWindowTitle(tr("Project"));
    mProjectWidget->setObjectName(QStringLiteral("ProjectPanel"));
    mProjectWidget->setParent(this);
    mEffectsPresetsPanel = createEffectsPresetsPanel();

    mCharacterPanel = new QWidget(this);
    mCharacterPanel->setObjectName(QStringLiteral("CharacterPanel"));
    const auto characterLayout = new QVBoxLayout(mCharacterPanel);
    characterLayout->setContentsMargins(4, 4, 4, 4);
    characterLayout->setSpacing(0);
    characterLayout->addWidget(mFontWidget, 0, Qt::AlignTop);
    characterLayout->addStretch();

    mAlignPanel = new QWidget(this);
    mAlignPanel->setObjectName(QStringLiteral("AlignPanel"));
    const auto alignLayout = new QVBoxLayout(mAlignPanel);
    alignLayout->setContentsMargins(4, 4, 4, 4);
    alignLayout->setSpacing(0);
    alignLayout->addWidget(mAlignWidget, 0, Qt::AlignTop);
    alignLayout->addStretch();

    mBottomTabs = new QTabWidget(this);
    mBottomTabs->setObjectName("AePanelTabs");
    mBottomTabs->setDocumentMode(true);
    mBottomTabs->setTabsClosable(true);
    mBottomTabs->tabBar()->setFocusPolicy(Qt::NoFocus);
    mBottomTabs->tabBar()->setExpanding(false);
    mBottomTabs->setContentsMargins(0, 0, 0, 0);
    mBottomTabs->setTabPosition(QTabWidget::North);
    eSizesUI::widget.add(mBottomTabs, [this](const int size) {
        mBottomTabs->setIconSize(QSize(size, size));
    });
    mTabQueueIndex = mBottomTabs->addTab(mRenderWidget,
                                         QIcon::fromTheme("render_animation"),
                                         tr("Render Queue"));
    if (auto *tabBar = mBottomTabs->tabBar()) {
        tabBar->setTabButton(mTabQueueIndex, QTabBar::RightSide, nullptr);
        tabBar->setTabButton(mTabQueueIndex, QTabBar::LeftSide, nullptr);
    }
    connect(mBottomTabs, &QTabWidget::currentChanged,
            this, [this](const int index) {
        if (!mBottomTabs || mSyncingBottomTabs || index < 0) { return; }
        if (mBottomTabs->widget(index) == mRenderWidget) {
            return;
        }
        const auto scenePtr = mBottomTabs->tabBar()->tabData(index).value<quintptr>();
        if (scenePtr == 0) { return; }
        if (auto *scene = reinterpret_cast<Canvas*>(scenePtr)) {
            attachTimelineToBottomScene(scene);
            mTimeline->updateSettingsForCurrentCanvas(scene);
            mDocument.setActiveScene(scene);
        }
    });
    connect(mBottomTabs, &QTabWidget::tabCloseRequested,
            this, &MainWindow::closeBottomSceneTab);
    ensureBottomSceneTab(*mDocument.fActiveScene);
    selectBottomSceneTab(*mDocument.fActiveScene);
    updateWorkspaceTabTitles();

    connect(mObjectSettingsScrollArea->verticalScrollBar(),
            &QScrollBar::valueChanged,
            mObjectSettingsWidget, &BoxScrollWidget::changeVisibleTop);
    connect(mObjectSettingsScrollArea, &ScrollArea::heightChanged,
            mObjectSettingsWidget, &BoxScrollWidget::changeVisibleHeight);
    connect(mObjectSettingsScrollArea, &ScrollArea::widthChanged,
            mObjectSettingsWidget, &BoxScrollWidget::setWidth);

    connect(&mDocument, &Document::activeSceneSet,
            this, [this](Canvas *scene) {
        updateSceneNavigationChain(scene);
        if (mBottomSceneTabs.contains(scene)) {
            attachTimelineToBottomScene(scene);
            selectBottomSceneTab(scene);
        }
        mTimeline->updateSettingsForCurrentCanvas(scene);
        updateWorkspaceTabTitles();
        if (scene) {
            connect(scene, &Canvas::prp_nameChanged,
                    this, [this, scene]() {
                        updateWorkspaceTabTitles();
                        if (!mBottomTabs) { return; }
                        const auto it = mBottomSceneTabs.constFind(scene);
                        if (it == mBottomSceneTabs.constEnd()) { return; }
                        const int idx = mBottomTabs->indexOf(it.value());
                        if (idx >= 0) {
                            mBottomTabs->setTabText(idx, scene->prp_getName());
                        }
                    },
                    Qt::UniqueConnection);
        }
    });
    connect(&mDocument, qOverload<Canvas*>(&Document::sceneRemoved),
            this, [this](Canvas *scene) {
        const auto it = mBottomSceneTabs.find(scene);
        if (it == mBottomSceneTabs.end() || !mBottomTabs) { return; }
        const int idx = mBottomTabs->indexOf(it.value());
        if (idx >= 0) {
            closeBottomSceneTab(idx);
        }
    });

}

QWidget *MainWindow::createEffectsPresetsPanel()
{
    const auto panel = new QWidget(this);
    panel->setObjectName("EffectsPresetsPanel");
    const auto layout = new QVBoxLayout(panel);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    const auto search = new QLineEdit(panel);
    search->setObjectName("EffectsPresetsSearch");
    search->setPlaceholderText(tr("Search Effects"));
    search->setClearButtonEnabled(true);
    layout->addWidget(search);

    const auto tree = new EffectsPresetsTreeWidget(panel);
    tree->setHeaderHidden(true);
    tree->setRootIsDecorated(true);
    tree->setFrameShape(QFrame::NoFrame);
    tree->setObjectName("EffectsPresetsTree");
    tree->header()->setStretchLastSection(true);
    tree->setAlternatingRowColors(true);
    tree->setSelectionMode(QAbstractItemView::SingleSelection);
    tree->setDragEnabled(true);
    tree->setDragDropMode(QAbstractItemView::DragOnly);
    tree->setDefaultDropAction(Qt::CopyAction);

    QHash<QString, EffectsPresetItem*> roots;

    auto addRasterPreset = [&](const QString &rootName,
                               const QString &name,
                               const QString &path,
                               const RasterEffectMenuCreator::EffectCreator &creator) {
        const auto parent = ensureEffectsCategory(tree, roots, rootName, path);
        const auto item = new EffectsPresetItem(parent, name);
        item->configureRaster(EffectsPresetApplyTarget::Raster, creator);
    };

    auto addBlendPreset = [&](const QString &name,
                              const BlendEffectMenuCreator::EffectCreator &creator) {
        const auto parent = ensureEffectsCategory(tree, roots, tr("Blend Effects"));
        const auto item = new EffectsPresetItem(parent, name);
        item->configureBlend(creator);
    };

    auto addTransformPreset = [&](const QString &name,
                                  const TransformEffectMenuCreator::EffectCreator &creator) {
        const auto parent = ensureEffectsCategory(tree, roots, tr("Transform Effects"));
        const auto item = new EffectsPresetItem(parent, name);
        item->configureTransform(creator);
    };

    auto addPathPreset = [&](const QString &rootName,
                             const EffectsPresetApplyTarget target,
                             const QString &name,
                             const PathEffectMenuCreator::EffectCreator &creator) {
        const auto parent = ensureEffectsCategory(tree, roots, rootName);
        const auto item = new EffectsPresetItem(parent, name);
        item->configurePath(target, creator);
    };

    RasterEffectMenuCreator::forEveryEffectCore([&](const QString &name,
                                                    const QString &path,
                                                    const RasterEffectMenuCreator::EffectCreator &creator) {
        if (!name.isEmpty()) {
            addRasterPreset(tr("Raster Effects"), name, path, creator);
        }
    });
    RasterEffectMenuCreator::forEveryEffectCustom([&](const QString &name,
                                                      const QString &path,
                                                      const RasterEffectMenuCreator::EffectCreator &creator) {
        if (!name.isEmpty()) {
            addRasterPreset(tr("Raster Effects"), name, path, creator);
        }
    });
    RasterEffectMenuCreator::forEveryEffectShader([&](const QString &name,
                                                      const QString &path,
                                                      const RasterEffectMenuCreator::EffectCreator &creator) {
        if (!name.isEmpty()) {
            addRasterPreset(tr("Shader Effects"), name, path, creator);
        }
    });
    BlendEffectMenuCreator::forEveryEffect([&](const QString &name,
                                               const BlendEffectMenuCreator::EffectCreator &creator) {
        if (!name.isEmpty()) {
            addBlendPreset(name, creator);
        }
    });
    TransformEffectMenuCreator::forEveryEffect([&](const QString &name,
                                                   const TransformEffectMenuCreator::EffectCreator &creator) {
        if (!name.isEmpty()) {
            addTransformPreset(name, creator);
        }
    });
    PathEffectMenuCreator::forEveryEffect([&](const QString &name,
                                              const PathEffectMenuCreator::EffectCreator &creator) {
        if (!name.isEmpty()) {
            addPathPreset(tr("Path Effects"), EffectsPresetApplyTarget::Path, name, creator);
            addPathPreset(tr("Fill Effects"), EffectsPresetApplyTarget::FillPath, name, creator);
            addPathPreset(tr("Outline Base Effects"), EffectsPresetApplyTarget::OutlineBasePath, name, creator);
            addPathPreset(tr("Outline Effects"), EffectsPresetApplyTarget::OutlinePath, name, creator);
        }
    });

    connect(search, &QLineEdit::textChanged,
            tree, [tree](const QString &text) {
        const QString trimmed = text.trimmed();
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            filterEffectsPresetItem(tree->topLevelItem(i), trimmed);
        }
    });

    connect(tree, &QTreeWidget::itemDoubleClicked,
            panel, [this](QTreeWidgetItem *rawItem, int) {
        const auto item = dynamic_cast<EffectsPresetItem*>(rawItem);
        if (!item || !item->isEffectItem()) {
            return;
        }

        switch (item->target()) {
        case EffectsPresetApplyTarget::Raster:
            addRasterEffect(item->createRasterEffect());
            break;
        case EffectsPresetApplyTarget::Blend:
            addBlendEffect(item->createBlendEffect());
            break;
        case EffectsPresetApplyTarget::Transform:
            addTransformEffect(item->createTransformEffect());
            break;
        case EffectsPresetApplyTarget::Path:
            addPathEffect(item->createPathEffect());
            break;
        case EffectsPresetApplyTarget::FillPath:
            addFillPathEffect(item->createPathEffect());
            break;
        case EffectsPresetApplyTarget::OutlineBasePath:
            addOutlineBasePathEffect(item->createPathEffect());
            break;
        case EffectsPresetApplyTarget::OutlinePath:
            addOutlinePathEffect(item->createPathEffect());
            break;
        }
    });

    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        tree->topLevelItem(i)->setExpanded(true);
    }

    layout->addWidget(tree);
    return panel;
}

void MainWindow::syncPanelsMenuState()
{
    const auto setCheckedFromDock =
        [this](QAction * const action,
               const QString &label,
               QWidget * const fallbackWidget = nullptr) {
            if (!action) { return; }
            bool checked = false;
            if (mUI) {
                checked = mUI->isDockVisible(label);
            } else if (fallbackWidget) {
                const auto parent = fallbackWidget->parentWidget();
                checked = parent ? parent->isVisible() : fallbackWidget->isVisible();
            }
            action->blockSignals(true);
            action->setChecked(checked);
            action->blockSignals(false);
        };

    setCheckedFromDock(mPanelCompositionAct, tr("Composition"), mCenterTabs);
    setCheckedFromDock(mPanelProjectAct, tr("Project"), mProjectWidget);
    setCheckedFromDock(mPanelEffectControlsAct, tr("Effect Controls"), mPropertiesPanel);
    setCheckedFromDock(mPanelLayersAct, tr("Layers"), mBottomTabs);
    setCheckedFromDock(mPanelEffectsAct, tr("Effect Presets"), mEffectsPresetsPanel);
    setCheckedFromDock(mPanelCharacterAct, tr("Character"), mCharacterPanel);
    setCheckedFromDock(mPanelAlignAct, tr("Align"), mAlignPanel);
}

void MainWindow::updateWorkspaceTabTitles()
{
    const auto scene = *mDocument.fActiveScene;
    const QString sceneName = scene ? scene->prp_getName() : tr("Composition");

    if (mCenterTabs) {
        mCenterTabs->setTabText(0, sceneName);
    }
    if (mBottomTabs && scene) {
        const auto it = mBottomSceneTabs.constFind(scene);
        if (it != mBottomSceneTabs.constEnd()) {
            const int idx = mBottomTabs->indexOf(it.value());
            if (idx >= 0) {
                mBottomTabs->setTabText(idx, sceneName);
            }
        }
    }
}

void MainWindow::ensureBottomSceneTab(Canvas *scene)
{
    if (!scene || !mBottomTabs || (mTimelineWindowAct && mTimelineWindowAct->isChecked())) { return; }

    QWidget *page = mBottomSceneTabs.value(scene, nullptr);
    if (!page) {
        page = new QWidget(mBottomTabs);
        auto *layout = new QVBoxLayout(page);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        const int renderIndex = mBottomTabs->indexOf(mRenderWidget);
        const int insertIndex = renderIndex >= 0 ? renderIndex : mBottomTabs->count();
        mBottomTabs->insertTab(insertIndex,
                               page,
                               QIcon::fromTheme("timeline"),
                               scene->prp_getName());
        mBottomSceneTabs.insert(scene, page);
    }

    const int idx = mBottomTabs->indexOf(page);
    if (idx < 0) { return; }

    mBottomTabs->setTabText(idx, scene->prp_getName());
    mBottomTabs->tabBar()->setTabData(idx,
                                      QVariant::fromValue<quintptr>(
                                          reinterpret_cast<quintptr>(scene)));
}

void MainWindow::attachTimelineToBottomScene(Canvas *scene)
{
    if (!scene || !mBottomTabs || !mTimeline ||
        (mTimelineWindowAct && mTimelineWindowAct->isChecked())) {
        return;
    }
    ensureBottomSceneTab(scene);
    const auto it = mBottomSceneTabs.constFind(scene);
    if (it == mBottomSceneTabs.constEnd()) { return; }
    auto *page = it.value();
    if (!page) { return; }
    if (mTimeline->parentWidget() == page) { return; }
    if (auto *layout = qobject_cast<QVBoxLayout*>(page->layout())) {
        layout->addWidget(mTimeline);
    }
}

void MainWindow::activateSceneWorkspace(Canvas *scene)
{
    if (!scene) { return; }
    ensureBottomSceneTab(scene);
    attachTimelineToBottomScene(scene);
    mTimeline->updateSettingsForCurrentCanvas(scene);
    mDocument.setActiveScene(scene);
    selectBottomSceneTab(scene);
}

void MainWindow::selectBottomSceneTab(Canvas *scene)
{
    if (!scene || !mBottomTabs || (mTimelineWindowAct && mTimelineWindowAct->isChecked())) { return; }
    const auto it = mBottomSceneTabs.constFind(scene);
    if (it == mBottomSceneTabs.constEnd()) { return; }
    const int idx = mBottomTabs->indexOf(it.value());
    if (idx < 0) { return; }

    mSyncingBottomTabs = true;
    mBottomTabs->setCurrentIndex(idx);
    mSyncingBottomTabs = false;
}

void MainWindow::closeBottomSceneTab(int index)
{
    if (!mBottomTabs || index < 0 || index >= mBottomTabs->count()) { return; }
    auto *page = mBottomTabs->widget(index);
    if (!page || page == mRenderWidget) { return; }

    Canvas *closedScene = nullptr;
    const auto scenePtr = mBottomTabs->tabBar()->tabData(index).value<quintptr>();
    if (scenePtr != 0) {
        closedScene = reinterpret_cast<Canvas*>(scenePtr);
    }

    if (closedScene) {
        mBottomSceneTabs.remove(closedScene);
        for (int i = mSceneNavigationChain.count() - 1; i >= 0; --i) {
            if (mSceneNavigationChain.at(i) == closedScene) {
                mSceneNavigationChain.removeAt(i);
            }
        }
    }

    if (mTimeline->parentWidget() == page) {
        mTimeline->setParent(nullptr);
    }
    mBottomTabs->removeTab(index);
    delete page;

    if (closedScene && *mDocument.fActiveScene == closedScene) {
        Canvas *nextScene = nullptr;
        for (int i = mSceneNavigationChain.count() - 1; i >= 0; --i) {
            if (mSceneNavigationChain.at(i) &&
                mBottomSceneTabs.contains(mSceneNavigationChain.at(i))) {
                nextScene = mSceneNavigationChain.at(i);
                break;
            }
        }

        if (!nextScene && !mBottomSceneTabs.isEmpty()) {
            nextScene = mBottomSceneTabs.constBegin().key();
        }

        if (nextScene) {
            mDocument.setActiveScene(nextScene);
        } else if (mBottomTabs->indexOf(mRenderWidget) >= 0) {
            mSyncingBottomTabs = true;
            mBottomTabs->setCurrentWidget(mRenderWidget);
            mSyncingBottomTabs = false;
        }
    }
}

void MainWindow::updateSceneNavigationChain(Canvas *scene)
{
    for (int i = mSceneNavigationChain.count() - 1; i >= 0; --i) {
        if (!mSceneNavigationChain.at(i)) {
            mSceneNavigationChain.removeAt(i);
        }
    }

    if (!scene) {
        mSceneNavigationChain.clear();
        return;
    }

    const auto oraChainIds = OraModule::sceneNavigationChainIds(scene);
    if (!oraChainIds.isEmpty()) {
        mSceneNavigationChain.clear();
        for (const int sceneId : oraChainIds) {
            for (const auto &candidate : mDocument.fScenes) {
                if (candidate && candidate->getDocumentId() == sceneId) {
                    mSceneNavigationChain.append(candidate.get());
                    break;
                }
            }
        }
        if (mSceneNavigationChain.isEmpty() ||
            mSceneNavigationChain.last() != scene) {
            mSceneNavigationChain.append(scene);
        }
        return;
    }

    const int existing = mSceneNavigationChain.indexOf(scene);
    if (existing >= 0) {
        while (mSceneNavigationChain.count() > existing + 1) {
            mSceneNavigationChain.removeLast();
        }
    } else {
        mSceneNavigationChain.append(scene);
    }
}

void MainWindow::setupLayout()
{
    mUI = new UILayout(this);
    std::vector<UILayout::Item> docks;
    docks.push_back({UIDock::Position::Up,
                     -1,
                     tr("Composition"),
                     mCenterTabs,
                     true,
                     true,
                     false});
    docks.push_back({UIDock::Position::Left,
                     -1,
                     tr("Project"),
                     mProjectWidget,
                     true,
                     true,
                     false});
    docks.push_back({UIDock::Position::Left,
                     -1,
                     tr("Effect Controls"),
                     mPropertiesPanel,
                     true,
                     true,
                     false});
    docks.push_back({UIDock::Position::Down,
                     -1,
                     tr("Layers"),
                     mBottomTabs,
                     false,
                     true,
                     false});
    docks.push_back({UIDock::Position::Right,
                     -1,
                     tr("Effect Presets"),
                     mEffectsPresetsPanel,
                     true,
                     true,
                     false});
    docks.push_back({UIDock::Position::Right,
                     -1,
                     tr("Character"),
                     mCharacterPanel,
                     true,
                     true,
                     false});
    docks.push_back({UIDock::Position::Right,
                     -1,
                     tr("Align"),
                     mAlignPanel,
                     true,
                     true,
                     false});
    mUI->addDocks(docks);
    setCentralWidget(mUI);
}

void MainWindow::clearAll()
{
    TaskScheduler::instance()->clearTasks();
    setFileChangedSinceSaving(false);
    mObjectSettingsWidget->setMainTarget(nullptr);

    mRenderWidget->clearRenderQueue();
    mFillStrokeSettings->clearAll();
    mFontWidget->clearAll();
    mDocument.clear();
    mLayoutHandler->clear();
    FilesHandler::sInstance->clear();

    mActions.setMovePathMode();

    openWelcomeDialog();
}

void MainWindow::updateTitle()
{
    QString unsaved = mChangedSinceSaving ? " *" : "";
    QFileInfo info(mDocument.fEvFile);
    QString file = info.baseName();
    if (file.isEmpty()) { file = tr("Untitled"); }
    setWindowTitle(QString("%1%2").arg(file, unsaved));
    if (mSaveAct) {
        mSaveAct->setText(mChangedSinceSaving ? tr("Save *") : tr("Save"));
    }
}

void MainWindow::openFile()
{
    if (askForSaving()) {
        disable();
        const QString defPath = mDocument.fEvFile.isEmpty() ? getLastOpenDir() : mDocument.fEvFile;
        const QString title = tr("Open File", "OpenDialog_Title");
        const QString files = tr("Friction Files %1", "OpenDialog_FileType");
        const QString openPath = eDialogs::openFile(title, defPath,
                                                    files.arg("(*.friction *.ev)"));
        if (!openPath.isEmpty()) { openFile(openPath); }
        enable();
    }
}

void MainWindow::openFile(const QString& openPath)
{
    clearAll();
    try {
        QFileInfo fi(openPath);
        const QString suffix = fi.suffix();
        if (suffix == "friction" || suffix == "ev") {
            loadEVFile(openPath);
        } /*else if (suffix == "xev") {
            loadXevFile(openPath);
        }*/ else { RuntimeThrow("Unrecognized file extension " + suffix); }
        mDocument.setPath(openPath);
        setFileChangedSinceSaving(false);
        updateLastOpenDir(openPath);
    } catch(const std::exception& e) {
        gPrintExceptionCritical(e);
    }
    mDocument.actionFinished();
}

void MainWindow::saveFile()
{
    if (mDocument.fEvFile.isEmpty()) { saveFileAs(true); }
    else {
        const int projectVersion = AppSupport::getProjectVersion(mDocument.fEvFile);
        const int newProjectVersion = AppSupport::getProjectVersion();
        if (newProjectVersion > projectVersion && projectVersion > 0) {
            const auto result = QMessageBox::question(this,
                                                      tr("Project version"),
                                                      tr("Saving this project file will change the project"
                                                         " format from version %1 to version %2."
                                                         " This breaks compatibility with older versions of Friction."
                                                         "\n\nAre you sure you want"
                                                         " to save this project file?").arg(QString::number(projectVersion),
                                                                                            QString::number(newProjectVersion)));
            if (result != QMessageBox::Yes) { return; }
        }
        saveFile(mDocument.fEvFile);
    }
}

void MainWindow::saveFile(const QString& path,
                          const bool setPath)
{
    try {
        QFileInfo fi(path);
        const QString suffix = fi.suffix();
        if (suffix == "friction" || suffix == "ev") {
            saveToFile(path);
        } /*else if (suffix == "xev") {
            saveToFileXEV(path);
            const auto& inst = DialogsInterface::instance();
            inst.displayMessageToUser("Please note that the XEV format is still in the testing phase.");
        }*/ else { RuntimeThrow("Unrecognized file extension " + suffix); }
        if (setPath) mDocument.setPath(path);
        setFileChangedSinceSaving(false);
        updateLastSaveDir(path);
        if (mBackupOnSave) {
            qDebug() << "auto backup";
            saveBackup();
        }
    } catch(const std::exception& e) {
        gPrintExceptionCritical(e);
    }
}

void MainWindow::saveFileAs(const bool setPath)
{
    disableEventFilter();
    const QString defPath = mDocument.fEvFile.isEmpty() ? getLastSaveDir() : mDocument.fEvFile;

    const QString title = tr("Save File", "SaveDialog_Title");
    const QString fileType = tr("Friction Files %1", "SaveDialog_FileType");
    QString saveAs = eDialogs::saveFile(title, defPath, fileType.arg("(*.friction)"));
    enableEventFilter();
    if (!saveAs.isEmpty()) {
        //const bool isXEV = saveAs.right(4) == ".xev";
        //if (!isXEV && saveAs.right(3) != ".ev") { saveAs += ".ev"; }
        QString suffix = QString::fromUtf8(".friction");
        if (!saveAs.endsWith(suffix)) { saveAs.append(suffix); }
        saveFile(saveAs, setPath);
    }
}

void MainWindow::saveBackup()
{
    const QString defPath = mDocument.fEvFile;
    QFileInfo defInfo(defPath);
    if (defPath.isEmpty() || defInfo.isDir())  { return; }
    const QString backupPath = defPath + "_backup/backup_%1.friction";
    int id = 1;
    QFile backupFile(backupPath.arg(id));
    while (backupFile.exists()) {
        id++;
        backupFile.setFileName(backupPath.arg(id) );
    }
    try {
        saveToFile(backupPath.arg(id), false);
    } catch(const std::exception& e) {
        gPrintExceptionCritical(e);
    }
}

const QString MainWindow::checkBeforeExportSVG()
{
    QStringList result;
    for (const auto& scene : mDocument.fScenes) {
        const auto warnings = scene->checkForUnsupportedSVG();
        if (!warnings.isEmpty()) { result.append(warnings); }
    }
    return result.join("");
}

void MainWindow::exportSVG(const bool &preview)
{
    const auto dialog = new ExportSvgDialog(this,
                                            preview ? QString() : checkBeforeExportSVG());
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    if (!preview) {
        dialog->show();
    } else {
        dialog->showPreview(true /* close when done */);
    }
}

void MainWindow::updateLastOpenDir(const QString &path)
{
    if (path.isEmpty()) { return; }
    QFileInfo i(path);
    AppSupport::setSettings("files",
                            "lastOpenDir",
                            i.absoluteDir().absolutePath());
}

void MainWindow::updateLastSaveDir(const QString &path)
{
    if (path.isEmpty()) { return; }
    QFileInfo i(path);
    AppSupport::setSettings("files",
                            "lastSaveDir",
                            i.absoluteDir().absolutePath());
}

const QString MainWindow::getLastOpenDir()
{
    return AppSupport::getSettings("files",
                                   "lastOpenDir",
                                   QDir::homePath()).toString();
}

const QString MainWindow::getLastSaveDir()
{
    return AppSupport::getSettings("files",
                                   "lastSaveDir",
                                   QDir::homePath()).toString();
}

bool MainWindow::closeProject()
{
    if (askForSaving()) {
        clearAll();
        return true;
    }
    return false;
}

void MainWindow::importFile()
{
    disableEventFilter();

    const auto recentDir = AppSupport::getSettings("files",
                                                   "recentImportDir",
                                                   QDir::homePath()).toString();
    QString defPath = QDir::homePath();
    switch (eSettings::instance().fImportFileDirOpt) {
    case eSettings::ImportFileDirRecent:
        defPath = recentDir;
        break;
    case eSettings::ImportFileDirProject:
        defPath = mDocument.fEvFile.isEmpty() ? recentDir : mDocument.fEvFile;
        break;
    default:;
    }

    const QString title = tr("Import File(s)", "ImportDialog_Title");
    const QString fileType = tr("Files %1", "ImportDialog_FileTypes");
    const QString fileTypes = "(*.friction *.ev *.svg *.ora *.glb *.gltf " +
            FileExtensions::videoFilters() +
            FileExtensions::imageFilters() +
            FileExtensions::soundFilters() + ")";
    const auto importPaths = eDialogs::openFiles(
                title, defPath, fileType.arg(fileTypes));
    enableEventFilter();
    if (!importPaths.isEmpty()) {
        for(const QString &path : importPaths) {
            if (path.isEmpty()) { continue; }
            try {
                mActions.importFile(path);
            } catch(const std::exception& e) {
                gPrintExceptionCritical(e);
            }
        }
    }
}

void MainWindow::linkFile()
{
    disableEventFilter();
    const QString defPath = mDocument.fEvFile.isEmpty() ?
                QDir::homePath() : mDocument.fEvFile;
    const QString title = tr("Link File", "LinkDialog_Title");
    const QString fileType = tr("Files %1", "LinkDialog_FileType");
    const auto importPaths = eDialogs::openFiles(
                title, defPath, fileType.arg("(*.svg *.ora)"));
    enableEventFilter();
    if (!importPaths.isEmpty()) {
        for (const QString &path : importPaths) {
            if (path.isEmpty()) { continue; }
            try {
                mActions.linkFile(path);
            } catch(const std::exception& e) {
                gPrintExceptionCritical(e);
            }
        }
    }
}

void MainWindow::importImageSequence()
{
    disableEventFilter();
    const QString defPath = mDocument.fEvFile.isEmpty() ?
                QDir::homePath() : mDocument.fEvFile;
    const QString title = tr("Import Image Sequence",
                             "ImportSequenceDialog_Title");
    const auto folder = eDialogs::openDir(title, defPath);
    enableEventFilter();
    if (!folder.isEmpty()) { mActions.importFile(folder); }
}

void MainWindow::revert()
{
    const int ask = QMessageBox::question(this,
                                          tr("Confirm revert"),
                                          tr("Are you sure you want to revert current project?"
                                             "<p><b>Any changes will be lost.</b></p>"));
    if (ask == QMessageBox::No) { return; }
    const QString path = mDocument.fEvFile;
    openFile(path);
}

void MainWindow::updateAutoSaveBackupState()
{
    if (mShutdown) { return; }

    mBackupOnSave = AppSupport::getSettings("files",
                                            "BackupOnSave",
                                            false).toBool();
    mAutoSave = AppSupport::getSettings("files",
                                        "AutoSave",
                                        false).toBool();
    int lastTimeout = mAutoSaveTimeout;
    mAutoSaveTimeout = AppSupport::getSettings("files",
                                               "AutoSaveTimeout",
                                               300000).toInt();
    qDebug() << "update auto save/backup state" << mBackupOnSave << mAutoSave << mAutoSaveTimeout;
    if (mAutoSave && !mAutoSaveTimer->isActive()) {
        mAutoSaveTimer->start(mAutoSaveTimeout);
    } else if (!mAutoSave && mAutoSaveTimer->isActive()) {
        mAutoSaveTimer->stop();
    }
    if (mAutoSave &&
        lastTimeout > 0 &&
        lastTimeout != mAutoSaveTimeout) {
        if (mAutoSaveTimer->isActive()) { mAutoSaveTimer->stop(); }
        mAutoSaveTimer->start(mAutoSaveTimeout);
    }
}

void MainWindow::openRendererWindow()
{
    if (mRenderWidget->count() < 1) {
        addCanvasToRenderQue();
    } else {
        if (mRenderWindowAct->isChecked()) { openRenderQueueWindow(); }
        else if (mBottomTabs) { mBottomTabs->setCurrentWidget(mRenderWidget); }
    }
}

void MainWindow::cmdAddAction(QAction *act)
{
    if (!act || eSettings::instance().fCommandPalette.contains(act)) { return; }
    eSettings::sInstance->fCommandPalette.append(act);
}

LayoutHandler *MainWindow::getLayoutHandler()
{
    return mLayoutHandler;
}

TimelineDockWidget *MainWindow::getTimeLineWidget()
{
    return mTimeline;
}

QList<Canvas*> MainWindow::sceneNavigationChain() const
{
    QList<Canvas*> chain;
    for (const auto &scene : mSceneNavigationChain) {
        if (scene) {
            chain.append(scene);
        }
    }
    return chain;
}

void MainWindow::focusFontWidget(const bool focus)
{
    if (mUI) { mUI->setDockVisible("Character", true); }
    if (focus) { mFontWidget->setTextFocus(); }
}

void MainWindow::focusColorWidget()
{
    if (mUI) { mUI->setDockVisible("Effect Controls", true); }
}

void MainWindow::openCurrentTextEditorPopup()
{
    const auto textBox = enve_cast<TextBox*>(getCurrentBox());
    if (!textBox) {
        focusFontWidget(false);
        return;
    }

    focusFontWidget(false);

    QWidget *dialogParent = this;
    if (auto *focus = QApplication::focusWidget()) {
        QWidget *probe = focus;
        while (probe) {
            if (qobject_cast<CanvasWindow*>(probe)) {
                dialogParent = probe;
                break;
            }
            probe = probe->parentWidget();
        }
    }

    QDialog dialog(dialogParent);
    dialog.setWindowTitle(tr("Edit Text"));
    dialog.setModal(true);
    dialog.resize(420, 220);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    auto *editor = new QPlainTextEdit(&dialog);
    editor->setPlainText(textBox->getCurrentValue());
    editor->setPlaceholderText(tr("Type text for the selected layer"));
    layout->addWidget(editor);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         Qt::Horizontal,
                                         &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialogParent) {
        const QPoint center = dialogParent->mapToGlobal(dialogParent->rect().center());
        dialog.move(center.x() - dialog.width()/2, center.y() - dialog.height()/2);
    }

    editor->setFocus();
    QTextCursor cursor = editor->textCursor();
    cursor.movePosition(QTextCursor::End);
    editor->setTextCursor(cursor);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString newText = editor->toPlainText();
    if (newText == textBox->getCurrentValue()) {
        return;
    }

    textBox->prp_startTransform();
    textBox->setCurrentValue(newText);
    textBox->prp_finishTransform();
    mDocument.actionFinished();
}

void MainWindow::openMarkerEditor()
{
    const auto scene = *mDocument.fActiveScene;
    if (!scene) { return; }
    const auto dialog = new Ui::MarkerEditorDialog(scene, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

void MainWindow::openExpressionDialog(QrealAnimator * const target)
{
    if (!target) { return; }
    const auto dialog = new ExpressionDialog(target, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

void MainWindow::openApplyExpressionDialog(QrealAnimator * const target)
{
    if (!target) { return; }
    const auto dialog = new ApplyExpressionDialog(target, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

stdsptr<void> MainWindow::lock()
{
    if (mLock) { return mLock->ref<Lock>(); }
    setEnabled(false);
    const auto newLock = enve::make_shared<Lock>(this);
    mLock = newLock.get();
    QApplication::setOverrideCursor(Qt::WaitCursor);
    return newLock;
}

void MainWindow::lockFinished()
{
    if (mLock) {
        gPrintException(false, "Lock finished before lock object deleted");
    } else {
        QApplication::restoreOverrideCursor();
        setEnabled(true);
    }
}

void MainWindow::resizeEvent(QResizeEvent* e)
{
    //if (statusBar()) { statusBar()->setMaximumWidth(width()); }
    QMainWindow::resizeEvent(e);
}

void MainWindow::showEvent(QShowEvent *e)
{
    //if (statusBar()) { statusBar()->setMaximumWidth(width()); }
    QMainWindow::showEvent(e);
}

void MainWindow::updateRecentMenu()
{
    mRecentMenu->clear();
    for (const auto &path : mRecentFiles) {
        QFileInfo info(path);
        if (!info.exists()) { continue; }
        mRecentMenu->addAction(QIcon::fromTheme(AppSupport::getAppID()), info.baseName(), [path, this]() {
            openFile(path);
        });
    }
}

void MainWindow::addRecentFile(const QString &recent)
{
    if (mRecentFiles.contains(recent)) {
        mRecentFiles.removeOne(recent);
    }
    while (mRecentFiles.count() >= 11) {
        mRecentFiles.removeLast();
    }
    mRecentFiles.prepend(recent);
    updateRecentMenu();
    writeRecentFiles();
}

void MainWindow::readRecentFiles()
{
    const auto files = AppSupport::getSettings("files",
                                               "recentSaved").toStringList();
    for (const auto &file : files) { mRecentFiles.append(file); }
}

void MainWindow::writeRecentFiles()
{
    QStringList files;
    for (const auto &file : mRecentFiles) { files.append(file); }
    AppSupport::setSettings("files", "recentSaved", files);
}

void MainWindow::handleNewVideoClip(const VideoBox::VideoSpecs &specs)
{
    int act = eSettings::instance().fAdjustSceneFromFirstClip;

    // never apply or bad specs?
    if (act == eSettings::AdjustSceneNever ||
        specs.fps < 1 ||
        specs.dim.height() < 1 ||
        specs.dim.width() < 1) { return; }

    const auto scene = *mDocument.fActiveScene;
    if (!scene) { return; }

    // only continue if this is the only clip
    if (scene->getContainedBoxes().count() != 1) { return; }

    // is identical?
    if (scene->getCanvasSize() == specs.dim &&
        scene->getFps() == specs.fps &&
        scene->getFrameRange().fMax == specs.range.fMax) { return; }

    // always apply?
    if (act == eSettings::AdjustSceneAlways) {
        scene->setCanvasSize(specs.dim.width(),
                             specs.dim.height());
        scene->setFps(specs.fps);
        scene->setFrameRange(specs.range);
        return;
    }

    // open dialog if ask
    AdjustSceneDialog dialog(scene, specs, this);
    dialog.exec();
}
