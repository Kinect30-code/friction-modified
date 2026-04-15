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

#include "boxsinglewidget.h"
#include "swt_abstraction.h"
#include "singlewidgettarget.h"
#include "optimalscrollarena/scrollwidgetvisiblepart.h"
#include "widgets/colorsettingswidget.h"

#include "Boxes/containerbox.h"
#include "widgets/qrealanimatorvalueslider.h"
#include "boxscroller.h"
#include "GUI/keysview.h"
#include "renderhandler.h"
#include "pointhelpers.h"
#include "GUI/BoxesList/boolpropertywidget.h"
#include "boxtargetwidget.h"
#include "particleoverlifewidget.h"
#include "Properties/boxtargetproperty.h"
#include "Properties/comboboxproperty.h"
#include "Animators/enumanimator.h"
#include "Animators/boolanimator.h"
#include "Animators/qstringanimator.h"
#include "RasterEffects/rastereffectcollection.h"
#include "Properties/boolproperty.h"
#include "Properties/boolpropertycontainer.h"
#include "Animators/qpointfanimator.h"
#include "Animators/qrealanimator.h"
#include "Animators/transformanimator.h"
#include "Boxes/pathbox.h"
#include "Boxes/imagebox.h"
#include "Boxes/imagesequencebox.h"
#include "Boxes/internallinkcanvas.h"
#include "Boxes/nullobject.h"
#include "Boxes/textbox.h"
#include "Boxes/videobox.h"
#include "canvas.h"
#include "BlendEffects/blendeffectcollection.h"
#include "BlendEffects/blendeffectboxshadow.h"
#include "BlendEffects/trackmatteeffect.h"
#include "Sound/eindependentsound.h"
#include "GUI/propertynamedialog.h"
#include "Animators/SmartPath/smartpathcollection.h"
#include "GUI/timelinewidget.h"
#include "GUI/BoxesList/boxscrollwidget.h"
#include "GUI/dialogsinterface.h"
#include "Expressions/expression.h"
#include "../../../modules/particles/particleoverlifeanimator.h"
#include "../../../modules/particles/particlesystemeffect.h"

#include "typemenu.h"
#include "themesupport.h"

#include <QFileInfo>
#include <QLocale>
#include <QMessageBox>

QPixmap* BoxSingleWidget::VISIBLE_ICON;
QPixmap* BoxSingleWidget::INVISIBLE_ICON;
QPixmap* BoxSingleWidget::BOX_CHILDREN_VISIBLE_ICON;
QPixmap* BoxSingleWidget::BOX_CHILDREN_HIDDEN_ICON;
QPixmap* BoxSingleWidget::ANIMATOR_CHILDREN_VISIBLE_ICON;
QPixmap* BoxSingleWidget::ANIMATOR_CHILDREN_HIDDEN_ICON;
QPixmap* BoxSingleWidget::LOCKED_ICON;
QPixmap* BoxSingleWidget::UNLOCKED_ICON;
QPixmap* BoxSingleWidget::MUTED_ICON;
QPixmap* BoxSingleWidget::UNMUTED_ICON;
QPixmap* BoxSingleWidget::ANIMATOR_RECORDING_ICON;
QPixmap* BoxSingleWidget::ANIMATOR_NOT_RECORDING_ICON;
QPixmap* BoxSingleWidget::ANIMATOR_DESCENDANT_RECORDING_ICON;
QPixmap* BoxSingleWidget::C_ICON;
QPixmap* BoxSingleWidget::G_ICON;
QPixmap* BoxSingleWidget::CG_ICON;
QPixmap* BoxSingleWidget::GRAPH_PROPERTY_ICON;
QPixmap* BoxSingleWidget::PROMOTE_TO_LAYER_ICON;

bool BoxSingleWidget::sStaticPixmapsLoaded = false;

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

BoundingBox *defaultTrackMatteSource(BoundingBox *box)
{
    if (!box) {
        return nullptr;
    }
    auto *parent = box->getParentGroup();
    if (!parent) {
        return nullptr;
    }

    const auto &siblings = parent->getContainedBoxes();
    const int boxIndex = siblings.indexOf(box);
    if (boxIndex < 0 || boxIndex + 1 >= siblings.count()) {
        return nullptr;
    }
    return siblings.at(boxIndex + 1);
}

int matteIndexForTrackMatte(BoundingBox *box)
{
    if (!box || !box->getTrackMatteTarget()) {
        return 0;
    }

    switch (box->getTrackMatteMode()) {
    case TrackMatteMode::alphaMatte:
        return 1;
    case TrackMatteMode::alphaInvertedMatte:
        return 2;
    default:
        return 0;
    }
}

QString titleCaseWords(const QString &text)
{
    const QString simplified = text.simplified();
    if (simplified.isEmpty()) {
        return simplified;
    }

    const QStringList parts = simplified.split(QChar::fromLatin1(' '),
                                               Qt::SkipEmptyParts);
    QStringList titled;
    titled.reserve(parts.size());
    for (QString part : parts) {
        if (part.isEmpty()) {
            continue;
        }
        part[0] = part.at(0).toUpper();
        for (int i = 1; i < part.size(); ++i) {
            part[i] = part.at(i).toLower();
        }
        titled.append(part);
    }
    return titled.join(QChar::fromLatin1(' '));
}

QString aeFriendlyPropertyName(const QString &currentName)
{
    const QString trimmed = currentName.trimmed();
    const QString normalizedName = trimmed.toLower();
    if (normalizedName.isEmpty()) {
        return QString();
    }

    if (normalizedName == QStringLiteral("translation")) {
        return QObject::tr("Position");
    }
    if (normalizedName == QStringLiteral("pivot") ||
        normalizedName == QStringLiteral("anchor") ||
        normalizedName == QStringLiteral("pivot point") ||
        normalizedName == QStringLiteral("anchor point")) {
        return QObject::tr("Anchor Point");
    }
    if (normalizedName == QStringLiteral("scale")) {
        return QObject::tr("Scale");
    }
    if (normalizedName == QStringLiteral("rotation")) {
        return QObject::tr("Rotation");
    }
    if (normalizedName == QStringLiteral("opacity")) {
        return QObject::tr("Opacity");
    }
    if (normalizedName == QStringLiteral("shear")) {
        return QObject::tr("Shear");
    }
    if (normalizedName == QStringLiteral("transform")) {
        return QObject::tr("Transform");
    }
    if (normalizedName == QStringLiteral("raster effects") ||
        normalizedName == QStringLiteral("transform effects")) {
        return QObject::tr("Effects");
    }
    if (normalizedName == QStringLiteral("blend effects")) {
        return QObject::tr("Compositing");
    }
    if (normalizedName == QStringLiteral("path effects") ||
        normalizedName == QStringLiteral("path base effects")) {
        return QObject::tr("Path Effects");
    }
    if (normalizedName == QStringLiteral("fill effects")) {
        return QObject::tr("Fill Effects");
    }
    if (normalizedName == QStringLiteral("outline base effects")) {
        return QObject::tr("Stroke Base Effects");
    }
    if (normalizedName == QStringLiteral("outline effects")) {
        return QObject::tr("Stroke Effects");
    }
    if (normalizedName == QStringLiteral("__ae_layer_masks__")) {
        return QObject::tr("Masks");
    }
    if (normalizedName == QStringLiteral("horizontal radius")) {
        return QObject::tr("Horizontal Radius");
    }
    if (normalizedName == QStringLiteral("vertical radius")) {
        return QObject::tr("Vertical Radius");
    }
    if (normalizedName == QStringLiteral("line width")) {
        return QObject::tr("Line Width");
    }
    if (normalizedName == QStringLiteral("frame remapping")) {
        return QObject::tr("Time Remap");
    }

    bool canHumanize = true;
    for (const QChar ch : trimmed) {
        if (ch == QChar::fromLatin1(' ') || ch == QChar::fromLatin1('_')) {
            continue;
        }
        if (!ch.isLetter() || !ch.isLower()) {
            canHumanize = false;
            break;
        }
    }
    if (canHumanize) {
        QString spaced = trimmed;
        spaced.replace(QChar::fromLatin1('_'), QChar::fromLatin1(' '));
        return titleCaseWords(spaced);
    }

    return trimmed;
}

QString timelineDisplayNameForProperty(Property *prop)
{
    if (!prop) { return QString(); }

    const QString currentName = prop->prp_getName();
    const QString friendlyName = aeFriendlyPropertyName(currentName);

    const bool looksGeneric = currentName.isEmpty() ||
                              currentName.startsWith(QStringLiteral("Object"),
                                                     Qt::CaseInsensitive) ||
                              currentName == QStringLiteral("Image") ||
                              currentName == QStringLiteral("Video") ||
                              currentName == QStringLiteral("Sound") ||
                              currentName == QStringLiteral("Image Sequence");
    if (!looksGeneric) {
        return friendlyName;
    }

    auto mediaNameForPath = [&](const QString &path) {
        if (path.isEmpty()) { return QString(); }
        const QFileInfo info(path);
        const QString baseName = info.completeBaseName();
        if (!baseName.isEmpty()) { return baseName; }
        return info.fileName();
    };

    if (const auto image = enve_cast<ImageBox*>(prop)) {
        const QString mediaName = mediaNameForPath(image->getFilePath());
        if (!mediaName.isEmpty()) { return mediaName; }
        return QObject::tr("Image");
    }
    if (const auto sequence = enve_cast<ImageSequenceBox*>(prop)) {
        const QString mediaName = mediaNameForPath(sequence->getFolderPath());
        if (!mediaName.isEmpty()) { return mediaName; }
        return QObject::tr("Image Sequence");
    }
    if (const auto video = enve_cast<VideoBox*>(prop)) {
        const QString mediaName = mediaNameForPath(video->getFilePath());
        if (!mediaName.isEmpty()) { return mediaName; }
        return QObject::tr("Video");
    }
    if (const auto sound = enve_cast<eIndependentSound*>(prop)) {
        const QString mediaName = mediaNameForPath(sound->getFilePath());
        if (!mediaName.isEmpty()) { return mediaName; }
        return QObject::tr("Sound");
    }

    if (!currentName.trimmed().isEmpty()) {
        return friendlyName;
    }

    if (const auto text = enve_cast<TextBox*>(prop)) {
        Q_UNUSED(text)
        return QObject::tr("Text");
    }
    if (const auto nullObject = enve_cast<NullObject*>(prop)) {
        Q_UNUSED(nullObject)
        return QObject::tr("Null");
    }
    if (const auto container = enve_cast<ContainerBox*>(prop)) {
        return container->isLayer() ? QObject::tr("Layer")
                                    : QObject::tr("Group");
    }
    if (const auto path = enve_cast<PathBox*>(prop)) {
        Q_UNUSED(path)
        return QObject::tr("Shape");
    }

    return QObject::tr("Layer");
}

QStringList propertyPathRelativeToBox(Property *prop, BoundingBox *box)
{
    QStringList path;
    Property *cursor = prop;
    while (cursor && cursor != box) {
        path.prepend(cursor->prp_getName());
        cursor = cursor->getParent<Property>();
    }
    if (cursor != box) {
        path.clear();
    }
    return path;
}

