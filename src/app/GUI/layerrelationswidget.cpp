#include "layerrelationswidget.h"

#include "Private/document.h"
#include "Boxes/boundingbox.h"
#include "Boxes/containerbox.h"
#include "canvas.h"

#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace {
constexpr int kNoneItemData = 0;

SkBlendMode blendModeForMatteIndex(const int index)
{
    switch (index) {
    case 1:
        return SkBlendMode::kDstIn;
    case 2:
        return SkBlendMode::kDstOut;
    default:
        return SkBlendMode::kSrcOver;
    }
}

int matteIndexForBlendMode(const SkBlendMode mode)
{
    switch (mode) {
    case SkBlendMode::kDstIn:
        return 1;
    case SkBlendMode::kDstOut:
        return 2;
    default:
        return 0;
    }
}

QString matteStatusText(BoundingBox *box)
{
    if (!box) {
        return LayerRelationsWidget::tr("No active layer selected.");
    }

    switch (box->getBlendMode()) {
    case SkBlendMode::kDstIn:
        return LayerRelationsWidget::tr("This layer acts as an alpha matte for layers below it.");
    case SkBlendMode::kDstOut:
        return LayerRelationsWidget::tr("This layer acts as an inverted alpha matte for layers below it.");
    default:
        return LayerRelationsWidget::tr("Track matte is off for this layer.");
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
    mMatteCombo->setToolTip(tr("Uses the current layer as a matte for layers beneath it."));
    form->addRow(tr("Track Matte"), mMatteCombo);

    mStatusLabel = new QLabel(group);
    mStatusLabel->setWordWrap(true);
    mStatusLabel->setObjectName(QStringLiteral("AeLayerRelationsStatus"));
    form->addRow(QString(), mStatusLabel);

    mHintLabel = new QLabel(tr("Matte follows layer order: keep the matte layer above the layer you want to reveal."),
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
            mCurrentBox->clearParentKeepTransform();
        } else {
            const auto parentBox = reinterpret_cast<BoundingBox*>(data);
            if (!parentBox || parentBox == mCurrentBox) {
                return;
            }
            mCurrentBox->setParentTransformKeepTransform(parentBox->getTransformAnimator());
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

        mCurrentBox->setBlendModeSk(blendModeForMatteIndex(index));
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
                                &BoundingBox::blendModeChanged,
                                this,
                                [this]() { syncControls(); });
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

    const auto currentParent = mCurrentBox->getParentTransform();
    const auto defaultParent = mCurrentBox->getParentGroup()
            ? mCurrentBox->getParentGroup()->getTransformAnimator()
            : nullptr;

    {
        QSignalBlocker blocker(mParentCombo);
        mUpdatingControls = true;
        int parentIndex = 0;
        if (currentParent && currentParent != defaultParent) {
            for (int i = 1; i < mParentCombo->count(); ++i) {
                const auto data = mParentCombo->itemData(i).value<quintptr>();
                const auto *box = reinterpret_cast<BoundingBox*>(data);
                if (box && box->getTransformAnimator() == currentParent) {
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
        mMatteCombo->setCurrentIndex(matteIndexForBlendMode(mCurrentBox->getBlendMode()));
        mUpdatingControls = false;
    }
}

void LayerRelationsWidget::setControlsEnabled(const bool enabled)
{
    mParentCombo->setEnabled(enabled);
    mMatteCombo->setEnabled(enabled);
    mHintLabel->setEnabled(enabled);
}
