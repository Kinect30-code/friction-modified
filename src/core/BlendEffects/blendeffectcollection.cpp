#include "blendeffectcollection.h"

#include "Boxes/layerboxrenderdata.h"
#include "typemenu.h"

#include "moveblendeffect.h"
#include "targetedblendeffect.h"
#include "trackmatteeffect.h"
#include "layermaskeffect.h"
#include "Boxes/boundingbox.h"
#include "skia/skiaincludes.h"

namespace {

enum MaskMode {
    None = 0,
    Add = 1,
    Subtract = 2,
    Intersect = 3,
    Lighten = 4,
    Darken = 5,
    Difference = 6
};

QRectF resolveOwnerBoundsRect(const BlendEffectCollection* const collection,
                             const qreal relFrame) {
    if(!collection) {
        return QRectF();
    }
    const auto owner = collection->getFirstAncestor<BoundingBox>();
    if(!owner) {
        return QRectF();
    }
    return owner->getTotalTransformAtFrame(relFrame).mapRect(
                owner->getRelBoundingRect());
}

SkPath resolveOwnerBoundsPath(const BlendEffectCollection* const collection,
                              const qreal relFrame) {
    SkPath ownerBoundsPath;
    const auto rect = resolveOwnerBoundsRect(collection, relFrame);
    if(rect.isEmpty()) {
        return ownerBoundsPath;
    }
    ownerBoundsPath.addRect(
                SkRect::MakeLTRB(rect.left(),
                                 rect.top(),
                                 rect.right(),
                                 rect.bottom()));
    ownerBoundsPath.setFillType(SkPathFillType::kWinding);
    return ownerBoundsPath;
}

bool combineMaskPaths(const SkPath& lhs,
                      const SkPath& rhs,
                      const int mode,
                      SkPath* const out) {
    if(!out) {
        return false;
    }
    SkPathOp op = SkPathOp::kUnion_SkPathOp;
    switch(mode) {
    case Add:
    case Lighten:
        op = SkPathOp::kUnion_SkPathOp;
        break;
    case Subtract:
        op = SkPathOp::kDifference_SkPathOp;
        break;
    case Intersect:
    case Darken:
        op = SkPathOp::kIntersect_SkPathOp;
        break;
    case Difference:
        op = SkPathOp::kXOR_SkPathOp;
        break;
    default:
        return false;
    }
    return Op(lhs, rhs, op, out);
}

FrameRange layerMaskIdenticalRelRange(const BlendEffectCollection* const collection,
                                      const int relFrame) {
    if(!collection) {
        return {FrameRange::EMIN, FrameRange::EMAX};
    }

    FrameRange range{FrameRange::EMIN, FrameRange::EMAX};
    const int iMax = collection->ca_getNumberOfChildren();
    for(int i = 0; i < iMax; i++) {
        const auto effect = collection->getChild(i);
        if(!effect->isVisible()) {
            continue;
        }
        const auto layerMask = enve_cast<LayerMaskEffect*>(effect);
        if(!layerMask) {
            continue;
        }
        range *= layerMask->prp_getIdenticalRelRange(relFrame);
        if(range.isUnary()) {
            return range;
        }
    }

    return range;
}

bool composeLayerMaskPathUncached(const BlendEffectCollection* const collection,
                                  const qreal relFrame,
                                  SkPath* const outPath) {
    if(!outPath) {
        return false;
    }

    const int iMax = collection->ca_getNumberOfChildren();
    SkPath composedMaskPath;
    bool hasComposedMask = false;
    SkPath ownerBoundsPath;
            bool ownerBoundsResolved = false;

    const auto ownerBounds = [&]() -> const SkPath& {
        if(!ownerBoundsResolved) {
            ownerBoundsPath = resolveOwnerBoundsPath(collection, relFrame);
            ownerBoundsResolved = true;
        }
        return ownerBoundsPath;
    };

    for(int i = 0; i < iMax; i++) {
        const auto effect = collection->getChild(i);
        if(!effect->isVisible()) {
            continue;
        }
        const auto layerMask = enve_cast<LayerMaskEffect*>(effect);
        if(!layerMask) {
            continue;
        }

        const int mode = layerMask->modeValue();
        if(mode == None) {
            continue;
        }

        const auto path = layerMask->effectivePath(relFrame);
        if(path.isEmpty()) {
            continue;
        }

        if(!hasComposedMask) {
            if(mode == Subtract) {
                const auto& base = ownerBounds();
                if(base.isEmpty()) {
                    continue;
                }
                if(!combineMaskPaths(base, path, mode, &composedMaskPath)) {
                    continue;
                }
            } else {
                composedMaskPath = path;
            }
            hasComposedMask = true;
            continue;
        }

        SkPath combined;
        if(!combineMaskPaths(composedMaskPath, path, mode, &combined)) {
            continue;
        }
        composedMaskPath = combined;
    }

    if(!hasComposedMask) {
        return false;
    }

    *outPath = composedMaskPath;
    return true;
}

}