QList<Animator*> animatorsForSelectedBoxesMatchingPath(Animator *sourceAnim)
{
    QList<Animator*> result;
    if (!sourceAnim) {
        return result;
    }

    auto *sourceProp = static_cast<Property*>(sourceAnim);
    auto *sourceScene = sourceProp->getParentScene();
    auto *sourceBox = sourceProp->getFirstAncestor<BoundingBox>();
    if (!sourceScene || !sourceBox || !sourceBox->isSelected()) {
        result.append(sourceAnim);
        return result;
    }

    const auto selectedBoxes = sourceScene->selectedBoxesList();
    if (selectedBoxes.count() < 2) {
        result.append(sourceAnim);
        return result;
    }

    const QStringList relativePath = propertyPathRelativeToBox(sourceProp, sourceBox);
    if (relativePath.isEmpty()) {
        result.append(sourceAnim);
        return result;
    }

    QSet<Animator*> seen;
    for (auto *box : selectedBoxes) {
        if (!box) {
            continue;
        }
        auto *match = enve_cast<Animator*>(box->ca_findPropertyWithPath(0, relativePath));
        if (!match || seen.contains(match)) {
            continue;
        }
        seen.insert(match);
        result.append(match);
    }

    if (result.isEmpty()) {
        result.append(sourceAnim);
    }
    return result;
}

}

#include "GUI/global.h"
#include "GUI/mainwindow.h"
#include "GUI/timelinedockwidget.h"
#include "clipboardcontainer.h"
#include "Timeline/durationrectangle.h"
#include "GUI/coloranimatorbutton.h"
#include "canvas.h"
#include "PathEffects/patheffect.h"
#include "PathEffects/patheffectcollection.h"
#include "Sound/esoundobjectbase.h"

#include "widgets/ecombobox.h"

#include <QApplication>
#include <QDrag>
#include <QMenu>
#include <QInputDialog>
#include <QSignalBlocker>
#include <QColorDialog>
#include <QStatusBar>
#include <QJSEngine>

eComboBox* createCombo(QWidget* const parent)
{
    const auto result = new eComboBox(parent);
    result->setWheelMode(eComboBox::WheelMode::enabledWithCtrl);
    result->setFocusPolicy(Qt::NoFocus);
    return result;
}

