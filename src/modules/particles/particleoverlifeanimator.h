#ifndef PARTICLEOVERLIFEANIMATOR_H
#define PARTICLEOVERLIFEANIMATOR_H

#include "../../core/Animators/qrealanimator.h"

class ParticleOverLifeAnimator final : public QrealAnimator {
public:
    ParticleOverLifeAnimator(const qreal iniVal, const QString& name) :
        QrealAnimator(iniVal, 0, 1, 0.01, name) {
        setNumberDecimals(2);
    }

    void anim_setRecording(const bool rec) override {
        if (!rec) {
            anim_setRecordingWithoutChangingKeys(false);
        }
    }

    void anim_addKeyAtRelFrame(const int relFrame) override {
        QrealAnimator::anim_addKeyAtRelFrame(relFrame);
        anim_setRecordingWithoutChangingKeys(false);
    }

    bool graph_usesNormalizedFrameDomain() const override
    { return true; }

    qreal graph_frameDisplayValue(const qreal frame) const override
    { return qBound<qreal>(0., frame/100., 1.); }

    int graph_frameDisplayPrecision() const override
    { return 2; }

    FrameRange graph_preferredViewFrameRange() const override
    { return {0, 100}; }

    qValueRange graph_getMinAndMaxValues() const override
    { return {0., 1.}; }

    qValueRange graph_getMinAndMaxValuesBetweenFrames(
            const int startFrame, const int endFrame) const override {
        Q_UNUSED(startFrame)
        Q_UNUSED(endFrame)
        return {0., 1.};
    }

    qreal graph_clampGraphValue(const qreal value) override
    { return qBound<qreal>(0., value, 1.); }
};

#endif // PARTICLEOVERLIFEANIMATOR_H
