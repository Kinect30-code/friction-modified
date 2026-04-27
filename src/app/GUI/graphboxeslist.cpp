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

#include <QPainter>
#include <QMouseEvent>
#include <QDebug>
#include "Animators/SmartPath/smartpathanimator.h"
#include "Animators/transformanimator.h"
#include "Expressions/expression.h"
#include "Expressions/propertybindingparser.h"
#include "mainwindow.h"
#include "dialogs/qrealpointvaluedialog.h"
#include "keysview.h"
#include "Animators/qrealpoint.h"
#include "GUI/global.h"
#include "Animators/qrealkey.h"
#include "GUI/BoxesList/boxscrollwidget.h"
#include "themesupport.h"

namespace {
enum class QuickBezierMode {
    EaseIn,
    EaseOut,
    EaseInOut
};

struct QuickBezierPreset {
    QuickBezierMode mode;
    qreal x1;
    qreal y1;
    qreal x2;
    qreal y2;
};

bool resolveQuickBezierPreset(const QString &easingId, QuickBezierPreset &preset)
{
    const QString id = easingId.section('.', -1);
    if (id == QStringLiteral("easeInSine")) {
        preset = {QuickBezierMode::EaseIn, 0.12, 0.0, 0.39, 0.0};
        return true;
    }
    if (id == QStringLiteral("easeOutSine")) {
        preset = {QuickBezierMode::EaseOut, 0.61, 1.0, 0.88, 1.0};
        return true;
    }
    if (id == QStringLiteral("easeInOutSine")) {
        preset = {QuickBezierMode::EaseInOut, 0.37, 0.0, 0.63, 1.0};
        return true;
    }
    if (id == QStringLiteral("easeInQuad")) {
        preset = {QuickBezierMode::EaseIn, 0.11, 0.0, 0.50, 0.0};
        return true;
    }
    if (id == QStringLiteral("easeOutQuad")) {
        preset = {QuickBezierMode::EaseOut, 0.50, 1.0, 0.89, 1.0};
        return true;
    }
    if (id == QStringLiteral("easeInOutQuad")) {
        preset = {QuickBezierMode::EaseInOut, 0.45, 0.0, 0.55, 1.0};
        return true;
    }
    if (id == QStringLiteral("easeInCubic")) {
        preset = {QuickBezierMode::EaseIn, 0.32, 0.0, 0.67, 0.0};
        return true;
    }
    if (id == QStringLiteral("easeOutCubic")) {
        preset = {QuickBezierMode::EaseOut, 0.33, 1.0, 0.68, 1.0};
        return true;
    }
    if (id == QStringLiteral("easeInOutCubic")) {
        preset = {QuickBezierMode::EaseInOut, 0.65, 0.0, 0.35, 1.0};
        return true;
    }
    if (id == QStringLiteral("easeInQuart")) {
        preset = {QuickBezierMode::EaseIn, 0.50, 0.0, 0.75, 0.0};
        return true;
    }
    if (id == QStringLiteral("easeOutQuart")) {
        preset = {QuickBezierMode::EaseOut, 0.25, 1.0, 0.50, 1.0};
        return true;
    }
    if (id == QStringLiteral("easeInOutQuart")) {
        preset = {QuickBezierMode::EaseInOut, 0.76, 0.0, 0.24, 1.0};
        return true;
    }
    if (id == QStringLiteral("easeInQuint")) {
        preset = {QuickBezierMode::EaseIn, 0.64, 0.0, 0.78, 0.0};
        return true;
    }
    if (id == QStringLiteral("easeOutQuint")) {
        preset = {QuickBezierMode::EaseOut, 0.22, 1.0, 0.36, 1.0};
        return true;
    }
    if (id == QStringLiteral("easeInOutQuint")) {
        preset = {QuickBezierMode::EaseInOut, 0.83, 0.0, 0.17, 1.0};
        return true;
    }
    if (id == QStringLiteral("easeInExpo")) {
        preset = {QuickBezierMode::EaseIn, 0.70, 0.0, 0.84, 0.0};
        return true;
    }
    if (id == QStringLiteral("easeOutExpo")) {
        preset = {QuickBezierMode::EaseOut, 0.16, 1.0, 0.30, 1.0};
        return true;
    }
    if (id == QStringLiteral("easeInOutExpo")) {
        preset = {QuickBezierMode::EaseInOut, 0.87, 0.0, 0.13, 1.0};
        return true;
    }
    if (id == QStringLiteral("easeInCirc")) {
        preset = {QuickBezierMode::EaseIn, 0.55, 0.0, 1.00, 0.45};
        return true;
    }
    if (id == QStringLiteral("easeOutCirc")) {
        preset = {QuickBezierMode::EaseOut, 0.00, 0.55, 0.45, 1.0};
        return true;
    }
    if (id == QStringLiteral("easeInOutCirc")) {
        preset = {QuickBezierMode::EaseInOut, 0.85, 0.0, 0.15, 1.0};
        return true;
    }
    if (id == QStringLiteral("easeInBack")) {
        preset = {QuickBezierMode::EaseIn, 0.36, 0.0, 0.66, -0.56};
        return true;
    }
    if (id == QStringLiteral("easeOutBack")) {
        preset = {QuickBezierMode::EaseOut, 0.34, 1.56, 0.64, 1.0};
        return true;
    }
    if (id == QStringLiteral("easeInOutBack")) {
        preset = {QuickBezierMode::EaseInOut, 0.68, -0.6, 0.32, 1.6};
        return true;
    }
    if (id == QStringLiteral("easeInBounce")) {
        preset = {QuickBezierMode::EaseIn, 0.60, 0.0, 0.95, 0.25};
        return true;
    }
    if (id == QStringLiteral("easeOutBounce")) {
        preset = {QuickBezierMode::EaseOut, 0.05, 0.75, 0.40, 1.0};
        return true;
    }
    if (id == QStringLiteral("easeInOutBounce")) {
        preset = {QuickBezierMode::EaseInOut, 0.68, 0.0, 0.32, 1.0};
        return true;
    }
    if (id == QStringLiteral("easeInElastic")) {
        preset = {QuickBezierMode::EaseIn, 0.70, -0.75, 0.90, 0.0};
        return true;
    }
    if (id == QStringLiteral("easeOutElastic")) {
        preset = {QuickBezierMode::EaseOut, 0.10, 1.0, 0.30, 1.75};
        return true;
    }
    if (id == QStringLiteral("easeInOutElastic")) {
        preset = {QuickBezierMode::EaseInOut, 0.80, -0.8, 0.20, 1.8};
        return true;
    }
    return false;
}

bool applyQuickBezierPreset(GraphKey *firstKey,
                            GraphKey *lastKey,
                            const QuickBezierPreset &preset)
{
    if (!firstKey || !lastKey || firstKey == lastKey) {
        return false;
    }

    const qreal startFrame = firstKey->getRelFrame();
    const qreal endFrame = lastKey->getRelFrame();
    const qreal dFrame = endFrame - startFrame;
    if (qFuzzyIsNull(dFrame)) {
        return false;
    }

    const qreal startValue = firstKey->getValueForGraph();
    const qreal endValue = lastKey->getValueForGraph();
    const qreal dValue = endValue - startValue;

    firstKey->setCtrlsModeAction(CtrlsMode::corner);
    lastKey->setCtrlsModeAction(CtrlsMode::corner);
    firstKey->setC1EnabledAction(true);
    lastKey->setC0EnabledAction(true);

    firstKey->setC1Frame(startFrame + dFrame * preset.x1);
    firstKey->setC1Value(startValue + dValue * preset.y1);
    lastKey->setC0Frame(startFrame + dFrame * preset.x2);
    lastKey->setC0Value(startValue + dValue * preset.y2);
    lastKey->afterKeyChanged();
    return true;
}

QList<QList<GraphKey*>> selectedOrForwardSegmentsForGraph(GraphAnimator *anim)
{
    QList<QList<GraphKey*>> segments;
    if (!anim) {
        return segments;
    }

    anim->graph_getSelectedSegments(segments);
    if (!segments.isEmpty()) {
        return segments;
    }

    const auto &selectedKeys = anim->anim_getSelectedKeys();
    for (const auto &key : selectedKeys) {
        auto *graphKey = dynamic_cast<GraphKey*>(key);
        if (!graphKey) {
            continue;
        }
        auto *nextGraphKey = dynamic_cast<GraphKey*>(graphKey->getNextKey());
        if (!nextGraphKey) {
            continue;
        }
        segments << (QList<GraphKey*>() << graphKey << nextGraphKey);
    }

    return segments;
}
}