BoxSingleWidget::BoxSingleWidget(BoxScroller * const parent)
    : SingleWidget(parent)
    , mParent(parent)
{
    mMainLayout = new QHBoxLayout(this);
    setLayout(mMainLayout);
    mMainLayout->setSpacing(0);
    mMainLayout->setContentsMargins(0, 0, 0, 0);
    mMainLayout->setAlignment(Qt::AlignLeft);

    mRecordButton = new PixmapActionButton(this);
    mRecordButton->setPixmapChooser([this]() {
        if (!mTarget) { return static_cast<QPixmap*>(nullptr); }
        const auto target = mTarget->getTarget();
        if (enve_cast<eBoxOrSound*>(target)) {
            return static_cast<QPixmap*>(nullptr);
        } else if (const auto asCAnim = enve_cast<ComplexAnimator*>(target)) {
            if (asCAnim->anim_isRecording() || asCAnim->anim_getKeyOnCurrentFrame()) {
                return BoxSingleWidget::ANIMATOR_RECORDING_ICON;
            } else {
                if (asCAnim->anim_isDescendantRecording()) {
                    return BoxSingleWidget::ANIMATOR_DESCENDANT_RECORDING_ICON;
                }
                return BoxSingleWidget::ANIMATOR_NOT_RECORDING_ICON;
            }
        } else if (const auto asAnim = enve_cast<Animator*>(target)) {
            if (asAnim->anim_getKeyOnCurrentFrame()) {
                return BoxSingleWidget::ANIMATOR_RECORDING_ICON;
            }
            return BoxSingleWidget::ANIMATOR_NOT_RECORDING_ICON;
        }
        return static_cast<QPixmap*>(nullptr);
    });

    mMainLayout->addWidget(mRecordButton);
    connect(mRecordButton, &BoxesListActionButton::pressed,
            this, &BoxSingleWidget::switchRecordingAction);

    mContentButton = new PixmapActionButton(this);
    mContentButton->setPixmapChooser([this]() {
        if (!mTarget) { return static_cast<QPixmap*>(nullptr); }
        if (mTarget->childrenCount() == 0) {
            return static_cast<QPixmap*>(nullptr);
        }
        const auto target = mTarget->getTarget();
        if (enve_cast<eBoxOrSound*>(target)) {
            if (mTarget->contentVisible()) {
                return BoxSingleWidget::BOX_CHILDREN_VISIBLE_ICON;
            }
            return BoxSingleWidget::BOX_CHILDREN_HIDDEN_ICON;
        } else {
            if (mTarget->contentVisible()) {
                return BoxSingleWidget::ANIMATOR_CHILDREN_VISIBLE_ICON;
            }
            return BoxSingleWidget::ANIMATOR_CHILDREN_HIDDEN_ICON;
        }
    });

    mMainLayout->addWidget(mContentButton);
    connect(mContentButton, &BoxesListActionButton::pressed,
            this, &BoxSingleWidget::switchContentVisibleAction);

    mVisibleButton = new PixmapActionButton(this);
    mVisibleButton->setPixmapChooser([this]() {
        if (!mTarget) { return static_cast<QPixmap*>(nullptr); }
        const auto target = mTarget->getTarget();
        if (const auto ebos = enve_cast<eBoxOrSound*>(target)) {
            if (enve_cast<eSound*>(target)) {
                if (ebos->isVisible()) { return BoxSingleWidget::UNMUTED_ICON; }
                return BoxSingleWidget::MUTED_ICON;
            } else if (ebos->isVisible()) { return BoxSingleWidget::VISIBLE_ICON; }
            return BoxSingleWidget::INVISIBLE_ICON;
        } else if (const auto eEff = enve_cast<eEffect*>(target)) {
            if (eEff->isVisible()) { return BoxSingleWidget::VISIBLE_ICON; }
            return BoxSingleWidget::INVISIBLE_ICON;
        } /*else if (enve_cast<GraphAnimator*>(target)) {
            const auto bsvt = static_cast<BoxScroller*>(mParent);
            const auto keysView = bsvt->getKeysView();
            if (keysView) { return BoxSingleWidget::GRAPH_PROPERTY_ICON; }
            return static_cast<QPixmap*>(nullptr);
        }*/
        return static_cast<QPixmap*>(nullptr);
    });

    mMainLayout->addWidget(mVisibleButton);
    connect(mVisibleButton, &BoxesListActionButton::pressed,
            this, &BoxSingleWidget::switchBoxVisibleAction);

    mSoloButton = new QPushButton(tr("S"), this);
    mSoloButton->setObjectName(QStringLiteral("AeTimelineSoloButton"));
    mSoloButton->setCheckable(true);
    mSoloButton->setFlat(true);
    mSoloButton->setFocusPolicy(Qt::NoFocus);
    mSoloButton->setToolTip(tr("Solo layer"));
    mSoloButton->setFixedWidth(qRound(eSizesUI::widget * 0.95));
    mSoloButton->hide();
    mMainLayout->addWidget(mSoloButton);
    connect(mSoloButton, &QPushButton::clicked, this, [this]() {
        if (!mTarget || !mParent) { return; }
        if (const auto target = enve_cast<eBoxOrSound*>(mTarget->getTarget())) {
            mParent->toggleSolo(target);
        }
    });

    mLockedButton = new PixmapActionButton(this);
    mLockedButton->setPixmapChooser([this]() {
        if (!mTarget) { return static_cast<QPixmap*>(nullptr); }
        const auto target = mTarget->getTarget();
        if (const auto box = enve_cast<BoundingBox*>(target)) {
            if (box->isLocked()) { return BoxSingleWidget::LOCKED_ICON; }
            return BoxSingleWidget::UNLOCKED_ICON;
        }
        return static_cast<QPixmap*>(nullptr);
    });

    mMainLayout->addWidget(mLockedButton);
    connect(mLockedButton, &BoxesListActionButton::pressed,
            this, &BoxSingleWidget::switchBoxLockedAction);

    mHwSupportButton = new PixmapActionButton(this);
    mHwSupportButton->setToolTip(tr("Adjust GPU/CPU Processing"));
    mHwSupportButton->setPixmapChooser([this]() {
        if (!mTarget) { return static_cast<QPixmap*>(nullptr); }
        const auto target = mTarget->getTarget();
        if (const auto rEff = enve_cast<RasterEffect*>(target)) {
            if (rEff->instanceHwSupport() == HardwareSupport::cpuOnly) {
                return BoxSingleWidget::C_ICON;
            } else if (rEff->instanceHwSupport() == HardwareSupport::gpuOnly) {
                return BoxSingleWidget::G_ICON;
            }
            return BoxSingleWidget::CG_ICON;
        }
        return static_cast<QPixmap*>(nullptr);
    });

    mMainLayout->addWidget(mHwSupportButton);
    connect(mHwSupportButton, &BoxesListActionButton::pressed, this, [this]() {
        if (!mTarget) { return; }
        const auto target = mTarget->getTarget();
        if (const auto sEff = enve_cast<ShaderEffect*>(target)) { return; }
        if (const auto rEff = enve_cast<RasterEffect*>(target)) {
            rEff->switchInstanceHwSupport();
            Document::sInstance->actionFinished();
        }
    });

    mFillWidget = new QWidget(this);
    mMainLayout->addWidget(mFillWidget);
    mFillWidget->setObjectName("transparentWidget");
    mFillWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *fillLayout = new QHBoxLayout(mFillWidget);
    fillLayout->setContentsMargins(qRound(eSizesUI::widget * 0.35), 0,
                                   qRound(eSizesUI::widget * 0.25), 0);
    fillLayout->setSpacing(0);
    mNameLabel = new QLabel(mFillWidget);
    mNameLabel->setObjectName(QStringLiteral("AeTimelineNameLabel"));
    mNameLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    mNameLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    fillLayout->addWidget(mNameLabel);
    mExpressionEdit = new QLineEdit(mFillWidget);
    mExpressionEdit->setObjectName(QStringLiteral("AeInlineExpressionEdit"));
    mExpressionEdit->setPlaceholderText(tr("Type expression and press Enter"));
    mExpressionEdit->setVisible(false);
    fillLayout->addWidget(mExpressionEdit);
    connect(mExpressionEdit, &QLineEdit::returnPressed,
            this, &BoxSingleWidget::applyInlineExpressionEdit);
    connect(mExpressionEdit, &QLineEdit::editingFinished,
            this, [this]() {
        if (mInlineExpressionEditing && !mExpressionEdit->hasFocus()) {
            applyInlineExpressionEdit();
        }
    });

    mPromoteToLayerButton = new PixmapActionButton(this);
    mPromoteToLayerButton->setToolTip(tr("Promote to Layer"));
    mPromoteToLayerButton->setPixmapChooser([this]() {
        const auto targetGroup = getPromoteTargetGroup();
        if (targetGroup) {
            return BoxSingleWidget::PROMOTE_TO_LAYER_ICON;
        }
        return static_cast<QPixmap*>(nullptr);
    });

    mMainLayout->addWidget(mPromoteToLayerButton);
    connect(mPromoteToLayerButton, &BoxesListActionButton::pressed,
            this, [this]() {
        const auto targetGroup = getPromoteTargetGroup();
        if (targetGroup) {
            targetGroup->promoteToLayer();
            Document::sInstance->actionFinished();
        }
    });

    mValueSlider = new QrealAnimatorValueSlider(nullptr, this);
    mMainLayout->addWidget(mValueSlider, Qt::AlignRight);

    mSecondValueSlider = new QrealAnimatorValueSlider(nullptr, this);
    mMainLayout->addWidget(mSecondValueSlider, Qt::AlignRight);

    mPointValueLabel = new QLabel(this);
    mPointValueLabel->setObjectName(QStringLiteral("AeTimelinePointValueLabel"));
    mPointValueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    mPointValueLabel->hide();
    mMainLayout->addWidget(mPointValueLabel, Qt::AlignRight);

    mColorButton = new ColorAnimatorButton(nullptr, this);
    mMainLayout->addWidget(mColorButton, Qt::AlignRight);
    mColorButton->setFixedHeight(mColorButton->height() - 6);
    mColorButton->setContentsMargins(0, 3, 0, 3);

    mPropertyComboBox = createCombo(this);
    mMainLayout->addWidget(mPropertyComboBox);

    mBlendModeCombo = createCombo(this);
    mMainLayout->addWidget(mBlendModeCombo);
    mBlendModeCombo->setObjectName("blendModeCombo");
    mBlendModeCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    mBlendModeCombo->setMinimumContentsLength(7);
    mBlendModeCombo->setMaximumWidth(qRound(eSizesUI::widget * 6.5));

    for(int modeId = int(SkBlendMode::kSrcOver);
        modeId <= int(SkBlendMode::kLastMode); modeId++) {
        const auto mode = static_cast<SkBlendMode>(modeId);
        mBlendModeCombo->addItem(SkBlendMode_Name(mode), modeId);
    }

    mBlendModeCombo->insertSeparator(8);
    mBlendModeCombo->insertSeparator(14);
    mBlendModeCombo->insertSeparator(21);
    mBlendModeCombo->insertSeparator(25);
    connect(mBlendModeCombo, qOverload<int>(&QComboBox::activated),
            this, &BoxSingleWidget::setCompositionMode);
    mBlendModeCombo->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Minimum);

    mPathBlendModeCombo = createCombo(this);
    mMainLayout->addWidget(mPathBlendModeCombo);
    mPathBlendModeCombo->addItems(QStringList() << "Normal" <<
                                  "Add" << "Remove" << "Remove reverse" <<
                                  "Intersect" << "Exclude" << "Divide");
    connect(mPathBlendModeCombo, qOverload<int>(&QComboBox::activated),
            this, &BoxSingleWidget::setPathCompositionMode);
    mPathBlendModeCombo->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Minimum);

    mFillTypeCombo = createCombo(this);
    mMainLayout->addWidget(mFillTypeCombo);
    mFillTypeCombo->addItems(QStringList() << "Winding" << "Even-odd");
    connect(mFillTypeCombo, qOverload<int>(&QComboBox::activated),
            this, &BoxSingleWidget::setFillType);
    mFillTypeCombo->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Minimum);

    mParentPickWhipButton = new QPushButton(tr("P"), this);
    mParentPickWhipButton->setObjectName("AeTimelineRelationPickWhip");
    mParentPickWhipButton->setFlat(true);
    mParentPickWhipButton->setFocusPolicy(Qt::NoFocus);
    mParentPickWhipButton->setToolTip(tr("Pick-whip parent target from another layer row."));
    mParentPickWhipButton->setFixedWidth(qRound(eSizesUI::widget * 1.1));
    mMainLayout->addWidget(mParentPickWhipButton);
    connect(mParentPickWhipButton, &QPushButton::pressed, this, [this]() {
        const auto box = mTarget ? enve_cast<BoundingBox*>(mTarget->getTarget()) : nullptr;
        if (!box || !mParent) return;
        mParent->beginPickWhip(
                    box,
                    BoxScroller::PickWhipMode::parent,
                    mParentPickWhipButton->mapToGlobal(mParentPickWhipButton->rect().center()));
    });

    mMattePickWhipButton = new QPushButton(tr("T"), this);
    mMattePickWhipButton->setObjectName("AeTimelineRelationPickWhip");
    mMattePickWhipButton->setFlat(true);
    mMattePickWhipButton->setFocusPolicy(Qt::NoFocus);
    mMattePickWhipButton->setToolTip(tr("Pick-whip another layer to use as this layer's track matte source."));
    mMattePickWhipButton->setFixedWidth(qRound(eSizesUI::widget * 1.1));
    mMainLayout->addWidget(mMattePickWhipButton);
    mMattePickWhipButton->hide();
    connect(mMattePickWhipButton, &QPushButton::pressed, this, [this]() {
        const auto box = mTarget ? enve_cast<BoundingBox*>(mTarget->getTarget()) : nullptr;
        if (!box || !mParent) return;
        mParent->beginPickWhip(
                    box,
                    BoxScroller::PickWhipMode::matte,
                    mMattePickWhipButton->mapToGlobal(mMattePickWhipButton->rect().center()));
    });

    mCollapseCheckbox = new BoolPropertyWidget(this);
    mCollapseCheckbox->setToolTip(tr("AE-style collapse transformations switch for a nested composition layer."));
    mMainLayout->addWidget(mCollapseCheckbox);

    mParentLayerCombo = createCombo(this);
    mParentLayerCombo->setObjectName("timelineParentCombo");
    mParentLayerCombo->setToolTip(tr("AE-style parent column for the selected layer."));
    mParentLayerCombo->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Minimum);
    mParentLayerCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    mParentLayerCombo->setMinimumContentsLength(4);
    mParentLayerCombo->setMaximumWidth(qRound(eSizesUI::widget * 4.4));
    mMainLayout->addWidget(mParentLayerCombo);
    connect(mParentLayerCombo, qOverload<int>(&QComboBox::activated),
            this, [this](const int id) {
        if (!mTarget || mUpdatingTimelineRelations) return;
        const auto box = enve_cast<BoundingBox*>(mTarget->getTarget());
        if (!box) return;
        const auto parentData = mParentLayerCombo->itemData(id).value<quintptr>();
        if (parentData == 0) {
            box->clearParentEffectTarget();
        } else {
            const auto parentBox = reinterpret_cast<BoundingBox*>(parentData);
            if (parentBox && parentBox != box) {
                if (boxIsAncestorOf(box, parentBox)) {
                    if (auto *win = MainWindow::sGetInstance()) {
                        if (win->statusBar()) {
                            win->statusBar()->showMessage(
                                tr("AE: Cannot parent a layer to its own child/descendant."),
                                3500);
                        }
                    }
                    syncTimelineRelationControls();
                    return;
                }
                box->setParentEffectTarget(parentBox);
            }
        }
        Document::sInstance->actionFinished();
        syncTimelineRelationControls();
    });

    mTrackMatteCombo = createCombo(this);
    mTrackMatteCombo->setObjectName("timelineTrackMatteCombo");
    mTrackMatteCombo->setToolTip(tr("AE-style track matte mode for this layer. If no matte source is linked yet, the layer above is used by default."));
    mTrackMatteCombo->addItem(tr("None"), -1);
    mTrackMatteCombo->addItem(tr("alpF"), int(TrackMatteMode::alphaMatte));
    mTrackMatteCombo->addItem(tr("alpB"), int(TrackMatteMode::alphaInvertedMatte));
    mTrackMatteCombo->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Minimum);
    mTrackMatteCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    mTrackMatteCombo->setMinimumContentsLength(5);
    mTrackMatteCombo->setMaximumWidth(qRound(eSizesUI::widget * 4.6));
    mMainLayout->addWidget(mTrackMatteCombo);
    connect(mTrackMatteCombo, qOverload<int>(&QComboBox::activated),
            this, [this](const int id) {
        if (!mTarget || mUpdatingTimelineRelations) return;
        const auto box = enve_cast<BoundingBox*>(mTarget->getTarget());
        if (!box) return;
        if (id == 0) {
            box->clearTrackMatte();
            Document::sInstance->actionFinished();
            syncTimelineRelationControls();
            return;
        }

        auto *matteSource = box->getTrackMatteTarget();
        if (!matteSource) {
            matteSource = defaultTrackMatteSource(box);
        }
        if (!matteSource || matteSource == box) {
            if (auto *win = MainWindow::sGetInstance()) {
                if (win->statusBar()) {
                    win->statusBar()->showMessage(
                        tr("No matte source layer is available above this layer. Move the matte source above this layer or use the T whip to pick one."),
                        3500);
                }
            }
            syncTimelineRelationControls();
            return;
        }

        const auto mode = static_cast<TrackMatteMode>(mTrackMatteCombo->itemData(id).toInt());
        box->setTrackMatteTarget(matteSource, mode);
        Document::sInstance->actionFinished();
        syncTimelineRelationControls();
    });

    mPropertyComboBox->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Minimum);
    mBoxTargetWidget = new BoxTargetWidget(this);
    mMainLayout->addWidget(mBoxTargetWidget);

    mCheckBox = new BoolPropertyWidget(this);
    mMainLayout->addWidget(mCheckBox);

    mOverLifeWidget = new ParticleOverLifeWidget(this);
    mMainLayout->addWidget(mOverLifeWidget);
    mOverLifeWidget->hide();

    //eSizesUI::widget.addHalfSpacing(mMainLayout);

    hide();
    connectAppFont(this);
}

ContainerBox* BoxSingleWidget::getPromoteTargetGroup() {
    if(!mTarget) return nullptr;
    const auto target = mTarget->getTarget();
    ContainerBox* targetGroup = nullptr;
    if(const auto box = enve_cast<ContainerBox*>(target)) {
        if(box->isGroup()) targetGroup = box;
    } else if(enve_cast<RasterEffectCollection*>(target) ||
              enve_cast<BlendEffectCollection*>(target)) {
        const auto pTarget = static_cast<Property*>(target);
        const auto parentBox = pTarget->getFirstAncestor<BoundingBox>();
        if(parentBox && parentBox->isGroup()) {
            targetGroup = static_cast<ContainerBox*>(parentBox);
        }
    }
    return targetGroup;
}

void BoxSingleWidget::setCompositionMode(const int id) {
    if(!mTarget) return;
    const auto target = mTarget->getTarget();

    if(const auto boxTarget = enve_cast<BoundingBox*>(target)) {
        const int modeId = mBlendModeCombo->itemData(id).toInt();
        const auto mode = static_cast<SkBlendMode>(modeId);
        boxTarget->setBlendModeSk(mode);
    }
    Document::sInstance->actionFinished();
}

