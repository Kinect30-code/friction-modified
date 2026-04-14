#include "glbcliptrack.h"

#include <cmath>

namespace {

constexpr int kGlbHostTimelineFrames = 1000000;

QStringList defaultClipNames() {
    return QStringList() << QStringLiteral("No Animations");
}

float wrapTimeSeconds(const float timeSeconds, const float durationSeconds) {
    if(durationSeconds <= 0.f) {
        return 0.f;
    }
    const float wrapped = std::fmod(timeSeconds, durationSeconds);
    return wrapped < 0.f ? wrapped + durationSeconds : wrapped;
}

}

GlbClipTrack::GlbClipTrack()
    : StaticComplexAnimator(QStringLiteral("track"))
{
    mEnabled = enve::make_shared<BoolAnimator>(QStringLiteral("enabled"));
    mEnabled->setCurrentBoolValue(true);
    ca_addChild(mEnabled);

    mClip = enve::make_shared<EnumAnimator>(QStringLiteral("clip"),
                                            defaultClipNames(),
                                            0);
    ca_addChild(mClip);

    mWeight = enve::make_shared<QrealAnimator>(1.0,
                                               0.0,
                                               1.0,
                                               0.01,
                                               QStringLiteral("weight"));
    mWeight->setNumberDecimals(2);
    ca_addChild(mWeight);

    mSpeed = enve::make_shared<QrealAnimator>(1.0,
                                              -4.0,
                                              4.0,
                                              0.01,
                                              QStringLiteral("speed"));
    mSpeed->setNumberDecimals(2);
    ca_addChild(mSpeed);

    mTimeOffsetFrames = enve::make_shared<QrealAnimator>(
                0.0,
                -100000.0,
                100000.0,
                1.0,
                QStringLiteral("time offset"));
    mTimeOffsetFrames->setNumberDecimals(0);
    ca_addChild(mTimeOffsetFrames);

    mLoopMode = enve::make_shared<EnumAnimator>(
                QStringLiteral("loop"),
                QStringList() << QStringLiteral("Once")
                              << QStringLiteral("Loop"),
                int(GlbClipLoopMode::once));
    ca_addChild(mLoopMode);

    const auto notify = [this]() { emit trackChanged(); };
    connect(mEnabled.get(), &QrealAnimator::baseValueChanged, this,
            [notify](const qreal) { notify(); });
    connect(mClip.get(), &QrealAnimator::baseValueChanged, this,
            [notify](const qreal) { notify(); });
    connect(mClip.get(), &EnumAnimator::valueNamesChanged, this,
            [notify](const QStringList&) { notify(); });
    connect(mWeight.get(), &QrealAnimator::baseValueChanged, this,
            [notify](const qreal) { notify(); });
    connect(mSpeed.get(), &QrealAnimator::baseValueChanged, this,
            [notify](const qreal) { notify(); });
    connect(mTimeOffsetFrames.get(), &QrealAnimator::baseValueChanged, this,
            [notify](const qreal) { notify(); });
    connect(mLoopMode.get(), &QrealAnimator::baseValueChanged, this,
            [notify](const qreal) { notify(); });
    connect(mLoopMode.get(), &EnumAnimator::valueNamesChanged, this,
            [notify](const QStringList&) { notify(); });
}

void GlbClipTrack::prp_setupTreeViewMenu(PropertyMenu * const menu) {
    if(menu->hasActionsForType<GlbClipTrack>()) {
        return;
    }
    menu->addedActionsForType<GlbClipTrack>();
    const PropertyMenu::PlainSelectedOp<GlbClipTrack> dOp =
    [](GlbClipTrack* const track) {
        if(!track) {
            return;
        }
        const auto parent = track->getParent<DynamicComplexAnimatorBase<GlbClipTrack>>();
        if(!parent || parent->ca_getNumberOfChildren() <= 1) {
            return;
        }
        parent->removeChild(track->ref<GlbClipTrack>());
    };
    menu->addPlainAction(QIcon::fromTheme("trash"),
                         QObject::tr("Delete Track"),
                         dOp)->setEnabled(getParent<DynamicComplexAnimatorBase<GlbClipTrack>>() &&
                                          getParent<DynamicComplexAnimatorBase<GlbClipTrack>>()->ca_getNumberOfChildren() > 1);
    menu->addSeparator();
    StaticComplexAnimator::prp_setupTreeViewMenu(menu);
}

void GlbClipTrack::setClipNames(const QStringList &clipNames) {
    mClip->setValueNames(clipNames.isEmpty() ? defaultClipNames() : clipNames);
    mClip->SWT_setDisabled(mClip->getValueNames().count() <= 1);
}

void GlbClipTrack::setClipIndex(const int index) {
    mClip->setCurrentValue(index);
}

bool GlbClipTrack::enabled(const qreal relFrame) const {
    return mEnabled && mEnabled->getBoolValue(relFrame);
}

int GlbClipTrack::clipIndex(const qreal relFrame) const {
    return mClip ? mClip->getCurrentValue(relFrame) : -1;
}

QString GlbClipTrack::clipName() const {
    return mClip ? mClip->getCurrentValueName() : QStringLiteral("No Animations");
}

qreal GlbClipTrack::weight(const qreal relFrame) const {
    return mWeight ? qBound<qreal>(0.0, mWeight->getEffectiveValue(relFrame), 1.0) : 0.0;
}

qreal GlbClipTrack::speed(const qreal relFrame) const {
    if(!mSpeed) {
        return 1.0;
    }
    const qreal value = mSpeed->getEffectiveValue(relFrame);
    return qFuzzyIsNull(value) ? 0.0 : value;
}

