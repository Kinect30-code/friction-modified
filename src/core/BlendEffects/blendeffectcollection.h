#ifndef BLENDEFFECTCOLLECTION_H
#define BLENDEFFECTCOLLECTION_H

#include "Animators/dynamiccomplexanimator.h"

#include "blendeffect.h"
#include <QReadWriteLock>

CORE_EXPORT
qsptr<BlendEffect> readIdCreateBlendEffect(eReadStream& src);
CORE_EXPORT
void writeBlendEffectType(BlendEffect* const obj, eWriteStream& dst);

CORE_EXPORT
qsptr<BlendEffect> readIdCreateBlendEffectXEV(const QDomElement& ele);
CORE_EXPORT
void writeBlendEffectTypeXEV(BlendEffect* const obj, QDomElement& ele);

typedef DynamicComplexAnimator<
    BlendEffect,
    writeBlendEffectType,
    readIdCreateBlendEffect,
    writeBlendEffectTypeXEV,
    readIdCreateBlendEffectXEV> BlendEffectCollectionBase;

class CORE_EXPORT BlendEffectCollection : public BlendEffectCollectionBase {
    Q_OBJECT
    e_OBJECT
protected:
    BlendEffectCollection();
public:
    bool SWT_shouldFlattenHierarchy() const override { return true; }
    void prp_setupTreeViewMenu(PropertyMenu * const menu) override;

    void prp_writeProperty_impl(eWriteStream &dst) const override;
    void prp_readProperty_impl(eReadStream &src) override;

    void blendSetup(ChildRenderData &data,
                    const int index, const qreal relFrame,
                    QList<ChildRenderData> &delayed) const;
    void detachedBlendUISetup(int& drawId,
                        QList<BlendEffect::UIDelayed> &delayed) const;
    void detachedBlendSetup(const BoundingBox * const boxToDraw,
                        SkCanvas * const canvas,
                        const SkFilterQuality filter, int& drawId,
                        QList<BlendEffect::Delayed> &delayed) const;
    void drawBlendSetup(SkCanvas * const canvas);
private:
    struct LayerMaskPathCache {
        bool fResolved = false;
        bool fHasMask = false;
        FrameRange fRange = {0, -1};
        uint fOwnerStateId = 0;
        QRectF fOwnerBoundsRect;
        SkPath fPath;
    };

    bool composedLayerMaskPath(const qreal relFrame,
                               SkPath * const outPath) const;

    mutable QReadWriteLock mLayerMaskPathCacheLock;
    mutable LayerMaskPathCache mLayerMaskPathCache;
};

#endif // BLENDEFFECTCOLLECTION_H