void BoxSingleWidget::setPathCompositionMode(const int id) {
    if(!mTarget) return;
    const auto target = mTarget->getTarget();

    if(const auto pAnim = enve_cast<SmartPathAnimator*>(target)) {
        pAnim->setMode(static_cast<SmartPathAnimator::Mode>(id));
    }
    Document::sInstance->actionFinished();
}

void BoxSingleWidget::setFillType(const int id) {
    if(!mTarget) return;
    const auto target = mTarget->getTarget();

    if(const auto pAnim = enve_cast<SmartPathCollection*>(target)) {
        pAnim->setFillType(static_cast<SkPathFillType>(id));
    }
    Document::sInstance->actionFinished();
}

bool BoxSingleWidget::isTimelineLayerRow() const {
    return mParent && mParent->getKeysView();
}

QRect BoxSingleWidget::timelineColorSwatchRect(const int nameX) const {
    const int swatchSize = qMax(10, eSizesUI::widget/2);
    return QRect(nameX + eSizesUI::widget/6,
                 (height() - swatchSize)/2,
                 swatchSize,
                 swatchSize);
}

void BoxSingleWidget::editTimelineColor(BoundingBox *box) {
    if (!box) return;
    QColor color = QColorDialog::getColor(box->effectiveTimelineColor(),
                                          this,
                                          tr("Select Layer Color"),
                                          QColorDialog::ShowAlphaChannel);
    if (!color.isValid()) return;
    color.setAlpha(255);
    box->setTimelineColor(color);
    Document::sInstance->actionFinished();
}

void BoxSingleWidget::refreshParentLayerCombo(BoundingBox *box) {
    QSignalBlocker blocker(mParentLayerCombo);
    mParentLayerCombo->clear();
    mParentLayerCombo->addItem(tr("None"), QVariant::fromValue<quintptr>(0));

    const auto scene = mParent ? mParent->currentScene() : nullptr;
    if (!scene || !box) {
        return;
    }

    const auto &boxes = scene->getContainedBoxes();
    for (auto *candidate : boxes) {
        if (!candidate || candidate == box) continue;
        if (boxIsAncestorOf(box, candidate)) continue;
        mParentLayerCombo->addItem(timelineDisplayNameForProperty(candidate),
                                   QVariant::fromValue<quintptr>(reinterpret_cast<quintptr>(candidate)));
    }
}

void BoxSingleWidget::syncTimelineRelationControls() {
    const auto box = mTarget ? enve_cast<BoundingBox*>(mTarget->getTarget()) : nullptr;
    if (!box || !isTimelineLayerRow()) {
        return;
    }

    mUpdatingTimelineRelations = true;
    refreshParentLayerCombo(box);

    int parentIndex = 0;
    if (const auto currentParent = box->getParentEffectTarget()) {
        for (int i = 1; i < mParentLayerCombo->count(); ++i) {
            const auto data = mParentLayerCombo->itemData(i).value<quintptr>();
            const auto parentBox = reinterpret_cast<BoundingBox*>(data);
            if (parentBox == currentParent) {
                parentIndex = i;
                break;
            }
        }
    }
    mParentLayerCombo->setCurrentIndex(parentIndex);

    mTrackMatteCombo->setCurrentIndex(matteIndexForTrackMatte(box));
    mUpdatingTimelineRelations = false;
}

void BoxSingleWidget::setComboProperty(ComboBoxProperty* const combo) {
    if(!combo) return mPropertyComboBox->hide();
    mPropertyComboBox->clear();
    mPropertyComboBox->addItems(combo->getValueNames());
    mPropertyComboBox->setCurrentIndex(combo->getCurrentValue());
    mTargetConn << connect(combo, &ComboBoxProperty::valueChanged,
                           mPropertyComboBox, &QComboBox::setCurrentIndex);
    mTargetConn << connect(mPropertyComboBox,
                           qOverload<int>(&QComboBox::activated),
                           this, [combo](const int id) {
        combo->setCurrentValue(id);
        Document::sInstance->actionFinished();
    });
    mPropertyComboBox->show();
}

void BoxSingleWidget::setComboProperty(EnumAnimator* const combo) {
    if(!combo) return mPropertyComboBox->hide();
    mPropertyComboBox->clear();
    mPropertyComboBox->addItems(combo->getValueNames());
    mPropertyComboBox->setCurrentIndex(combo->getCurrentValue());
    mTargetConn << connect(combo, &EnumAnimator::valueNamesChanged,
                           this, [this, combo](const QStringList&) {
        const QSignalBlocker blocker(mPropertyComboBox);
        mPropertyComboBox->clear();
        mPropertyComboBox->addItems(combo->getValueNames());
        mPropertyComboBox->setCurrentIndex(combo->getCurrentValue());
    });
    mTargetConn << connect(combo, &QrealAnimator::baseValueChanged,
                           this, [this, combo](qreal) {
        const QSignalBlocker blocker(mPropertyComboBox);
        mPropertyComboBox->setCurrentIndex(combo->getCurrentValue());
    });
    mTargetConn << connect(mPropertyComboBox,
                           qOverload<int>(&QComboBox::activated),
                           this, [combo](const int id) {
        combo->prp_startTransform();
        combo->setCurrentValue(id);
        combo->prp_finishTransform();
        Document::sInstance->actionFinished();
    });
    mPropertyComboBox->show();
}

Property *BoxSingleWidget::targetProperty() const
{
    return mTarget ? enve_cast<Property*>(mTarget->getTarget()) : nullptr;
}

bool BoxSingleWidget::isTimelineLayerBackgroundRow() const
{
    const auto prop = targetProperty();
    return enve_cast<BoundingBox*>(prop) || enve_cast<eIndependentSound*>(prop);
}

bool BoxSingleWidget::hasExpandedTimelineContent() const
{
    return mTarget && mTarget->contentVisible();
}

bool BoxSingleWidget::isPropertyRowSelected() const
{
    const auto prop = targetProperty();
    return prop && !enve_cast<eBoxOrSound*>(prop) && prop->prp_isSelected();
}

bool BoxSingleWidget::isLayerRowSelected() const
{
    const auto boxTarget = mTarget ? enve_cast<eBoxOrSound*>(mTarget->getTarget()) : nullptr;
    return boxTarget && boxTarget->isSelected();
}

void BoxSingleWidget::handlePropertySelectedChanged(const Property *prop)
{
    if (const auto graph = enve_cast<GraphAnimator*>(prop)) {
        const auto bsvt = static_cast<BoxScroller*>(mParent);
        const auto keysView = bsvt->getKeysView();
        if (keysView) {
            const bool graphSelected = keysView->graphIsSelected(graph);
            const bool isSelected = prop->prp_isSelected();
            if (graphSelected) {
                if (!isSelected) { keysView->graphRemoveViewedAnimator(graph); }
            } else {
                if (isSelected) { keysView->graphAddViewedAnimator(graph); }
            }
            Document::sInstance->actionFinished();
        }
    }
}

ColorAnimator *BoxSingleWidget::getColorTarget() const {
    const auto swt = mTarget->getTarget();
    ColorAnimator * color = nullptr;
    if(const auto ca = enve_cast<ComplexAnimator*>(swt)) {
        color = enve_cast<ColorAnimator*>(swt);
        if(!color) {
            const auto guiProp = ca->ca_getGUIProperty();
            color = enve_cast<ColorAnimator*>(guiProp);
        }
    }
    return color;
}

void BoxSingleWidget::clearAndHideValueAnimators() {
    mValueSlider->setTarget(nullptr);
    mValueSlider->hide();
    mSecondValueSlider->setTarget(nullptr);
    mSecondValueSlider->hide();
    if (mPointValueLabel) {
        mPointValueLabel->hide();
        mPointValueLabel->clear();
    }
}

void BoxSingleWidget::updateCombinedPointValue() {
    if (!mPointValueLabel) return;
    mPointValueLabel->hide();
    mPointValueLabel->clear();
}

