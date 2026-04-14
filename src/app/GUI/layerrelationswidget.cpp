#include "layerrelationswidget.h"

#include "Private/document.h"
#include "Boxes/boundingbox.h"
#include "Boxes/containerbox.h"
#include "BlendEffects/trackmatteeffect.h"
#include "canvas.h"

#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace {
constexpr int kNoneItemData = 0;

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

TrackMatteMode matteModeForIndex(const int index)
{
    switch (index) {
    case 1:
        return TrackMatteMode::alphaMatte;
    case 2:
        return TrackMatteMode::alphaInvertedMatte;
    default:
        return TrackMatteMode::alphaMatte;
    }
}

QString matteStatusText(BoundingBox *box)
{
    if (!box) {
        return LayerRelationsWidget::tr("No active layer selected.");
    }

    const auto *matteSource = box->getTrackMatteTarget();
    if (!matteSource) {
        return LayerRelationsWidget::tr("Track matte is off for this layer.");
    }

    switch (box->getTrackMatteMode()) {
    case TrackMatteMode::alphaMatte:
        return LayerRelationsWidget::tr("This layer uses \"%1\" as its alpha matte.")
                .arg(matteSource->prp_getName());
    case TrackMatteMode::alphaInvertedMatte:
        return LayerRelationsWidget::tr("This layer uses \"%1\" as its inverted alpha matte.")
                .arg(matteSource->prp_getName());
    default:
        return LayerRelationsWidget::tr("Track matte is linked to \"%1\".")
                .arg(matteSource->prp_getName());
    }
}

int matteIndexForBox(BoundingBox *box)
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
}

LayerRelationsWidget::LayerRelationsWidget(Document &document,
                                           QWidget *parent)
    : QWidget(parent)
    , mDocument(document)
{
    setObjectName(QStringLiteral("AeLayerRelationsWidget"));

    const auto mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 8);
    mainLayout->setSpacing(6);

    const auto group = new QGroupBox(tr("Layer Relations"), this);
    group->setObjectName(QStringLiteral("BlueBox"));
    const auto form = new QFormLayout(group);
    form->setContentsMargins(10, 12, 10, 10);
    form->setHorizontalSpacing(8);
    form->setVerticalSpacing(6);

    mParentCombo = new QComboBox(group);
    mParentCombo->setObjectName(QStringLiteral("AeParentCombo"));
    mParentCombo->setToolTip(tr("Parent the active layer to another layer in the current composition."));
    form->addRow(tr("Parent"), mParentCombo);

    mMatteCombo = new QComboBox(group);
    mMatteCombo->setObjectName(QStringLiteral("AeMatteCombo"));
    mMatteCombo->addItem(tr("None"));
    mMatteCombo->addItem(tr("Alpha Matte"));
    mMatteCombo->addItem(tr("Alpha Inverted Matte"));
    mMatteCombo->setToolTip(tr("Uses another layer as the matte source for the current layer."));
    form->addRow(tr("Track Matte"), mMatteCombo);

    mStatusLabel = new QLabel(group);
    mStatusLabel->setWordWrap(true);
    mStatusLabel->setObjectName(QStringLiteral("AeLayerRelationsStatus"));
    form->addRow(QString(), mStatusLabel);

    mHintLabel = new QLabel(tr("If no matte source is linked yet, enabling Track Matte uses the layer above by default. Use the timeline T whip to pick a different source."),
                            group);
    mHintLabel->setWordWrap(true);
    mHintLabel->setObjectName(QStringLiteral("AeLayerRelationsHint"));
    form->addRow(QString(), mHintLabel);

    mainLayout->addWidget(group);

    connect(mParentCombo,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this,
            [this](const int index) {
        if (mUpdatingControls || !mCurrentBox) {
            return;
        }

        const auto data = mParentCombo->itemData(index).value<quintptr>();
        if (data == kNoneItemData) {
            mCurrentBox->clearParentEffectTarget();
        } else {
            const auto parentBox = reinterpret_cast<BoundingBox*>(data);
            if (!parentBox || parentBox == mCurrentBox) {
                return;
            }
            mCurrentBox->setParentEffectTarget(parentBox);
        }

        mDocument.actionFinished();
        syncControls();
    });

    connect(mMatteCombo,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this,
            [this](const int index) {
        if (mUpdatingControls || !mCurrentBox) {
            return;
        }

        if (index == 0) {
            mCurrentBox->clearTrackMatte();
            mDocument.actionFinished();
            syncControls();
            return;
        }

        auto *matteSource = mCurrentBox->getTrackMatteTarget();
        if (!matteSource) {
            matteSource = defaultTrackMatteSource(mCurrentBox);
        }
        if (!matteSource || matteSource == mCurrentBox) {
            syncControls();
            return;
        }

        mCurrentBox->setTrackMatteTarget(matteSource, matteModeForIndex(index));
        mDocument.actionFinished();
        syncControls();
    });

    setControlsEnabled(false);
    mStatusLabel->setText(tr("No active layer selected."));
}