qreal GlbClipTrack::timeOffsetFrames(const qreal relFrame) const {
    return mTimeOffsetFrames ? mTimeOffsetFrames->getEffectiveValue(relFrame) : 0.0;
}

GlbClipLoopMode GlbClipTrack::loopMode(const qreal relFrame) const {
    return mLoopMode && mLoopMode->getCurrentValue(relFrame) == int(GlbClipLoopMode::loop) ?
                GlbClipLoopMode::loop :
                GlbClipLoopMode::once;
}

GlbClipTrackList::GlbClipTrackList()
    : DynamicComplexAnimator<GlbClipTrack>(QStringLiteral("clip tracks"))
{
    connect(this, &ComplexAnimator::ca_childAdded, this, [this](Property* child) {
        if(auto* const track = enve_cast<GlbClipTrack*>(child)) {
            connect(track, &GlbClipTrack::trackChanged,
                    this, &GlbClipTrackList::tracksChanged);
        }
        emit tracksChanged();
    });
    connect(this, &ComplexAnimator::ca_childRemoved, this, [this](Property*) {
        emit tracksChanged();
    });
    ensurePrimaryTrack();
}

void GlbClipTrackList::prp_setupTreeViewMenu(PropertyMenu * const menu) {
    if(menu->hasActionsForType<GlbClipTrackList>()) {
        return;
    }
    menu->addedActionsForType<GlbClipTrackList>();
    const PropertyMenu::PlainSelectedOp<GlbClipTrackList> aOp =
    [](GlbClipTrackList* const target) {
        if(target) {
            target->addTrack();
        }
    };
    menu->addPlainAction(QIcon::fromTheme("list-add"),
                         QObject::tr("Add Clip Track"),
                         aOp);
    menu->addSeparator();
    DynamicComplexAnimator<GlbClipTrack>::prp_setupTreeViewMenu(menu);
}

void GlbClipTrackList::ensurePrimaryTrack() {
    if(ca_getNumberOfChildren() > 0) {
        return;
    }
    addChild(createTrack(makeNameUnique(QStringLiteral("track 1"))));
}

void GlbClipTrackList::syncClipNames(const QStringList &clipNames) {
    ensurePrimaryTrack();
    const QStringList safeNames = clipNames.isEmpty() ? defaultClipNames() : clipNames;
    for(int i = 0; i < ca_getNumberOfChildren(); ++i) {
        getChild(i)->setClipNames(safeNames);
    }
}

void GlbClipTrackList::setPrimaryClipIndex(const int clipIndex) {
    ensurePrimaryTrack();
    getChild(0)->setClipIndex(clipIndex);
}

QString GlbClipTrackList::describeSelection() const {
    int activeTrackCount = 0;
    QString firstClipName = QStringLiteral("No Animations");
    for(int i = 0; i < ca_getNumberOfChildren(); ++i) {
        const auto* const track = getChild(i);
        if(!track->enabled() || track->weight() <= 0.0) {
            continue;
        }
        ++activeTrackCount;
        if(activeTrackCount == 1) {
            firstClipName = track->clipName();
        }
    }
    if(activeTrackCount <= 1) {
        return firstClipName;
    }
    return QObject::tr("%1 tracks mixed").arg(activeTrackCount);
}

int GlbClipTrackList::frameCountHint(const GltfSceneData &scene,
                                     const qreal fps) const {
    Q_UNUSED(scene)
    Q_UNUSED(fps)
    return kGlbHostTimelineFrames;
}

QVector<GltfAnimationLayer> GlbClipTrackList::buildLayers(
        const GltfSceneData &scene,
        const qreal sampleFrame,
        const qreal fps) const {
    QVector<GltfAnimationLayer> layers;
    if(scene.fAnimations.isEmpty() || fps <= 0.0) {
        return layers;
    }

    layers.reserve(ca_getNumberOfChildren());
    for(int i = 0; i < ca_getNumberOfChildren(); ++i) {
        const auto* const track = getChild(i);
        const qreal relFrame = sampleFrame;
        if(!track->enabled(relFrame)) {
            continue;
        }

        const int clipIndex = track->clipIndex(relFrame);
        if(clipIndex < 0 || clipIndex >= scene.fAnimations.count()) {
            continue;
        }

        const float duration = scene.fAnimations.at(clipIndex).fDurationSeconds;
        const float trackWeight = float(track->weight(relFrame));
        if(trackWeight <= 0.f) {
            continue;
        }

        const float playbackSpeed = float(track->speed(relFrame));
        const float offsetFrames = float(track->timeOffsetFrames(relFrame));
        float timeSeconds = float(sampleFrame / fps);
        timeSeconds = timeSeconds * playbackSpeed + offsetFrames / float(fps);

        if(track->loopMode(relFrame) == GlbClipLoopMode::loop) {
            timeSeconds = wrapTimeSeconds(timeSeconds, duration);
        } else {
            timeSeconds = qBound(0.f, timeSeconds, duration);
        }

        layers.append({clipIndex, timeSeconds, trackWeight});
    }
    return layers;
}

qsptr<GlbClipTrack> GlbClipTrackList::createTrack(const QString &name) const {
    const auto track = enve::make_shared<GlbClipTrack>();
    track->prp_setName(name);
    if(ca_getNumberOfChildren() > 0) {
        const auto* const primary = getChild(0);
        track->setClipNames(primary->clipAnimator()->getValueNames());
        track->setClipIndex(primary->clipAnimator()->getCurrentValue());
    }
    return track;
}

void GlbClipTrackList::addTrack() {
    addChild(createTrack(makeNameUnique(QStringLiteral("track %1")
                                        .arg(ca_getNumberOfChildren() + 1))));
}