void BoxSingleWidget::setTargetAbstraction(SWT_Abstraction *abs) {
    mTargetConn.clear();
    SingleWidget::setTargetAbstraction(abs);
    if(!abs) {
        if (mNameLabel) { mNameLabel->clear(); }
        cancelInlineExpressionEdit();
        return;
    }
    const auto target = abs->getTarget();
    const auto prop = enve_cast<Property*>(target);
    if(!prop) return;
    refreshDisplayName();
    mTargetConn << connect(prop, &SingleWidgetTarget::SWT_changedDisabled,
                           this, qOverload<>(&QWidget::update));
    mTargetConn << connect(prop, &Property::prp_nameChanged,
                           this, qOverload<>(&QWidget::update));
    mTargetConn << connect(prop, &Property::prp_nameChanged,
                           this, [this]() { refreshDisplayName(); });
    if (const auto pointAnimator = enve_cast<QPointFAnimator*>(prop)) {
        mTargetConn << connect(pointAnimator->getXAnimator(), &QrealAnimator::effectiveValueChanged,
                               this, [this](qreal) { updateCombinedPointValue(); });
        mTargetConn << connect(pointAnimator->getYAnimator(), &QrealAnimator::effectiveValueChanged,
                               this, [this](qreal) { updateCombinedPointValue(); });
        mTargetConn << connect(pointAnimator->getXAnimator(), &QrealAnimator::baseValueChanged,
                               this, [this](qreal) { updateCombinedPointValue(); });
        mTargetConn << connect(pointAnimator->getYAnimator(), &QrealAnimator::baseValueChanged,
                               this, [this](qreal) { updateCombinedPointValue(); });
    }

    const auto boolProperty = enve_cast<BoolProperty*>(prop);
    const auto boolPropertyContainer = enve_cast<BoolPropertyContainer*>(prop);
    const auto boxTargetProperty = enve_cast<BoxTargetProperty*>(prop);
    const auto comboBoxProperty = enve_cast<ComboBoxProperty*>(prop);
    const auto enumAnimator = enve_cast<EnumAnimator*>(prop);
    const auto boolAnimator = enve_cast<BoolAnimator*>(prop);
    const auto animator = enve_cast<Animator*>(prop);
    const auto graphAnimator = enve_cast<GraphAnimator*>(prop);
    const auto smartPathAnimator = enve_cast<SmartPathAnimator*>(prop);
    const auto complexAnimator = enve_cast<ComplexAnimator*>(prop);
    const auto colorAnimator = enve_cast<ColorAnimator*>(prop);
    const auto pointAnimator = enve_cast<QPointFAnimator*>(prop);
    const auto eboxOrSound = enve_cast<eBoxOrSound*>(prop);
    const auto eindependentSound = enve_cast<eIndependentSound*>(prop);
    const auto eeffect = enve_cast<eEffect*>(prop);
    const auto rasterEffect = enve_cast<RasterEffect*>(prop);
    const auto boundingBox = enve_cast<BoundingBox*>(prop);
    mMainLayout->setContentsMargins(0, 0, boundingBox ? 0 : 5, 0);
    mContentButton->setVisible(complexAnimator && !colorAnimator);
    mRecordButton->setVisible(animator && !eboxOrSound &&
                              !enve_cast<ParticleOverLifeAnimator*>(prop));
    mVisibleButton->setVisible(eboxOrSound || eeffect ||
                               (graphAnimator && !smartPathAnimator));
    mLockedButton->setVisible(boundingBox);
    mHwSupportButton->setVisible(rasterEffect);
    {
        const auto targetGroup = getPromoteTargetGroup();
        if(boundingBox && targetGroup) {
            mTargetConn << connect(targetGroup,
                                   &ContainerBox::switchedGroupLayer,
                                   this, [this](const eBoxType type) {
                mBlendModeCombo->setEnabled(type == eBoxType::layer);
            });
        }
        mPromoteToLayerButton->setVisible(targetGroup);
        if(targetGroup) {
            mTargetConn << connect(targetGroup, &ContainerBox::switchedGroupLayer,
                                   this, [this](const eBoxType type) {
                mPromoteToLayerButton->setVisible(type == eBoxType::group);
            });
        }
    }
    bool boxTargetWidgetVisible = boxTargetProperty;
    bool checkBoxVisible = boolProperty || boolPropertyContainer || boolAnimator;
    bool propertyComboVisible = comboBoxProperty || enumAnimator;
    cancelInlineExpressionEdit();

    mPathBlendModeVisible = false;
    mBlendModeVisible = false;
    mFillTypeVisible = false;
    mTimelineParentVisible = false;
    mTimelineMatteVisible = false;
    mTimelineCollapseVisible = false;
    mParentPickWhipButton->hide();
    mMattePickWhipButton->hide();
    mCollapseCheckbox->hide();
    mSoloButton->hide();
    mSelected = false;

    mColorButton->setColorTarget(nullptr);
    mValueSlider->setTarget(nullptr);
    mSecondValueSlider->setTarget(nullptr);
    mOverLifeWidget->setTarget(nullptr);
    mPointValueLabel->hide();
    mPointValueLabel->clear();

    bool valueSliderVisible = false;
    bool secondValueSliderVisible = false;
    bool colorButtonVisible = false;
    bool overLifeWidgetVisible = false;

    if(boundingBox) {
        if (isTimelineLayerRow()) {
            mBlendModeVisible = true;
            mTimelineParentVisible = true;
            mTimelineMatteVisible = true;
            if (const auto linkCanvas = enve_cast<InternalLinkCanvas*>(boundingBox)) {
                mTimelineCollapseVisible = true;
                mCollapseCheckbox->setTarget(linkCanvas->clipToCanvasProperty());
                mCollapseCheckbox->setPlaceholderCrossVisible(false);
            } else {
                mTimelineCollapseVisible = true;
                mCollapseCheckbox->setTarget(static_cast<BoolProperty*>(nullptr));
                mCollapseCheckbox->setPlaceholderCrossVisible(true);
            }
            mSoloButton->show();
            syncTimelineRelationControls();
        } else {
            mBlendModeVisible = true;
        }
        const auto blendName = SkBlendMode_Name(boundingBox->getBlendMode());
        mBlendModeCombo->setCurrentText(blendName);
        mBlendModeCombo->setEnabled(!boundingBox->isGroup());
        mTargetConn << connect(boundingBox, &BoundingBox::blendModeChanged,
                               this, [this, boundingBox](const SkBlendMode mode) {
            mBlendModeCombo->setCurrentText(SkBlendMode_Name(mode));
            Q_UNUSED(mode)
            if (boundingBox && isTimelineLayerRow()) {
                syncTimelineRelationControls();
            }
        });
        mTargetConn << connect(boundingBox, &BoundingBox::blendEffectChanged,
                               this, [this, boundingBox]() {
            if (boundingBox && isTimelineLayerRow()) {
                syncTimelineRelationControls();
            }
        });
        mTargetConn << connect(boundingBox, &BoundingBox::timelineColorChanged,
                               this, qOverload<>(&QWidget::update));
        mTargetConn << connect(boundingBox->getTransformAnimator(),
                               &BoxTransformAnimator::inheritedTransformChanged,
                               this, [this, boundingBox](const UpdateReason) {
            if (boundingBox && isTimelineLayerRow()) {
                syncTimelineRelationControls();
                refreshDisplayName();
                update();
            }
        });
        mTargetConn << connect(boundingBox, &Property::prp_nameChanged,
                               this, [this, boundingBox]() {
            refreshDisplayName();
            if (boundingBox && isTimelineLayerRow()) {
                syncTimelineRelationControls();
            }
        });
    } else if(enve_cast<eSoundObjectBase*>(prop)) {
    } else if(boolProperty) {
        mCheckBox->setTarget(boolProperty);
        checkBoxVisible = true;
        mTargetConn << connect(boolProperty, &BoolProperty::valueChanged,
                               this, [this]() { mCheckBox->update(); });
    } else if(boolPropertyContainer) {
        mCheckBox->setTarget(boolPropertyContainer);
        checkBoxVisible = true;
        mTargetConn << connect(boolPropertyContainer,
                               &BoolPropertyContainer::valueChanged,
                               this, [this]() { mCheckBox->update(); });
    } else if(boolAnimator) {
        mCheckBox->setTarget(boolAnimator);
        checkBoxVisible = true;
        mTargetConn << connect(boolAnimator, &QrealAnimator::baseValueChanged,
                               this, [this](qreal) { mCheckBox->update(); });
    } else if(comboBoxProperty) {
        propertyComboVisible = true;
        setComboProperty(comboBoxProperty);
    } else if(enumAnimator) {
        propertyComboVisible = true;
        setComboProperty(enumAnimator);
    } else if(const auto qra = enve_cast<QrealAnimator*>(prop)) {
        if (enve_cast<ParticleOverLifeAnimator*>(qra)) {
            mOverLifeWidget->setTarget(qra);
            overLifeWidgetVisible = true;
        } else {
            mValueSlider->setTarget(qra);
            valueSliderVisible = true;
            mValueSlider->setIsLeftSlider(false);
        }
    } else if(complexAnimator) {
        if(const auto col = enve_cast<ColorAnimator*>(prop)) {
            colorButtonVisible = true;
            mColorButton->setColorTarget(col);
        } else if(const auto coll = enve_cast<SmartPathCollection*>(prop)) {
            mFillTypeVisible = true;
            mFillTypeCombo->setCurrentIndex(static_cast<int>(coll->getFillType()));
            mTargetConn << connect(coll, &SmartPathCollection::fillTypeChanged,
                                   this, [this](const SkPathFillType type) {
                mFillTypeCombo->setCurrentIndex(static_cast<int>(type));
            });
        }
        const bool isTransformAnimator =
                enve_cast<BasicTransformAnimator*>(prop) ||
                enve_cast<AdvancedTransformAnimator*>(prop);
        if (pointAnimator) {
            updateValueSlidersForQPointFAnimator();
            valueSliderVisible = mValueSlider->isVisible();
            secondValueSliderVisible = mSecondValueSlider->isVisible();
        } else if(complexAnimator && !abs->contentVisible() && !isTransformAnimator) {
            {
                const auto guiProp = complexAnimator->ca_getGUIProperty();
                if(const auto qra = enve_cast<QrealAnimator*>(guiProp)) {
                    if (enve_cast<ParticleOverLifeAnimator*>(qra)) {
                        mOverLifeWidget->setTarget(qra);
                        overLifeWidgetVisible = true;
                    } else {
                        valueSliderVisible = true;
                        mValueSlider->setTarget(qra);
                        mValueSlider->setIsLeftSlider(false);
                        mSecondValueSlider->setTarget(nullptr);
                    }
                } else if(const auto col = enve_cast<ColorAnimator*>(guiProp)) {
                    mColorButton->setColorTarget(col);
                    colorButtonVisible = true;
                } else if(const auto combo = enve_cast<ComboBoxProperty*>(guiProp)) {
                    propertyComboVisible = true;
                    setComboProperty(combo);
                } else if(const auto combo = enve_cast<EnumAnimator*>(guiProp)) {
                    propertyComboVisible = true;
                    setComboProperty(combo);
                } else if(const auto boxTarget = enve_cast<BoxTargetProperty*>(guiProp)) {
                    mBoxTargetWidget->setTargetProperty(boxTarget);
                    boxTargetWidgetVisible = true;
                } else if(const auto boolProp = enve_cast<BoolProperty*>(guiProp)) {
                    mCheckBox->setTarget(boolProp);
                    checkBoxVisible = true;
                } else if(const auto boolAnim = enve_cast<BoolAnimator*>(guiProp)) {
                    mCheckBox->setTarget(boolAnim);
                    checkBoxVisible = true;
                } else if(const auto boolContainer = enve_cast<BoolPropertyContainer*>(guiProp)) {
                    mCheckBox->setTarget(boolContainer);
                    checkBoxVisible = true;
                }
            }
        }
    } else if(boxTargetProperty) {
        mBoxTargetWidget->setTargetProperty(boxTargetProperty);
        boxTargetWidgetVisible = true;
    } else if(const auto path = enve_cast<SmartPathAnimator*>(prop)) {
        mPathBlendModeVisible = true;
        mPathBlendModeCombo->setCurrentIndex(static_cast<int>(path->getMode()));
        mTargetConn << connect(path, &SmartPathAnimator::pathBlendModeChagned,
                               this, [this](const SmartPathAnimator::Mode mode) {
            mPathBlendModeCombo->setCurrentIndex(static_cast<int>(mode));
        });
    }

    if(animator) {
        mTargetConn << connect(animator, &Animator::anim_isRecordingChanged,
                               this, [this]() { mRecordButton->update(); });
        mTargetConn << connect(animator, &Animator::anim_changedKeyOnCurrentFrame,
                               this, [this]() { mRecordButton->update(); });
    }
    if(eeffect) {
        if(rasterEffect) {
            mTargetConn << connect(rasterEffect, &RasterEffect::hardwareSupportChanged,
                                   this, [this]() { mHwSupportButton->update(); });
        }

        mTargetConn << connect(eeffect, &eEffect::effectVisibilityChanged,
                               this, [this]() { mVisibleButton->update(); });
    }
    if(boundingBox || eindependentSound) {
        const auto ptr = static_cast<eBoxOrSound*>(prop);
        mTargetConn << connect(ptr, &eBoxOrSound::visibilityChanged,
                               this, [this]() { mVisibleButton->update(); });
        mTargetConn << connect(ptr, &eBoxOrSound::selectionChanged,
                               this, qOverload<>(&QWidget::update));
        mTargetConn << connect(ptr, &eBoxOrSound::lockedChanged,
                               this, [this]() { mLockedButton->update(); });
    }
    if(!boundingBox && !eindependentSound) {
        mTargetConn << connect(prop, &Property::prp_selectionChanged,
                               this, qOverload<>(&QWidget::update));
        mTargetConn << connect(prop, &Property::prp_selectionChanged,
                               this, [this, prop]() { handlePropertySelectedChanged(prop); });
    }

    mValueSlider->setVisible(valueSliderVisible);
    mSecondValueSlider->setVisible(secondValueSliderVisible);
    mOverLifeWidget->setVisible(overLifeWidgetVisible);
    mColorButton->setVisible(colorButtonVisible);
    mBoxTargetWidget->setVisible(boxTargetWidgetVisible);
    mCheckBox->setVisible(checkBoxVisible);
    mPropertyComboBox->setVisible(propertyComboVisible);
    updateCombinedPointValue();

    updateCompositionBoxVisible();
    updatePathCompositionBoxVisible();
    updateFillTypeBoxVisible();
    updateTimelineRelationCombosVisible();
    updatePickWhipButtonsVisible();
}