void LayerRelationsWidget::setCurrentScene(Canvas *scene)
{
    if (mSceneSelectionConn) {
        disconnect(mSceneSelectionConn);
    }

    mCurrentScene = scene;
    if (mCurrentScene) {
        mSceneSelectionConn = connect(mCurrentScene,
                                      &Canvas::objectSelectionChanged,
                                      this,
                                      [this]() {
            rebuildParentChoices();
            syncControls();
        });
    }

    rebuildParentChoices();
    syncControls();
}

void LayerRelationsWidget::setCurrentBox(BoundingBox *box)
{
    if (mBoxBlendConn) {
        disconnect(mBoxBlendConn);
    }

    mCurrentBox = box;
    if (mCurrentBox) {
        mBoxBlendConn = connect(mCurrentBox,
                                &BoundingBox::blendEffectChanged,
                                this,
                                [this]() {
            rebuildParentChoices();
            syncControls();
        });
    }

    rebuildParentChoices();
    syncControls();
}

void LayerRelationsWidget::rebuildParentChoices()
{
    QSignalBlocker blocker(mParentCombo);
    mUpdatingControls = true;

    mParentCombo->clear();
    mParentCombo->addItem(tr("None"), QVariant::fromValue<quintptr>(kNoneItemData));

    if (mCurrentScene) {
        const auto &boxes = mCurrentScene->getContainedBoxes();
        for (auto *box : boxes) {
            if (!box || box == mCurrentBox) {
                continue;
            }

            mParentCombo->addItem(box->prp_getName(),
                                  QVariant::fromValue<quintptr>(reinterpret_cast<quintptr>(box)));
        }
    }

    mUpdatingControls = false;
}

void LayerRelationsWidget::syncControls()
{
    const bool hasCurrentBox = !mCurrentBox.isNull();
    setControlsEnabled(hasCurrentBox);
    mStatusLabel->setText(matteStatusText(mCurrentBox));

    if (!hasCurrentBox) {
        return;
    }

    {
        QSignalBlocker blocker(mParentCombo);
        mUpdatingControls = true;
        int parentIndex = 0;
        if (const auto currentParent = mCurrentBox->getParentEffectTarget()) {
            for (int i = 1; i < mParentCombo->count(); ++i) {
                const auto data = mParentCombo->itemData(i).value<quintptr>();
                const auto *box = reinterpret_cast<BoundingBox*>(data);
                if (box == currentParent) {
                    parentIndex = i;
                    break;
                }
            }
        }
        mParentCombo->setCurrentIndex(parentIndex);
        mUpdatingControls = false;
    }

    {
        QSignalBlocker blocker(mMatteCombo);
        mUpdatingControls = true;
        mMatteCombo->setCurrentIndex(matteIndexForBox(mCurrentBox));
        mUpdatingControls = false;
    }
}

void LayerRelationsWidget::setControlsEnabled(const bool enabled)
{
    mParentCombo->setEnabled(enabled);
    mMatteCombo->setEnabled(enabled);
    mHintLabel->setEnabled(enabled);
}