QColor KeysView::sGetAnimatorColor(const int i) {
    return ANIMATOR_COLORS.at(i % ANIMATOR_COLORS.length());
}

bool KeysView::graphIsSelected(GraphAnimator * const anim) {
    if(mCurrentScene && mBoxesListWidget) {
        const int id = mBoxesListWidget->getId();
        const auto all = mCurrentScene->getSelectedForGraph(id);
        if(all) return all->contains(anim);
    }
    return false;
}

void KeysView::graphEasingAction(const QString &easing)
{
    if (mSelectedKeysAnimators.isEmpty()) { return; }
    if (mGraphViewed) {
        for (const auto& anim : mGraphAnimators) {
            const auto segments = selectedOrForwardSegmentsForGraph(anim);
            for (const auto &segment : segments) {
                if (segment.count() < 2) { continue; }
                auto *firstKey = segment.first();
                auto *lastKey = segment.last();
                graphEasingApply(static_cast<QrealAnimator*>(anim),
                                 {firstKey->getRelFrame(),
                                  lastKey->getRelFrame()},
                                 easing);
            }
        }
    } else {
        for (const auto& anim : mSelectedKeysAnimators) {
            const auto graphAnim = enve_cast<GraphAnimator*>(anim);
            const auto segments = selectedOrForwardSegmentsForGraph(graphAnim);
            for (const auto &segment : segments) {
                if (segment.count() < 2) { continue; }
                auto *firstKey = segment.first();
                auto *lastKey = segment.last();
                graphEasingApply(static_cast<QrealAnimator*>(anim),
                                 {firstKey->getRelFrame(),
                                  lastKey->getRelFrame()},
                                 easing);
            }
        }
    }
}

