#include "blendeffect.h"
#include "Boxes/pathbox.h"
#include "Boxes/containerbox.h"
#include "Boxes/smartvectorpath.h"
#include "Animators/SmartPath/smartpathcollection.h"

BlendEffect::BlendEffect(const QString& name,
                         const BlendEffectType type) :
    eEffect(name), mType(type) {
    mClipPath = enve::make_shared<BoxTargetProperty>("clip path");
    mClipPath->setValidator<PathBox>();
    connect(mClipPath.get(), &BoxTargetProperty::targetSet,
            this, [this](BoundingBox* const newClipBox) {
        auto& conn = mClipBox.assign(newClipBox);
        if(newClipBox) {
            conn << connect(newClipBox, &Property::prp_currentFrameChanged,
                            this, &Property::prp_currentFrameChanged);
            conn << connect(newClipBox, &Property::prp_absFrameRangeChanged,
                            this, &Property::prp_afterChangedAbsRange);
            if(const auto vectorPath = enve_cast<SmartVectorPath*>(newClipBox)) {
                if(auto* const pathAnimator = vectorPath->getPathAnimator()) {
                    conn << connect(pathAnimator, &Property::prp_currentFrameChanged,
                                    this, &Property::prp_currentFrameChanged);
                    conn << connect(pathAnimator, &Property::prp_absFrameRangeChanged,
                                    this, &Property::prp_afterChangedAbsRange);
                }
            }
        }
    });

    ca_addChild(mClipPath);
}

void BlendEffect::prp_readProperty_impl(eReadStream& src) {
    if(src.evFileVersion() < 13) {
        StaticComplexAnimator::prp_readProperty_impl(src);
    } else eEffect::prp_readProperty_impl(src);
}

void BlendEffect::writeIdentifier(eWriteStream& dst) const {
    dst.write(&mType, sizeof(BlendEffectType));
}

void BlendEffect::writeIdentifierXEV(QDomElement& ele) const {
    ele.setAttribute("type", static_cast<int>(mType));
}

void BlendEffect::prp_setupTreeViewMenu(PropertyMenu * const menu) {
    if(menu->hasActionsForType<BlendEffect>()) return;
    menu->addedActionsForType<BlendEffect>();
    {
        const PropertyMenu::PlainSelectedOp<BlendEffect> dOp =
        [](BlendEffect* const prop) {
            if(prop->mType == BlendEffectType::layerMask) {
                if(const auto clip = prop->clipPathSource()) {
                    const auto parent = clip->getParentGroup();
                    if(parent &&
                       (parent->prp_getName() == QStringLiteral("__AE_LAYER_MASKS__") ||
                        parent->prp_getName() == QStringLiteral("Masks"))) {
                        clip->removeFromParent_k();
                    }
                }
            }
            const auto parent = prop->template getParent<
                    DynamicComplexAnimatorBase<BlendEffect>>();
            parent->removeChild(prop->template ref<BlendEffect>());
        };
        menu->addPlainAction(QIcon::fromTheme("trash"), tr("Delete"), dOp);
    }

    menu->addSeparator();
    StaticComplexAnimator::prp_setupTreeViewMenu(menu);
}

bool BlendEffect::prp_dependsOn(const Property* const prop) const {
    if(eEffect::prp_dependsOn(prop)) {
        return true;
    }
    const auto target = clipPathSource();
    return target && target->prp_dependsOn(prop);
}

FrameRange BlendEffect::prp_getIdenticalRelRange(const int relFrame) const {
    const auto local = eEffect::prp_getIdenticalRelRange(relFrame);
    const auto target = clipPathSource();
    if(!target) {
        return local;
    }

    const qreal absFrameF = prp_relFrameToAbsFrameF(relFrame);
    const int targetRelFrame = qRound(target->prp_absFrameToRelFrameF(absFrameF));
    const auto targetRange = target->prp_getIdenticalRelRange(targetRelFrame);
    const int targetAbsMin = target->prp_relFrameToAbsFrame(targetRange.fMin);
    const int targetAbsMax = target->prp_relFrameToAbsFrame(targetRange.fMax);
    const FrameRange mapped = {prp_absFrameToRelFrame(targetAbsMin),
                               prp_absFrameToRelFrame(targetAbsMax)};
    return local * mapped;
}

bool BlendEffect::isPathValid() const {
    return clipPathSource();
}

SkPath BlendEffect::clipPath(const qreal relFrame) const {
    const auto target = clipPathSource();
    if(!target) return SkPath();
    const qreal absFrame = prp_relFrameToAbsFrameF(relFrame);
    const qreal tRelFrame = target->prp_absFrameToRelFrameF(absFrame);
    return target->getAbsolutePath(tRelFrame);
}

void BlendEffect::setClipPathSource(PathBox * const source)
{
    mClipPath->setTarget(source);
}

PathBox *BlendEffect::clipPathSource() const {
    const auto target = mClipPath->getTarget();
    return enve_cast<PathBox*>(target);
}
