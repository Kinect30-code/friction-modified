#include "puppettoolmodule.h"

#include "../../core/Boxes/boundingbox.h"
#include "../../core/pluginmanager.h"
#include "../../core/RasterEffects/puppeteffect.h"
#include "../../core/conncontextobjlist.h"

namespace PuppetToolModule {

BoundingBox* resolveTarget(BoundingBox* const currentBox,
                           const ConnContextObjList<BoundingBox*>& selectedBoxes) {
    if(!PluginManager::isEnabled(PluginFeature::puppetTool)) {
        return nullptr;
    }
    if(currentBox) {
        return currentBox;
    }
    if(selectedBoxes.isEmpty()) {
        return nullptr;
    }
    return selectedBoxes.last();
}

PuppetEffect* findEffect(BoundingBox* const target) {
    if(!PluginManager::isEnabled(PluginFeature::puppetTool)) {
        return nullptr;
    }
    if(!target) {
        return nullptr;
    }
    PuppetEffect* result = nullptr;
    target->ca_execOnDescendants([target, &result](Property* const prop) {
        if(result) {
            return;
        }
        const auto puppet = enve_cast<PuppetEffect*>(prop);
        if(!puppet) {
            return;
        }
        if(puppet->getFirstAncestor<BoundingBox>() != target) {
            return;
        }
        result = puppet;
    });
    return result;
}

PuppetEffect* ensureEffect(BoundingBox* const target) {
    if(!PluginManager::isEnabled(PluginFeature::puppetTool)) {
        return nullptr;
    }
    if(!target) {
        return nullptr;
    }
    if(auto* const existing = findEffect(target)) {
        return existing;
    }
    auto effect = enve::make_shared<PuppetEffect>();
    auto* const result = effect.get();
    target->addRasterEffect(effect);
    target->setRasterEffectsEnabled(true);
    target->refreshCanvasControls();
    return result;
}

QPointF absoluteToNormalized(BoundingBox* const target,
                             const QPointF& absPos) {
    if(!PluginManager::isEnabled(PluginFeature::puppetTool)) {
        return QPointF(0.5, 0.5);
    }
    if(!target) {
        return QPointF(0.5, 0.5);
    }
    const QRectF rect = target->getRelBoundingRect();
    const QPointF relPos = target->mapAbsPosToRel(absPos);
    const qreal width = qMax<qreal>(1., rect.width());
    const qreal height = qMax<qreal>(1., rect.height());
    return {
        qBound<qreal>(0., (relPos.x() - rect.left())/width, 1.),
        qBound<qreal>(0., (relPos.y() - rect.top())/height, 1.)
    };
}

bool addPinAtCanvasPos(BoundingBox* const target,
                       const QPointF& absPos) {
    if(!PluginManager::isEnabled(PluginFeature::puppetTool)) {
        return false;
    }
    auto* const puppet = ensureEffect(target);
    if(!puppet) {
        return false;
    }
    return puppet->addPinAtNormalized(absoluteToNormalized(target, absPos));
}

}