void KeysView::graphEasingApply(QrealAnimator *anim,
                                const FrameRange &range,
                                const QString &easing)
{
    if (!anim) { return; }
    if (const auto spa = enve_cast<SmartPathAnimator*>(anim)) {
        emit statusMessage(tr("Smart paths does not support easing"));
        return;
    }
    QuickBezierPreset quickPreset;
    if (resolveQuickBezierPreset(easing, quickPreset)) {
        auto *firstKey = dynamic_cast<GraphKey*>(anim->anim_getKeyAtRelFrame(range.fMin));
        auto *lastKey = dynamic_cast<GraphKey*>(anim->anim_getKeyAtRelFrame(range.fMax));
        if (applyQuickBezierPreset(firstKey, lastKey, quickPreset)) {
            return;
        }
    }
    if (!graphEasingApplyExpression(anim, range, easing)) {
        emit statusMessage(tr("Failed to apply easing on %1").arg(anim->prp_getName()));
    }
}

bool KeysView::graphEasingApplyExpression(QrealAnimator *anim,
                                          const FrameRange &range,
                                          const QString &easing)
{
    if (!anim || easing.isEmpty()) { return false; }

    const auto preset = eSettings::sInstance->fExpressions.getExpr(easing);
    if (!preset.valid || !preset.enabled) { return false; }
    QString script = preset.script;
    script.replace("__START_VALUE__",
                   QString::number(anim->getBaseValue(range.fMin)));
    script.replace("__END_VALUE__",
                   QString::number(anim->getBaseValue(range.fMax)));
    script.replace("__START_FRAME__",
                   QString::number(range.fMin));
    script.replace("__END_FRAME__",
                   QString::number(range.fMax));

    PropertyBindingMap bindings;
    try { bindings = PropertyBindingParser::parseBindings(preset.bindings, nullptr, anim); }
    catch (const std::exception& e) { return false; }

    auto engine = std::make_unique<QJSEngine>();
    try { Expression::sAddDefinitionsTo(preset.definitions, *engine); }
    catch (const std::exception& e) { return false; }

    QJSValue eEvaluate;
    try {
        Expression::sAddScriptTo(script, bindings, *engine, eEvaluate,
                                 Expression::sQrealAnimatorTester);
    } catch(const std::exception& e) { return false; }

    try {
        auto expr = Expression::sCreate(preset.definitions,
                                        script, std::move(bindings),
                                        std::move(engine),
                                        std::move(eEvaluate));
        if (expr && !expr->isValid()) { expr = nullptr; }
        anim->setExpression(expr);
        anim->applyExpression(range, 10, true, true);
        Document::sInstance->actionFinished();
    } catch (const std::exception& e) { return false; }

    return true;
}