void BoxSingleWidget::refreshDisplayName()
{
    if (!mNameLabel) { return; }
    const auto prop = mTarget ? enve_cast<Property*>(mTarget->getTarget()) : nullptr;
    if (!prop) {
        mNameLabel->clear();
        return;
    }
    if (auto *layout = qobject_cast<QHBoxLayout*>(mFillWidget ? mFillWidget->layout() : nullptr)) {
        const bool layerRow = enve_cast<BoundingBox*>(prop) && isTimelineLayerRow();
        layout->setContentsMargins(layerRow ? qRound(eSizesUI::widget * 1.45)
                                            : qRound(eSizesUI::widget * 0.35),
                                   0,
                                   qRound(eSizesUI::widget * 0.25),
                                   0);
    }
    auto font = mNameLabel->font();
    font.setBold(enve_cast<BoundingBox*>(prop) || enve_cast<eIndependentSound*>(prop));
    mNameLabel->setFont(font);
    mNameLabel->setText(QFontMetrics(font).elidedText(
        timelineDisplayNameForProperty(prop),
        Qt::ElideRight,
        qMax(40, mFillWidget ? mFillWidget->width() - 4 : width()/2)));
}

void BoxSingleWidget::beginInlineExpressionEdit()
{
    const auto anim = mTarget ? enve_cast<QrealAnimator*>(mTarget->getTarget()) : nullptr;
    if (!anim || !mExpressionEdit || !mNameLabel) { return; }

    mInlineExpressionEditing = true;
    mNameLabel->hide();
    mExpressionEdit->setVisible(true);
    mExpressionEdit->setText(anim->hasExpression() ? anim->getExpressionScriptString()
                                                   : QString());
    mExpressionEdit->setFocus(Qt::OtherFocusReason);
    mExpressionEdit->selectAll();
}

void BoxSingleWidget::cancelInlineExpressionEdit()
{
    if (!mExpressionEdit || !mNameLabel) { return; }
    mInlineExpressionEditing = false;
    mExpressionEdit->hide();
    mNameLabel->show();
    refreshDisplayName();
}

void BoxSingleWidget::applyInlineExpressionEdit()
{
    const auto anim = mTarget ? enve_cast<QrealAnimator*>(mTarget->getTarget()) : nullptr;
    if (!anim || !mExpressionEdit) {
        cancelInlineExpressionEdit();
        return;
    }

    QString script = mExpressionEdit->text().trimmed();
    if (script.isEmpty()) {
        anim->clearExpressionAction();
        cancelInlineExpressionEdit();
        Document::sInstance->actionFinished();
        return;
    }

    if (!script.contains(QStringLiteral("return")) &&
        !script.contains(QLatin1Char(';')) &&
        !script.contains(QLatin1Char('\n'))) {
        script = QStringLiteral("return %1;").arg(script);
    }

    const QString bindings = QStringLiteral(
        "fps = $scene.fps;\n"
        "frame = $frame;\n"
        "value = $value;\n");

    try {
        auto expr = Expression::sCreate(bindings,
                                        QString(),
                                        script,
                                        anim,
                                        Expression::sQrealAnimatorTester);
        anim->setExpressionAction(expr);
        if (auto *win = MainWindow::sGetInstance()) {
            if (win->statusBar()) {
                win->statusBar()->showMessage(tr("Expression applied."), 2500);
            }
        }
    } catch (const std::exception &e) {
        if (auto *win = MainWindow::sGetInstance()) {
            if (win->statusBar()) {
                win->statusBar()->showMessage(QString::fromUtf8(e.what()), 5000);
            }
        }
        mExpressionEdit->setFocus(Qt::OtherFocusReason);
        mExpressionEdit->selectAll();
        return;
    }

    cancelInlineExpressionEdit();
    Document::sInstance->actionFinished();
}

void BoxSingleWidget::loadStaticPixmaps(int iconSize)
{
    if (sStaticPixmapsLoaded) { return; }
    if (!ThemeSupport::hasIconSize(iconSize)) {
        QMessageBox::warning(nullptr,
                             tr("Icon issues"),
                             tr("<p>Requested icon size <b>%1</b> is not available,"
                                " expect blurry icons.</p>"
                                "<p>Note that this may happen if you change the display scaling"
                                " in Windows without restarting."
                                " If you still have issues after restarting please report this issue.</p>").arg(iconSize));
    }
    const auto pixmapSize = ThemeSupport::getIconSize(iconSize);
    qDebug() << "pixmaps size" << pixmapSize;
    VISIBLE_ICON = new QPixmap(QIcon::fromTheme("visible").pixmap(pixmapSize));
    INVISIBLE_ICON = new QPixmap(QIcon::fromTheme("hidden").pixmap(pixmapSize));
    BOX_CHILDREN_VISIBLE_ICON = new QPixmap(QIcon::fromTheme("visible-child").pixmap(pixmapSize));
    BOX_CHILDREN_HIDDEN_ICON = new QPixmap(QIcon::fromTheme("hidden-child").pixmap(pixmapSize));
    ANIMATOR_CHILDREN_VISIBLE_ICON = new QPixmap(QIcon::fromTheme("visible-child-small").pixmap(pixmapSize));
    ANIMATOR_CHILDREN_HIDDEN_ICON = new QPixmap(QIcon::fromTheme("hidden-child-small").pixmap(pixmapSize));
    LOCKED_ICON = new QPixmap(QIcon::fromTheme("locked").pixmap(pixmapSize));
    UNLOCKED_ICON = new QPixmap(QIcon::fromTheme("unlocked").pixmap(pixmapSize));
    MUTED_ICON = new QPixmap(QIcon::fromTheme("muted").pixmap(pixmapSize));
    UNMUTED_ICON = new QPixmap(QIcon::fromTheme("unmuted").pixmap(pixmapSize));
    ANIMATOR_RECORDING_ICON = new QPixmap(QIcon::fromTheme("record").pixmap(pixmapSize));
    ANIMATOR_NOT_RECORDING_ICON = new QPixmap(QIcon::fromTheme("norecord").pixmap(pixmapSize));
    ANIMATOR_DESCENDANT_RECORDING_ICON = new QPixmap(QIcon::fromTheme("record-child").pixmap(pixmapSize));
    C_ICON = new QPixmap(QIcon::fromTheme("cpu-active").pixmap(pixmapSize));
    G_ICON = new QPixmap(QIcon::fromTheme("gpu-active").pixmap(pixmapSize));
    CG_ICON = new QPixmap(QIcon::fromTheme("cpu-gpu").pixmap(pixmapSize));
    GRAPH_PROPERTY_ICON = new QPixmap(QIcon::fromTheme("graph_property_2").pixmap(pixmapSize));
    PROMOTE_TO_LAYER_ICON = new QPixmap(QIcon::fromTheme("layer").pixmap(pixmapSize));

    sStaticPixmapsLoaded = true;
}

void BoxSingleWidget::clearStaticPixmaps()
{
    if (!sStaticPixmapsLoaded) { return; }

    delete VISIBLE_ICON;
    delete INVISIBLE_ICON;
    delete BOX_CHILDREN_VISIBLE_ICON;
    delete BOX_CHILDREN_HIDDEN_ICON;
    delete ANIMATOR_CHILDREN_VISIBLE_ICON;
    delete ANIMATOR_CHILDREN_HIDDEN_ICON;
    delete LOCKED_ICON;
    delete UNLOCKED_ICON;
    delete MUTED_ICON;
    delete UNMUTED_ICON;
    delete ANIMATOR_RECORDING_ICON;
    delete ANIMATOR_NOT_RECORDING_ICON;
    delete ANIMATOR_DESCENDANT_RECORDING_ICON;
    delete PROMOTE_TO_LAYER_ICON;
    delete C_ICON;
    delete G_ICON;
    delete CG_ICON;
    delete GRAPH_PROPERTY_ICON;
}

void BoxSingleWidget::mousePressEvent(QMouseEvent *event) {
    if(!mTarget) return;
    for (QWidget *p = parentWidget(); p; p = p->parentWidget()) {
        if (auto *timeline = qobject_cast<TimelineWidget*>(p)) {
            timeline->setFocus(Qt::MouseFocusReason);
            break;
        }
    }
    const auto previewState = RenderHandler::sInstance->currentPreviewState();
    if (previewState == PreviewState::playing) {
        RenderHandler::sInstance->interruptPreview();
    }
    const auto bbox = enve_cast<BoundingBox*>(mTarget->getTarget());
    if (bbox && isTimelineLayerRow()) {
        int nameX = mFillWidget->x() + eSizesUI::widget/4;
        if (timelineColorSwatchRect(nameX).contains(event->pos()) &&
            event->button() == Qt::RightButton) {
            bbox->clearTimelineColor();
            Document::sInstance->actionFinished();
            return;
        }
    }
    if(event->x() < mFillWidget->x() ||
       event->x() > mFillWidget->x() + mFillWidget->width()) return;
    const auto target = mTarget->getTarget();
    if(event->button() == Qt::RightButton) {
        setSelected(true);
        QMenu menu(this);

        if(const auto pTarget = enve_cast<Property*>(target)) {
            const bool shiftPressed = event->modifiers() & Qt::ShiftModifier;
            const auto bbox = enve_cast<BoundingBox*>(target);
            if (bbox && isTimelineLayerRow()) {
                bool handled = false;
                if (!bbox->isSelected()) {
                    for (QWidget *p = parentWidget(); p; p = p->parentWidget()) {
                        if (auto *timeline = qobject_cast<TimelineWidget*>(p)) {
                            handled = timeline->handleTimelineLayerSelection(
                                        bbox, event->modifiers());
                            break;
                        }
                    }
                }
                if (!handled && !bbox->isSelected()) {
                    bbox->selectionChangeTriggered(shiftPressed);
                }
            } else if(enve_cast<BoundingBox*>(target) || enve_cast<eIndependentSound*>(target)) {
                const auto box = static_cast<eBoxOrSound*>(target);
                if(!box->isSelected()) box->selectionChangeTriggered(shiftPressed);
            } else {
                if(!pTarget->prp_isSelected()) pTarget->prp_selectionChangeTriggered(shiftPressed);
            }
            PropertyMenu pMenu(&menu, mParent->currentScene(), MainWindow::sGetInstance());
            pTarget->prp_setupTreeViewMenu(&pMenu);
        }
        menu.exec(event->globalPos());
        setSelected(false);
    } else {
        mDragPressPos = event->pos().x() > mFillWidget->x();
        mDragStartPos = event->pos();
        if (event->button() == Qt::LeftButton && bbox && isTimelineLayerRow() &&
            mParent && !bbox->isSelected()) {
            mParent->beginLayerRectSelection(mapToGlobal(event->pos()),
                                             event->modifiers());
        }
    }
    Document::sInstance->actionFinished();
}