BlendEffectCollection::BlendEffectCollection() :
    BlendEffectCollectionBase("blend effects") {}

void BlendEffectCollection::prp_setupTreeViewMenu(PropertyMenu * const menu) {
    if(menu->hasActionsForType<BlendEffectCollection>()) return;
    menu->addedActionsForType<BlendEffectCollection>();
    {
        const PropertyMenu::PlainSelectedOp<BlendEffectCollection> aOp =
        [](BlendEffectCollection * coll) {
            coll->addChild(enve::make_shared<MoveBlendEffect>());
        };
        menu->addPlainAction(QIcon::fromTheme("effect"), tr("New Move Effect"), aOp);
    }
    {
        const PropertyMenu::PlainSelectedOp<BlendEffectCollection> aOp =
        [](BlendEffectCollection * coll) {
            coll->addChild(enve::make_shared<TargetedBlendEffect>());
        };
        menu->addPlainAction(QIcon::fromTheme("effect"), tr("New Targeted Effect"), aOp);
    }
    {
        const PropertyMenu::PlainSelectedOp<BlendEffectCollection> aOp =
        [](BlendEffectCollection * coll) {
            coll->addChild(enve::make_shared<TrackMatteEffect>());
        };
        menu->addPlainAction(QIcon::fromTheme("effect"), tr("New Track Matte"), aOp);
    }
    menu->addSeparator();
    BlendEffectCollectionBase::prp_setupTreeViewMenu(menu);
}

void BlendEffectCollection::blendSetup(
        ChildRenderData &data,
        const int index, const qreal relFrame,
        QList<ChildRenderData> &delayed) const {
    SkPath composedLayerMask;
    const bool hasLayerMask =
            composedLayerMaskPath(relFrame, &composedLayerMask);
    if(hasLayerMask) {
        data.fClip.fClipOps.append(
                    {composedLayerMask, SkClipOp::kIntersect, true});
    }

    const int iMax = ca_getNumberOfChildren();
    for(int i = 0; i < iMax; i++) {
        const auto effect = getChild(i);
        if(!effect->isVisible()) continue;
        if(enve_cast<LayerMaskEffect*>(effect)) continue;
        effect->blendSetup(data, index, relFrame, delayed);
    }
}

void BlendEffectCollection::drawBlendSetup(SkCanvas * const canvas) {
    const qreal relFrame = anim_getCurrentRelFrame();
    SkPath composedMaskPath;
    if(composedLayerMaskPath(relFrame, &composedMaskPath)) {
        if(composedMaskPath.isEmpty()) {
            canvas->clipRect(SkRect::MakeEmpty(), SkClipOp::kIntersect, true);
        } else {
            canvas->clipPath(composedMaskPath, SkClipOp::kIntersect, true);
        }
    }

    const int iMax = ca_getNumberOfChildren();
    for(int i = 0; i < iMax; i++) {
        const auto effect = getChild(i);
        if(!effect->isVisible()) continue;
        if(enve_cast<LayerMaskEffect*>(effect)) continue;
        effect->drawBlendSetup(relFrame, canvas);
    }
}

void BlendEffectCollection::detachedBlendUISetup(
        int &drawId,
        QList<BlendEffect::UIDelayed> &delayed) const {
    const qreal relFrame = anim_getCurrentRelFrame();
    const int iMax = ca_getNumberOfChildren();
    for(int i = 0; i < iMax; i++) {
        const auto effect = getChild(i);
        effect->detachedBlendUISetup(relFrame, drawId, delayed);
    }
}