int KeysView::graphGetAnimatorId(GraphAnimator * const anim) {
    return mGraphAnimators.indexOf(anim);
}

void KeysView::graphSetSmoothCtrlAction() {
    graphSetCtrlsModeForSelected(CtrlsMode::smooth);
    Document::sInstance->actionFinished();
}

void KeysView::graphSetSymmetricCtrlAction() {
    graphSetCtrlsModeForSelected(CtrlsMode::symmetric);
    Document::sInstance->actionFinished();
}

void KeysView::graphSetCornerCtrlAction() {
    graphSetCtrlsModeForSelected(CtrlsMode::corner);
    Document::sInstance->actionFinished();
}

void KeysView::graphMakeSegmentsSmoothAction(const bool smooth) {
    applyQuickInterpolation(smooth);
}

void KeysView::graphMakeSegmentsLinearAction() {
    graphMakeSegmentsSmoothAction(false);
}

void KeysView::graphMakeSegmentsSmoothAction() {
    graphMakeSegmentsSmoothAction(true);
}

void KeysView::graphPaint(QPainter *p) {
    p->setBrush(Qt::NoBrush);

    if(graphUsesNormalizedFrameDomain()) {
        const qreal maxX = width();
        const QList<int> incFrameList = {100, 50, 25, 10, 5, 1};
        int alpha = 24;
        qreal lineWidth = 1.;
        for(const int incFrame : incFrameList) {
            if(mPixelsPerFrame*incFrame < 14.) continue;
            const bool drawText = mPixelsPerFrame*incFrame > 48.;
            QColor lineColor = ThemeSupport::getThemeTimelineColor();
            lineColor.setAlpha(alpha);
            p->setPen(QPen(lineColor, lineWidth));
            int frameL = (mMinViewedFrame >= 0) ? -(mMinViewedFrame%incFrame) :
                                                -mMinViewedFrame;
            int currFrame = mMinViewedFrame + frameL;
            qreal xL = frameL*mPixelsPerFrame;
            const qreal inc = incFrame*mPixelsPerFrame;
            while(xL < 0) {
                xL += inc;
                currFrame += incFrame;
            }
            while(xL < maxX) {
                if(drawText) {
                    p->drawText(QRectF(xL - inc*0.5, 0, inc, 18),
                                Qt::AlignCenter,
                                graphFormatFrameLabel(currFrame));
                }
                p->drawLine(xL, 18, xL, height());
                xL += inc;
                currFrame += incFrame;
            }
            alpha = qMin(alpha + 20, 96);
            lineWidth += 0.1;
        }
    }
    if(graph_mValueLinesVisible) {
        p->setPen(QColor(255, 255, 255));
        const qreal incY = mValueInc*mPixelsPerValUnit;
        qreal yL = height() + fmod(mMinShownVal, mValueInc)*mPixelsPerValUnit + incY;
        qreal currValue = mMinShownVal - fmod(mMinShownVal, mValueInc) - mValueInc;
        const int nLines = qCeil(yL/incY);
        const auto lines = new QLine[static_cast<uint>(nLines)];
        int currLine = 0;
        while(yL > 0) {
            p->drawText(QRectF(-eSizesUI::widget/4, yL - incY,
                               2*eSizesUI::widget, 2*incY),
                        Qt::AlignCenter,
                        QString::number(currValue, 'f', mValuePrec));
            int yLi = qRound(yL);
            lines[currLine] = QLine(2*eSizesUI::widget, yLi, width(), yLi);
            currLine++;
            yL -= incY;
            currValue += mValueInc;
        }
        p->setPen(ThemeSupport::getThemeTimelineColor());
        p->drawLines(lines, nLines);
        delete[] lines;
    }

    p->setRenderHint(QPainter::Antialiasing);

    QMatrix transform;
    transform.translate(-mPixelsPerFrame*(mMinViewedFrame - 0.5),
                        height() + mPixelsPerValUnit*mMinShownVal);
    transform.scale(mPixelsPerFrame, -mPixelsPerValUnit);
    p->setTransform(QTransform(transform), true);

    const int minVisibleFrame = qFloor(mMinViewedFrame -
                                       eSizesUI::widget/(2*mPixelsPerFrame));
    const int maxVisibleFrame = qCeil(mMaxViewedFrame +
                                      3*eSizesUI::widget/(2*mPixelsPerFrame));
    const FrameRange viewedRange = { minVisibleFrame, maxVisibleFrame};
    for(int i = mGraphAnimators.count() - 1; i >= 0; i--) {
        const QColor &col = ANIMATOR_COLORS.at(i % ANIMATOR_COLORS.length());
        p->save();
        mGraphAnimators.at(i)->graph_drawKeysPath(p, col, viewedRange);
        p->restore();
    }
    p->setRenderHint(QPainter::Antialiasing, false);

    if(mSelecting) {
        QPen pen;
        pen.setColor(Qt::blue);
        pen.setWidthF(2);
        pen.setStyle(Qt::DotLine);
        pen.setCosmetic(true);
        p->setPen(pen);
        p->setBrush(Qt::NoBrush);
        p->drawRect(mSelectionRect);
    }
/*

    if(hasFocus() ) {
        p->setPen(QPen(Qt::red, 4.));
    } else {
        p->setPen(Qt::NoPen);
    }
    p->setBrush(Qt::NoBrush);
    p->drawRect(mGraphRect);
*/
}