void BoxSingleWidget::mouseMoveEvent(QMouseEvent *event) {
    if(!mTarget) return;
    if (mParent && mParent->hasPendingPickWhip()) {
        const auto bbox = enve_cast<BoundingBox*>(mTarget->getTarget());
        QRect hoverRect;
        if (bbox && isTimelineLayerRow()) {
            hoverRect = QRect(mapToGlobal(QPoint(0, 0)), size());
        }
        mParent->updatePickWhipPointer(mapToGlobal(event->pos()), bbox, hoverRect);
        return;
    }
    if(!mDragPressPos) return;
    if(!(event->buttons() & Qt::LeftButton)) return;
    if (mParent && mParent->updateLayerRectSelection(mapToGlobal(event->pos()))) {
        return;
    }
    const auto dist = (event->pos() - mDragStartPos).manhattanLength();
    if(dist < QApplication::startDragDistance()) return;
    if (mParent) {
        mParent->cancelLayerRectSelection();
    }
    const auto drag = new QDrag(this);
    {
        const auto prop = static_cast<Property*>(mTarget->getTarget());
        const QString name = prop->prp_getName();
        const int nameWidth = QApplication::fontMetrics().horizontalAdvance(name);
        QPixmap pixmap(mFillWidget->x() + nameWidth + eSizesUI::widget, height());
        render(&pixmap);
        drag->setPixmap(pixmap);
    }
    connect(drag, &QDrag::destroyed, this, &BoxSingleWidget::clearSelected);

    const auto mimeData = mTarget->getTarget()->SWT_createMimeData();
    if(!mimeData) return;
    setSelected(true);
    drag->setMimeData(mimeData);

    drag->installEventFilter(MainWindow::sGetInstance());
    drag->exec(Qt::CopyAction | Qt::MoveAction);
}

void BoxSingleWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (!mTarget) { return; }
    const auto target = mTarget->getTarget();

    const auto bbox = enve_cast<BoundingBox*>(target);
    if (bbox && isTimelineLayerRow()) {
        const int nameX = mFillWidget->x() + eSizesUI::widget/4;
        if (timelineColorSwatchRect(nameX).contains(event->pos()) &&
            event->button() == Qt::LeftButton) {
            editTimelineColor(bbox);
            return;
        }
    }
    if (event->button() == Qt::MidButton && bbox) {
        PropertyNameDialog::sRenameBox(bbox, this);
        return;
    }

    if (event->x() < mFillWidget->x() ||
        event->x() > mFillWidget->x() + mFillWidget->width()) { return; }
    setSelected(false);
    if (bbox && isTimelineLayerRow() && mParent &&
        mParent->finishLayerRectSelection(mapToGlobal(event->pos()))) {
        return;
    }

    if (pointToLen(event->pos() - mDragStartPos) > eSizesUI::widget/2) { return; }

    const bool shiftPressed = event->modifiers() & Qt::ShiftModifier;
    if (const auto bbox = enve_cast<BoundingBox*>(target)) {
        if (mParent && mParent->handlePickWhipTarget(bbox)) {
            return;
        }
        if (isTimelineLayerRow()) {
            for (QWidget *p = parentWidget(); p; p = p->parentWidget()) {
                if (auto *timeline = qobject_cast<TimelineWidget*>(p)) {
                    if (timeline->handleTimelineLayerSelection(bbox, event->modifiers())) {
                        Document::sInstance->actionFinished();
                        return;
                    }
                }
            }
        }
    }
    if (enve_cast<BoundingBox*>(target) || enve_cast<eIndependentSound*>(target)) {
        const auto boxTarget = static_cast<eBoxOrSound*>(target);
        boxTarget->selectionChangeTriggered(shiftPressed);
        Document::sInstance->actionFinished();
    } else if (const auto pTarget = enve_cast<Property*>(target)) {
        pTarget->prp_selectionChangeTriggered(shiftPressed);
    }
}

void BoxSingleWidget::enterEvent(QEvent *)
{
#ifdef Q_OS_MAC
    setFocus();
#endif
    mHover = true;
    if (mParent && mParent->hasPendingPickWhip()) {
        if (const auto bbox = mTarget ? enve_cast<BoundingBox*>(mTarget->getTarget()) : nullptr) {
            if (isTimelineLayerRow()) {
                mParent->updatePickWhipPointer(QCursor::pos(),
                                               bbox,
                                               QRect(mapToGlobal(QPoint(0, 0)), size()));
            }
        }
    }
    update();
}

void BoxSingleWidget::leaveEvent(QEvent *)
{
#ifdef Q_OS_MAC
    KeyFocusTarget::KFT_sSetLastTarget();
#endif
    mHover = false;
    if (mParent && mParent->hasPendingPickWhip()) {
        if (const auto bbox = mTarget ? enve_cast<BoundingBox*>(mTarget->getTarget()) : nullptr) {
            mParent->clearPickWhipHover(bbox);
        }
    }
    update();
}

#ifdef Q_OS_MAC
void BoxSingleWidget::keyPressEvent(QKeyEvent *event)
{
    if (mHover) {
        MainWindow::sGetInstance()->processBoxesListKeyEvent(event);
    }
    SingleWidget::keyPressEvent(event);
}
#endif

void BoxSingleWidget::mouseDoubleClickEvent(QMouseEvent *e)
{
    Q_UNUSED(e)
    if (isTimelineLayerRow() && mTarget) {
        if (const auto link = enve_cast<InternalLinkCanvas*>(mTarget->getTarget())) {
            if (auto *timeline = MainWindow::sGetInstance()
                                     ? MainWindow::sGetInstance()->getTimeLineWidget()
                                     : nullptr) {
                if (const auto targetScene = enve_cast<Canvas*>(link->getFinalTarget())) {
                    if (auto *tw = timeline->currentTimelineWidget()) {
                        tw->setCurrentScene(targetScene);
                    }
                    if (auto *window = MainWindow::sGetInstance()) {
                        window->activateSceneWorkspace(targetScene);
                    } else {
                        Document::sInstance->setActiveScene(targetScene);
                    }
                    return;
                }
            }
        }
        if (const auto group = enve_cast<ContainerBox*>(mTarget->getTarget())) {
            if (auto *timeline = MainWindow::sGetInstance()
                                     ? MainWindow::sGetInstance()->getTimeLineWidget()
                                     : nullptr) {
                if (auto *tw = timeline->currentTimelineWidget()) {
                    if (tw->enterGroup(group)) {
                        return;
                    }
                }
            }
        }
    }
}

void BoxSingleWidget::prp_drawTimelineControls(QPainter * const p,
                               const qreal pixelsPerFrame,
                               const FrameRange &viewedFrames) {
    if(isHidden() || !mTarget) return;
    const auto target = mTarget->getTarget();
    if(const auto asAnim = enve_cast<Animator*>(target)) {
        asAnim->prp_drawTimelineControls(
                    p, pixelsPerFrame, viewedFrames, eSizesUI::widget);
    }
}

Key* BoxSingleWidget::getKeyAtPos(const int pressX,
                                  const qreal pixelsPerFrame,
                                  const int minViewedFrame) {
    if(isHidden() || !mTarget) return nullptr;
    const auto target = mTarget->getTarget();
    if(const auto asAnim = enve_cast<Animator*>(target)) {
        return asAnim->anim_getKeyAtPos(pressX, minViewedFrame,
                                        pixelsPerFrame, KEY_RECT_SIZE);
    }
    return nullptr;
}

TimelineMovable* BoxSingleWidget::getRectangleMovableAtPos(
                            const int pressX,
                            const qreal pixelsPerFrame,
                            const int minViewedFrame) {
    if(isHidden() || !mTarget) return nullptr;
    const auto target = mTarget->getTarget();
    if(const auto asAnim = enve_cast<Animator*>(target)) {
        return asAnim->anim_getTimelineMovable(
                    pressX, minViewedFrame, pixelsPerFrame);
    }
    return nullptr;
}

void BoxSingleWidget::getKeysInRect(const QRectF &selectionRect,
                                    const qreal pixelsPerFrame,
                                    QList<Key*>& listKeys) {
    if(isHidden() || !mTarget) return;
    const auto target = mTarget->getTarget();
    if(const auto asAnim = enve_cast<Animator*>(target)) {
        asAnim->anim_getKeysInRect(selectionRect, pixelsPerFrame,
                                   listKeys, KEY_RECT_SIZE);
    }
}