void BlendEffectCollection::detachedBlendSetup(
        const BoundingBox* const boxToDraw,
        SkCanvas * const canvas,
        const SkFilterQuality filter, int &drawId,
        QList<BlendEffect::Delayed> &delayed) const {
    const qreal relFrame = anim_getCurrentRelFrame();
    const int iMax = ca_getNumberOfChildren();
    for(int i = 0; i < iMax; i++) {
        const auto effect = getChild(i);
        if(!effect->isVisible()) continue;
        effect->detachedBlendSetup(boxToDraw, relFrame, canvas, filter, drawId, delayed);
    }
}

bool BlendEffectCollection::composedLayerMaskPath(
        const qreal relFrame,
        SkPath * const outPath) const {
    if(!outPath) {
        return false;
    }

    const int relFrameI = qRound(relFrame);
    const auto * const owner = getFirstAncestor<BoundingBox>();
    const uint ownerStateId = owner ? owner->currentStateId() : 0;
    const QRectF ownerBoundsRect = resolveOwnerBoundsRect(this, relFrame);
    const FrameRange identicalRange = layerMaskIdenticalRelRange(this, relFrameI);
    {
        QReadLocker locker(&mLayerMaskPathCacheLock);
        if(mLayerMaskPathCache.fResolved &&
           mLayerMaskPathCache.fOwnerStateId == ownerStateId &&
           mLayerMaskPathCache.fOwnerBoundsRect == ownerBoundsRect &&
           mLayerMaskPathCache.fRange.inRange(relFrameI)) {
            *outPath = mLayerMaskPathCache.fPath;
            return mLayerMaskPathCache.fHasMask;
        }
    }

    SkPath composedMaskPath;
    const bool hasMask =
            composeLayerMaskPathUncached(this, relFrame, &composedMaskPath);
    {
        QWriteLocker locker(&mLayerMaskPathCacheLock);
        mLayerMaskPathCache = {true, hasMask, identicalRange, ownerStateId,
                               ownerBoundsRect,
                               composedMaskPath};
    }
    if(!hasMask) {
        return false;
    }
    *outPath = composedMaskPath;
    return true;
}

void BlendEffectCollection::prp_writeProperty_impl(eWriteStream &dst) const {
    BlendEffectCollectionBase::prp_writeProperty_impl(dst);
    dst << SWT_isVisible();
}

void BlendEffectCollection::prp_readProperty_impl(eReadStream &src) {
    if(src.evFileVersion() < 12) return;
    BlendEffectCollectionBase::prp_readProperty_impl(src);
    bool visible; src >> visible;
    SWT_setVisible(visible);
}

qsptr<BlendEffect> createBlendEffectForType(const BlendEffectType type) {
    switch(type) {
        case(BlendEffectType::move):
            return enve::make_shared<MoveBlendEffect>();
        case(BlendEffectType::targeted):
            return enve::make_shared<TargetedBlendEffect>();
        case(BlendEffectType::trackMatte):
            return enve::make_shared<TrackMatteEffect>();
        case(BlendEffectType::layerMask):
            return enve::make_shared<LayerMaskEffect>();
        default: RuntimeThrow("Invalid blend effect type '" +
                              QString::number(int(type)) + "'");
    }
}

qsptr<BlendEffect> readIdCreateBlendEffect(eReadStream &src) {
    BlendEffectType type;
    src.read(&type, sizeof(BlendEffectType));
    return createBlendEffectForType(type);
}

void writeBlendEffectType(BlendEffect * const obj, eWriteStream &dst) {
    obj->writeIdentifier(dst);
}

qsptr<BlendEffect> readIdCreateBlendEffectXEV(const QDomElement& ele) {
    const QString typeStr = ele.attribute("type");
    const int typeInt = XmlExportHelpers::stringToInt(typeStr);
    const auto type = static_cast<BlendEffectType>(typeInt);
    return createBlendEffectForType(type);
}

void writeBlendEffectTypeXEV(BlendEffect* const obj, QDomElement& ele) {
    obj->writeIdentifierXEV(ele);
}