void KeysView::graphGetAnimatorsMinMaxValue(qreal &minVal, qreal &maxVal) {
    if(mGraphAnimators.isEmpty()) {
        minVal = 0;
        maxVal = 0;
    } else {
        minVal = 1000000;
        maxVal = -1000000;

        for(const auto& anim : mGraphAnimators) {
            const auto valRange = anim->graph_getMinAndMaxValues();
            minVal = qMin(minVal, valRange.fMin);
            maxVal = qMax(maxVal, valRange.fMax);
        }
    }
    if(qAbs(minVal - maxVal) < 0.1) {
        minVal -= 0.05;
        maxVal += 0.05;
    }
    const qreal valRange = maxVal - minVal;
    maxVal += valRange*0.05;
    minVal -= valRange*0.05;
}

GraphAnimator *KeysView::graphPrimaryAnimator() const {
    if(mGraphAnimators.isEmpty()) return nullptr;
    return mGraphAnimators.first();
}

bool KeysView::graphUsesNormalizedFrameDomain() const {
    if(mGraphAnimators.isEmpty()) return false;
    for(const auto& anim : mGraphAnimators) {
        if(!anim->graph_usesNormalizedFrameDomain()) return false;
    }
    return true;
}

void KeysView::graphResetHorizontalRangeIfNeeded() {
    if(!graphUsesNormalizedFrameDomain()) return;
    const auto anim = graphPrimaryAnimator();
    if(!anim) return;
    const auto range = anim->graph_preferredViewFrameRange();
    if(mMinViewedFrame == range.fMin && mMaxViewedFrame == range.fMax) return;
    setFramesRange(range);
    emit changedViewedFrames(range);
}

QString KeysView::graphFormatFrameLabel(const qreal frame) const {
    const auto anim = graphPrimaryAnimator();
    if(!anim) return QString::number(frame);
    return QString::number(anim->graph_frameDisplayValue(frame),
                           'f',
                           anim->graph_frameDisplayPrecision());
}

void KeysView::graphUpdateDimensions() {
    const QList<qreal> validIncs = {7.5, 5, 2.5, 1};
    qreal incMulti = 10000.;
    int currIncId = 0;
    int nDiv = 0;
    mValueInc = validIncs.first()*incMulti;
    while(true) {
        mValueInc = validIncs.at(currIncId)*incMulti;
        if(mValueInc*mPixelsPerValUnit < 50) break;
        currIncId++;
        if(currIncId >= validIncs.count()) {
            currIncId = 0;
            incMulti *= 0.1;
            nDiv++;
        }
    }
    mValuePrec = qMax(nDiv - 3, 0);

    graphIncMinShownVal(0);
    updatePixelsPerFrame();
}