void BoxSingleWidget::paintEvent(QPaintEvent *) {
    if(!mTarget) return;
    QPainter p(this);
    const auto target = mTarget->getTarget();
    const auto prop = enve_cast<Property*>(target);
    if(!prop) return;
    if(prop->SWT_isDisabled()) p.setOpacity(.5);

    int nameX = mFillWidget->x();
    const auto bsTarget = enve_cast<eBoxOrSound*>(prop);
    const bool propertySelected = !bsTarget && prop->prp_isSelected();
    const bool layerSelected = bsTarget && bsTarget->isSelected();
    const bool rowSelected = mSelected || layerSelected || propertySelected;
    const bool isLayerRow = enve_cast<BoundingBox*>(prop) ||
                            enve_cast<eIndependentSound*>(prop);
    if (bsTarget && mSoloButton) {
        mSoloButton->setChecked(mParent && mParent->isSolo(bsTarget));
    }

    QColor trackAccent = ThemeSupport::getThemeHighlightColor();
    if (enve_cast<eSoundObjectBase*>(prop) || enve_cast<eIndependentSound*>(prop)) {
        trackAccent = ThemeSupport::getThemeColorGreen();
    } else if (enve_cast<TextBox*>(prop)) {
        trackAccent = QColor(122, 54, 54);
    } else if (enve_cast<eEffect*>(prop) || enve_cast<BlendEffectBoxShadow*>(prop)) {
        trackAccent = ThemeSupport::getThemeColorOrange();
    } else if (const auto bbox = enve_cast<BoundingBox*>(prop)) {
        trackAccent = bbox->effectiveTimelineColor();
    }

    QColor rowColor = isLayerRow ? ThemeSupport::getThemeBaseColor(132)
                                 : ThemeSupport::getThemeBaseColor(122);
    if (enve_cast<ComplexAnimator*>(prop)) {
        rowColor = ThemeSupport::getThemeBaseColor(112);
    }
    if (mTarget->contentVisible()) {
        rowColor = rowColor.darker(115);
    }
    p.fillRect(rect(), rowColor);

    if (rowSelected) {
        QColor selectionColor = isLayerRow
                ? ThemeSupport::getThemeHighlightColor(78)
                : ThemeSupport::getThemeHighlightColor(45);
        if (isLayerRow && !layerSelected) {
            selectionColor = ThemeSupport::getLightDarkColor(trackAccent, 430);
            selectionColor.setAlpha(190);
        }
        p.fillRect(rect(), selectionColor);
    }
    p.fillRect(QRect(0, 0, qMax(2, eSizesUI::widget/7), height()),
               rowSelected ? ThemeSupport::getThemeHighlightColor(220)
                           : trackAccent);
    if (mHover) {
        p.fillRect(rect(), ThemeSupport::getThemeHighlightColor(rowSelected ? 18 : 28));
    }

    if (bsTarget) {
        nameX += eSizesUI::widget/4;
        const bool ss = enve_cast<eSoundObjectBase*>(prop);
        if (ss || enve_cast<BoundingBox*>(prop)) {
            p.fillRect(rect(), QColor(0, 0, 0, 35));
            p.setPen(Qt::white);
        } else if (enve_cast<BlendEffectBoxShadow*>(prop)) {
            p.fillRect(rect(), QColor(0, 255, 125, 50));
            nameX += eSizesUI::widget;
        }
    } else if(!enve_cast<ComplexAnimator*>(prop)) {
        if(const auto graphAnim = enve_cast<GraphAnimator*>(prop)) {
            const auto bswvp = static_cast<BoxScroller*>(mParent);
            const auto keysView = bswvp->getKeysView();
            if(keysView) {
                const bool selected = keysView->graphIsSelected(graphAnim);
                if(selected) {
                    const int id = keysView->graphGetAnimatorId(graphAnim);
                    const auto color = id >= 0 ?
                                keysView->sGetAnimatorColor(id) :
                                QColor(Qt::black);
                    const QRect visRect(mVisibleButton->pos(),
                                        mVisibleButton->size());
                    const int adj = qRound(4*qreal(mVisibleButton->width())/20);
                    p.fillRect(visRect.adjusted(adj, adj, -adj, -adj), color);
                }
            }
            if(const auto path = enve_cast<SmartPathAnimator*>(prop)) {
                const QRect colRect(QPoint{nameX, 0},
                                    QSize{eSizesUI::widget, eSizesUI::widget});
                p.setPen(Qt::NoPen);
                p.setRenderHint(QPainter::Antialiasing, true);
                p.setBrush(path->getPathColor());
                const int radius = qRound(eSizesUI::widget*0.2);
                p.drawEllipse(colRect.center() + QPoint(0, 2),
                              radius, radius);
                p.setRenderHint(QPainter::Antialiasing, false);
                nameX += eSizesUI::widget;
            }
        } else nameX += eSizesUI::widget;

        if(!enve_cast<Animator*>(prop)) nameX += eSizesUI::widget;
        p.setPen(Qt::white);
    } else {
        p.setPen(Qt::white);
    }

    if (const auto bbox = enve_cast<BoundingBox*>(prop)) {
        if (isTimelineLayerRow()) {
            const QRect swatchRect = timelineColorSwatchRect(nameX);
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setPen(QPen(QColor(10, 10, 10, 180), 1));
            p.setBrush(bbox->effectiveTimelineColor());
            p.drawRoundedRect(swatchRect, 2, 2);
            if (!bbox->hasTimelineColor()) {
                p.setPen(QPen(QColor(255, 255, 255, 110), 1, Qt::DashLine));
                p.setBrush(Qt::NoBrush);
                p.drawRoundedRect(swatchRect.adjusted(1, 1, -1, -1), 2, 2);
            }
            p.setRenderHint(QPainter::Antialiasing, false);
            nameX = swatchRect.right() + eSizesUI::widget/3;
        }
    }

    if (!rowSelected) {
        if (isLayerRow) {
            p.setPen(QColor(236, 236, 236));
        } else if (enve_cast<Animator*>(prop)) {
            p.setPen(QColor(210, 210, 210));
        } else {
            p.setPen(QColor(192, 192, 192));
        }
    }

    p.setPen(QPen(ThemeSupport::getThemeTimelineColor(180), 1));
    p.drawLine(rect().bottomLeft(), rect().bottomRight());
    if(layerSelected) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(ThemeSupport::getThemeHighlightColor(210), 1));
        p.drawRect(rect().adjusted(0, 0, -1, -1));
    } else if(mSelected) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(Qt::lightGray));
        p.drawRect(rect().adjusted(0, 0, -1, -1));
    }
    p.end();
}

void BoxSingleWidget::switchContentVisibleAction() {
    if(!mTarget) return;
    if (auto *scrollWidget = qobject_cast<BoxScrollWidget*>(mParent->parentWidget())) {
        if (scrollWidget->currentAeRevealPreset() != BoxScrollWidget::AeRevealPreset::None) {
            scrollWidget->clearAeRevealPreset();
        }
    }
    mTarget->switchContentVisible();
    Document::sInstance->actionFinished();
    //mParent->callUpdaters();
}

void BoxSingleWidget::switchRecordingAction() {
    if(!mTarget) return;
    const auto target = mTarget->getTarget();
    if(!target) return;
    if (QApplication::keyboardModifiers() & Qt::AltModifier) {
        if (enve_cast<QrealAnimator*>(target)) {
            const auto &iface = DialogsInterface::instance();
            iface.showExpressionDialog(static_cast<QrealAnimator*>(target));
            return;
        }
    }
    if(const auto asAnim = enve_cast<Animator*>(target)) {
        const bool enableAndAdd = !asAnim->anim_isRecording();
        const bool deleteCurrent = !enableAndAdd && asAnim->anim_getKeyOnCurrentFrame();
        const auto targets = animatorsForSelectedBoxesMatchingPath(asAnim);
        for (auto *anim : targets) {
            if (!anim) {
                continue;
            }
            if (enableAndAdd) {
                if (!anim->anim_isRecording()) {
                    anim->anim_setRecordingWithoutChangingKeys(true);
                }
                anim->anim_saveCurrentValueAsKey();
            } else if (deleteCurrent) {
                if (anim->anim_getKeyOnCurrentFrame()) {
                    anim->anim_deleteCurrentKeyAction();
                    if (!anim->anim_hasKeys()) {
                        anim->anim_setRecordingWithoutChangingKeys(false);
                    }
                }
            } else {
                anim->anim_saveCurrentValueAsKey();
            }
        }
        Document::sInstance->actionFinished();
        update();
    }
}

void BoxSingleWidget::switchBoxVisibleAction() {
    if(!mTarget) return;
    const auto target = mTarget->getTarget();
    if(!target) return;
    if(const auto ebos = enve_cast<eBoxOrSound*>(target)) {
        ebos->switchVisible();
    } else if(const auto eEff = enve_cast<eEffect*>(target)) {
        eEff->switchVisible();
    } /*else if(const auto graph = enve_cast<GraphAnimator*>(target)) {
        const auto bsvt = static_cast<BoxScroller*>(mParent);
        const auto keysView = bsvt->getKeysView();
        if(keysView) {
            if(keysView->graphIsSelected(graph)) {
                keysView->graphRemoveViewedAnimator(graph);
            } else {
                keysView->graphAddViewedAnimator(graph);
            }
            Document::sInstance->actionFinished();
        }
    }*/
    Document::sInstance->actionFinished();
    update();
}

void BoxSingleWidget::switchBoxLockedAction() {
    if(!mTarget) return;
    static_cast<BoundingBox*>(mTarget->getTarget())->switchLocked();
    Document::sInstance->actionFinished();
    update();
}

void BoxSingleWidget::updateValueSlidersForQPointFAnimator() {
    if(!mTarget) return;
    const auto target = mTarget->getTarget();
    const auto asQPointFAnim = enve_cast<QPointFAnimator*>(target);
    if(!asQPointFAnim) return;
    mPointValueLabel->hide();
    if(mTarget->contentVisible()) return;
    if(width() - mFillWidget->x() > 10*eSizesUI::widget) {
        mValueSlider->setTarget(asQPointFAnim->getXAnimator());
        mValueSlider->show();
        mValueSlider->setIsLeftSlider(true);
        mSecondValueSlider->setTarget(asQPointFAnim->getYAnimator());
        mSecondValueSlider->show();
        mSecondValueSlider->setIsRightSlider(true);
    } else {
        clearAndHideValueAnimators();
    }
}

void BoxSingleWidget::updatePathCompositionBoxVisible() {
    if(!mTarget) return;
    if(mPathBlendModeVisible && width() - mFillWidget->x() > 8*eSizesUI::widget) {
        mPathBlendModeCombo->show();
    } else mPathBlendModeCombo->hide();
}

void BoxSingleWidget::updateCompositionBoxVisible() {
    if(!mTarget) return;
    const int remaining = width() - mFillWidget->x();
    const int threshold = isTimelineLayerRow() ? qRound(10.5*eSizesUI::widget)
                                               : 10*eSizesUI::widget;
    if(mBlendModeVisible && remaining > threshold) {
        mBlendModeCombo->show();
    } else mBlendModeCombo->hide();
}

void BoxSingleWidget::updateFillTypeBoxVisible() {
    if(!mTarget) return;
    if(mFillTypeVisible && width() - mFillWidget->x() > 8*eSizesUI::widget) {
        mFillTypeCombo->show();
    } else mFillTypeCombo->hide();
}

void BoxSingleWidget::updateTimelineRelationCombosVisible() {
    if(!mTarget) return;
    const int remaining = width() - mFillWidget->x();
    const int relationOffset = mBlendModeCombo->isVisible() ? qRound(5.4*eSizesUI::widget) : 0;
    const bool canShowCollapse = remaining - relationOffset > qRound(2.0*eSizesUI::widget);
    const bool canShowParent = remaining - relationOffset > qRound(3.8*eSizesUI::widget);
    const bool canShowMatte = remaining - relationOffset > qRound(5.8*eSizesUI::widget);
    if (mTimelineParentVisible && canShowParent) {
        mParentLayerCombo->show();
    } else {
        mParentLayerCombo->hide();
    }
    if (mTimelineMatteVisible && canShowMatte) {
        mTrackMatteCombo->show();
    } else {
        mTrackMatteCombo->hide();
    }
    if (mTimelineCollapseVisible && canShowCollapse) {
        mCollapseCheckbox->show();
    } else {
        mCollapseCheckbox->hide();
    }
}

void BoxSingleWidget::updatePickWhipButtonsVisible() {
    if (!mTarget) return;
    const int remaining = width() - mFillWidget->x();
    const bool canShowParent = remaining > qRound(4.2*eSizesUI::widget);
    const bool canShowMatte = remaining > qRound(5.4*eSizesUI::widget);
    mParentPickWhipButton->setVisible(mTimelineParentVisible && canShowParent);
    mMattePickWhipButton->setVisible(mTimelineMatteVisible && canShowMatte);
    if (const auto target = enve_cast<eBoxOrSound*>(mTarget->getTarget())) {
        mSoloButton->setVisible(isTimelineLayerRow());
        mSoloButton->setChecked(mParent && mParent->isSolo(target));
    } else {
        mSoloButton->hide();
    }
}

void BoxSingleWidget::resizeEvent(QResizeEvent *) {
    updateCompositionBoxVisible();
    updatePathCompositionBoxVisible();
    updateFillTypeBoxVisible();
    updateTimelineRelationCombosVisible();
    updatePickWhipButtonsVisible();
    updateValueSlidersForQPointFAnimator();
    refreshDisplayName();
}