void KeysView::graphResizeEvent(QResizeEvent *) {
    graphUpdateDimensions();
}

void KeysView::graphIncMinShownVal(const qreal inc) {
    graphSetMinShownVal((eSizesUI::widget/2)*inc/mPixelsPerValUnit +
                        mMinShownVal);
}

void KeysView::graphSetMinShownVal(const qreal newMinShownVal) {
    mMinShownVal = newMinShownVal;
}

void KeysView::graphGetValueAndFrameFromPos(const QPointF &pos,
                                            qreal &value, qreal &frame) const {
    value = (height() - pos.y())/mPixelsPerValUnit + mMinShownVal;
    frame = mMinViewedFrame + pos.x()/mPixelsPerFrame - 0.5;
}

QrealPoint * KeysView::graphGetPointAtPos(const QPointF &pressPos) const {
    qreal value;
    qreal frame;
    graphGetValueAndFrameFromPos(pressPos, value, frame);

    QrealPoint* point = nullptr;
    for(const auto& anim : mGraphAnimators) {
        point = anim->graph_getPointAt(value, frame, mPixelsPerFrame,
                                       mPixelsPerValUnit);
        if(point) break;
    }
    return point;
}

qreal KeysView::xToFrame(const qreal x) const {
    return x/mPixelsPerFrame + mMinViewedFrame;
}

void KeysView::graphMousePress(const QPointF &pressPos) {
    mFirstMove = true;
    QrealPoint * const pressedPoint = graphGetPointAtPos(pressPos);
    Key *parentKey = pressedPoint ? pressedPoint->getParentKey() : nullptr;
    if(!pressedPoint) {
        mSelecting = true;
        qreal value;
        qreal frame;
        graphGetValueAndFrameFromPos(pressPos, value, frame);
        mSelectionRect.setBottomRight({frame, value});
        mSelectionRect.setTopLeft({frame, value});
    } else if(pressedPoint->isKeyPt()) {
        if(QApplication::keyboardModifiers() & Qt::ShiftModifier) {
            if(parentKey->isSelected()) removeKeyFromSelection(parentKey);
            else addKeyToSelection(parentKey);
        } else {
            if(!parentKey->isSelected()) {
                clearKeySelection();
                addKeyToSelection(parentKey);
            }
        }
    } else {
        auto parentKey = pressedPoint->getParentKey();
        auto parentAnimator = parentKey->getParentAnimator<GraphAnimator>();
        parentAnimator->graph_getFrameValueConstraints(
                    parentKey, pressedPoint->getType(),
                    mMinMoveFrame, mMaxMoveFrame,
                    mMinMoveVal, mMaxMoveVal);
        pressedPoint->setSelected(true);
    }
    mGPressedPoint = pressedPoint;
    mMovingKeys = pressedPoint;
}

void KeysView::gMouseRelease() {
    if(mSelecting) {
        if(!(QApplication::keyboardModifiers() & Qt::ShiftModifier))
            clearKeySelection();

        QList<GraphKey*> keysList;
        for(const auto& anim : mGraphAnimators)
            anim->gAddKeysInRectToList(mSelectionRect, keysList);
        for(const auto& key : keysList)
            addKeyToSelection(key);

        mSelecting = false;
    } else if(mGPressedPoint) {

    }
}

void KeysView::gMiddlePress(const QPointF &pressPos) {
    mSavedMinViewedFrame = mMinViewedFrame;
    mSavedMaxViewedFrame = mMaxViewedFrame;
    mSavedMinShownValue = mMinShownVal;
    mMiddlePressPos = pressPos;
}

void KeysView::graphMiddleMove(const QPointF &movePos) {
    QPointF diffFrameValue = (movePos - mMiddlePressPos);
    diffFrameValue.setX(diffFrameValue.x()/mPixelsPerFrame);
    diffFrameValue.setY(diffFrameValue.y()/mPixelsPerValUnit);
    const int roundX = qRound(diffFrameValue.x() );
    setFramesRange({mSavedMinViewedFrame - roundX,
                    mSavedMaxViewedFrame - roundX});
    graphSetMinShownVal(mSavedMinShownValue + diffFrameValue.y());
}

void KeysView::graphConstrainAnimatorCtrlsFrameValues() {
    for(const auto& anim : mGraphAnimators) {
        anim->graph_constrainCtrlsFrameValues();
    }
}

void KeysView::graphSetCtrlsModeForSelected(const CtrlsMode mode) {
    if(mSelectedKeysAnimators.isEmpty()) return;

    for(const auto& anim : mGraphAnimators) {
        if(!anim->anim_hasSelectedKeys()) continue;
        anim->graph_startSelectedKeysTransform();
        anim->graph_enableCtrlPtsForSelected();
        anim->graph_setCtrlsModeForSelectedKeys(mode);
        anim->graph_finishSelectedKeysTransform();
    }
    graphConstrainAnimatorCtrlsFrameValues();
}

void KeysView::graphDeletePressed() {
    if(mGPressedPoint && mGPressedPoint->isCtrlPt()) {
        const auto parentKey = mGPressedPoint->getParentKey();
        if(mGPressedPoint->isC1Pt()) {
            parentKey->setC1EnabledAction(false);
        } else if(mGPressedPoint->isC0Pt()) {
            parentKey->setC0EnabledAction(false);
        }
        mGPressedPoint->setSelected(false);
        parentKey->afterKeyChanged();
    } else {
        deleteSelectedKeys();
    }
    mGPressedPoint = nullptr;
}

void KeysView::graphWheelEvent(QWheelEvent *event,
                               const qreal &hframe)
{
#ifdef Q_OS_MAC
    if (event->angleDelta().y() == 0) { return; }
#endif
    const bool ctrl = (event->modifiers() & Qt::ControlModifier);
    const bool shift = (event->modifiers() & Qt::ShiftModifier);
    if (ctrl && !shift) {
        emit wheelEventSignal(event, hframe);
        return;
    } else if (ctrl || shift) {
        if (ctrl) { emit wheelEventSignal(event, hframe); }
        qreal valUnderMouse;
        qreal frame;
        const auto ePos = event->position();
        graphGetValueAndFrameFromPos(ePos,
                                     valUnderMouse, frame);
        qreal graphScaleInc;
        if (event->angleDelta().y() > 0) { graphScaleInc = 0.1; }
        else { graphScaleInc = -0.1; }
        graphSetMinShownVal(mMinShownVal +
                            (valUnderMouse - mMinShownVal)*graphScaleInc);
        mPixelsPerValUnit += graphScaleInc*mPixelsPerValUnit;
        graphUpdateDimensions();
        if (ctrl) { return; }
    } else {
        if (event->angleDelta().y() > 0) { graphIncMinShownVal(1); }
        else { graphIncMinShownVal(-1); }
    }

    update();
}

bool KeysView::graphProcessFilteredKeyEvent(QKeyEvent *event) {
    const auto key = event->key();

    if(key == Qt::Key_X && mMovingKeys) {
        mValueInput.switchXOnlyMode();
        handleMouseMove(mapFromGlobal(QCursor::pos()),
                        Qt::LeftButton);
    } else if(key == Qt::Key_Y && mMovingKeys) {
        mValueInput.switchYOnlyMode();
        handleMouseMove(mapFromGlobal(QCursor::pos()),
                        Qt::LeftButton);
    } else {
        return false;
    }
    return true;
}

void KeysView::graphResetValueScaleAndMinShownAction() {
    graphResetHorizontalRangeIfNeeded();
    graphResetValueScaleAndMinShown();
    update();
}

void KeysView::graphSetValueLinesDisabled(const bool disabled) {
    graph_mValueLinesVisible = !disabled;
    update();
}

void KeysView::graphResetValueScaleAndMinShown() {
    qreal minVal;
    qreal maxVal;
    graphGetAnimatorsMinMaxValue(minVal, maxVal);
    graphSetMinShownVal(minVal);
    mPixelsPerValUnit = height()/(maxVal - minVal);
    graphUpdateDimensions();
}

bool KeysView::graphValidateVisible(GraphAnimator* const animator)
{
    if (animator->prp_isSelected() &&
        animator->prp_isParentBoxContained()) { return true; }
    return false;
}

void KeysView::graphAddToViewedAnimatorList(GraphAnimator * const animator) {
    if (mGraphAnimators.contains(animator)) { return; }
    auto& connContext = mGraphAnimators.addObj(animator);
    connContext << connect(animator, &QObject::destroyed,
                           this, [this, animator]() {
        graphRemoveViewedAnimator(animator);
    });
}

void KeysView::graphUpdateVisible()
{
    //mGraphAnimators.clear();
    if (mCurrentScene && mBoxesListWidget) {
        const int id = mBoxesListWidget->getId();
        const auto all = mCurrentScene->getSelectedForGraph(id);
        if (all) {
            const QList<GraphAnimator*> selectedAnimators = all->getList();
            for (auto *anim : selectedAnimators) {
                if (!anim) { continue; }
                if (graphValidateVisible(anim)) { graphAddToViewedAnimatorList(anim); }
                else {
                    anim->prp_setSelected(false);
                    graphRemoveViewedAnimator(anim);
                }
            }
        }
    }
    graphUpdateDimensions();
    graphResetHorizontalRangeIfNeeded();
    graphResetValueScaleAndMinShown();
    update();
}

void KeysView::graphAddViewedAnimator(GraphAnimator * const animator) {
    if(!mCurrentScene || !mBoxesListWidget) return;
    const int id = mBoxesListWidget->getId();
    mCurrentScene->addSelectedForGraph(id, animator);
    if(graphValidateVisible(animator)) {
        graphAddToViewedAnimatorList(animator);
        graphUpdateDimensions();
        graphResetHorizontalRangeIfNeeded();
        graphResetValueScaleAndMinShown();
        update();
    }
}

void KeysView::graphRemoveViewedAnimator(GraphAnimator * const animator) {
    if (!mGraphAnimators.contains(animator)) { return; }
    if(mCurrentScene && mBoxesListWidget) {
        const int id = mBoxesListWidget->getId();
        mCurrentScene->removeSelectedForGraph(id, animator);
    }
    if(mGraphAnimators.removeObj(animator)) {
        graphUpdateDimensions();
        graphResetHorizontalRangeIfNeeded();
        graphResetValueScaleAndMinShown();
        update();
    }
}

void KeysView::scheduleGraphUpdateAfterKeysChanged() {
    if(mGraphUpdateAfterKeysChangedNeeded) return;
    mGraphUpdateAfterKeysChangedNeeded = true;
}

void KeysView::graphUpdateAfterKeysChangedIfNeeded() {
    if(mGraphUpdateAfterKeysChangedNeeded) {
        mGraphUpdateAfterKeysChangedNeeded = false;
        graphUpdateAfterKeysChanged();
    }
}

void KeysView::graphUpdateAfterKeysChanged() {
    graphResetValueScaleAndMinShown();
    graphUpdateDimensions();
}

void KeysView::keyframeZoomHorizontalAction()
{
    if (!mCurrentScene) { return; }
    if (graphUsesNormalizedFrameDomain()) {
        graphResetHorizontalRangeIfNeeded();
        update();
        return;
    }
    
    FrameRange range;
    if (!mSelectedKeysAnimators.isEmpty()) {
        int minFrame = INT_MAX;
        int maxFrame = INT_MIN;
        
        for (const auto& anim : mSelectedKeysAnimators) {
            const int animMin = anim->anim_getLowestAbsFrameForSelectedKey(); 
            const int animMax = anim->anim_getHighestAbsFrameForSelectedKey();
            
            if (animMin < minFrame) { minFrame = animMin; }
            if (animMax > maxFrame) { maxFrame = animMax; }
        }
        
        if (minFrame != INT_MAX && maxFrame != INT_MIN && minFrame < maxFrame) {
            range = {minFrame, maxFrame};
        } else {
            range = mCurrentScene->getFrameRange();
        }
    } else {
        range = mCurrentScene->getFrameRange(); 
    }

    const int padding = 2;
    const FrameRange newRange = {range.fMin - padding, range.fMax + padding};
    setFramesRange(newRange);
    emit changedViewedFrames(newRange);
    update();
}
