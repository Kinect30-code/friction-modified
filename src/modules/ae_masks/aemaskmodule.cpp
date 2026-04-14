#include "aemaskmodule.h"

#include "../../core/canvas.h"

#include "../../core/Animators/SmartPath/smartpathanimator.h"
#include "../../core/BlendEffects/layermaskeffect.h"
#include "../../core/Boxes/boundingbox.h"
#include "../../core/Boxes/containerbox.h"
#include "../../core/Boxes/pathbox.h"
#include "../../core/Boxes/smartvectorpath.h"
#include "../../core/Private/document.h"
#include "../../core/Private/esettings.h"
#include "../../core/pluginmanager.h"

#include <QSignalBlocker>

namespace {

constexpr const char* kAeMaskStorageName = "Masks";

LayerMaskEffect* findLayerMaskEffectForPath(BoundingBox* const target,
                                            PathBox* const path) {
    if(!target || !path) return nullptr;
    LayerMaskEffect* result = nullptr;
    target->ca_execOnDescendants([&result, path](Property* const prop) {
        if(result) return;
        const auto layerMask = enve_cast<LayerMaskEffect*>(prop);
        if(!layerMask) return;
        if(layerMask->maskPathSource() != path) return;
        result = layerMask;
    });
    return result;
}

void configureAeMaskVectorPath(PathBox* const maskPath,
                               const QString& maskName) {
    if(!maskPath) {
        return;
    }
    maskPath->prp_setName(maskName);
    maskPath->prp_setDrawingOnCanvasEnabled(false);

    const auto vectorMask = enve_cast<SmartVectorPath*>(maskPath);
    if(!vectorMask) {
        return;
    }

    auto* const paths = vectorMask->getPathAnimator();
    if(!paths) {
        return;
    }
    paths->prp_setName(QStringLiteral("Path"));
    paths->prp_setDrawingOnCanvasEnabled(true);

    const int childCount = paths->ca_getNumberOfChildren();
    if(childCount > 0) {
        paths->ca_setGUIProperty(paths->ca_getChildAt<Property>(0));
    }
    for(int i = 0; i < childCount; ++i) {
        auto* const path = paths->ca_getChildAt<SmartPathAnimator>(i);
        if(!path) {
            continue;
        }
        path->prp_setName(QStringLiteral("Path"));
        path->prp_setDrawingOnCanvasEnabled(true);
    }
}

Property* firstAeMaskEditablePath(PathBox* const maskPath) {
    const auto vectorMask = enve_cast<SmartVectorPath*>(maskPath);
    if(!vectorMask) {
        return nullptr;
    }
    auto* const paths = vectorMask->getPathAnimator();
    if(!paths) {
        return nullptr;
    }
    if(paths->ca_getNumberOfChildren() > 0) {
        if(auto* const firstPath = paths->ca_getChildAt<Property>(0)) {
            return firstPath;
        }
    }
    return paths;
}

void focusAeMaskEditablePath(Canvas* const scene,
                             BoundingBox* const target,
                             PathBox* const maskPath) {
    if(!scene || !target || !maskPath) {
        return;
    }
    scene->clearBoxesSelection();
    scene->addBoxToSelection(target);
    scene->clearPointsSelection();
    scene->clearSelectedProps();
    if(auto* const editable = firstAeMaskEditablePath(maskPath)) {
        scene->addToSelectedProps(editable);
    }
    scene->requestUpdate();
}

void prepareMaskSource(PathBox* const maskPath) {
    if(!maskPath) {
        return;
    }
    const bool prevFillFlat = eSettings::instance().fLastFillFlatEnabled;
    const bool prevStrokeFlat = eSettings::instance().fLastStrokeFlatEnabled;
    maskPath->setVisible(false);
    maskPath->setVisibleForScene(false);
    if(auto* fill = maskPath->getFillSettings()) {
        const QSignalBlocker blocker(fill);
        fill->setPaintType(PaintType::NOPAINT);
    }
    if(auto* stroke = maskPath->getStrokeSettings()) {
        const QSignalBlocker blocker(stroke);
        stroke->setPaintType(PaintType::NOPAINT);
    }
    eSettings::sInstance->fLastFillFlatEnabled = prevFillFlat;
    eSettings::sInstance->fLastStrokeFlatEnabled = prevStrokeFlat;
    maskPath->setBlendModeSk(SkBlendMode::kSrcOver);
}

}

namespace AeMaskModule {

bool isDrawableTarget(BoundingBox* const box) {
    if(!PluginManager::isEnabled(PluginFeature::aeMasks)) {
        return false;
    }
    if(!box || !box->getFirstParentLayerOrSelf()) {
        return false;
    }
    return !enve_cast<PathBox*>(box);
}

QString nextMaskName(BoundingBox* const target,
                     ContainerBox* const parent) {
    if(!PluginManager::isEnabled(PluginFeature::aeMasks)) {
        return QStringLiteral("Mask 1");
    }
    if(!target) return QStringLiteral("Mask 1");
    const QString prefix = target->prp_getName() + " Mask ";
    int maxIndex = 0;
    const auto layer = target->getFirstParentLayerOrSelf();
    const auto scanContainer = [prefix, &maxIndex](ContainerBox* const container) {
        if(!container) return;
        for(const auto* child : container->getContainedBoxes()) {
            if(!child) continue;
            const QString name = child->prp_getName();
            if(!name.startsWith(prefix)) continue;
            bool ok = false;
            const int index = name.mid(prefix.length()).toInt(&ok);
            if(ok) maxIndex = qMax(maxIndex, index);
        }
    };
    scanContainer(parent);
    if(layer && layer != parent) {
        for(const auto* child : layer->getContainedBoxes()) {
            auto* group = enve_cast<ContainerBox*>(child);
            if(!group || group->prp_getName() != kAeMaskStorageName) continue;
            scanContainer(group);
            break;
        }
    }
    return prefix + QString::number(maxIndex + 1);
}

void syncSelection(Canvas* const scene,
                   BoundingBox* const target) {
    if(!PluginManager::isEnabled(PluginFeature::aeMasks)) {
        return;
    }
    if(!scene || !target) {
        return;
    }
    scene->clearSelectedProps();
    bool addedAny = false;
    target->ca_execOnDescendants([scene, &addedAny](Property* const prop) {
        const auto layerMask = enve_cast<LayerMaskEffect*>(prop);
        if(!layerMask) {
            return;
        }
        const auto vectorMask =
                enve_cast<SmartVectorPath*>(layerMask->maskPathSource());
        if(!vectorMask) {
            return;
        }
        auto* const paths = vectorMask->getPathAnimator();
        if(!paths) {
            return;
        }
        const int pathCount = paths->ca_getNumberOfChildren();
        for(int i = 0; i < pathCount; ++i) {
            if(auto* const path = paths->ca_getChildAt<Property>(i)) {
                scene->addToSelectedProps(path);
                addedAny = true;
            }
        }
    });
    if(!addedAny) {
        target->ca_execOnDescendants([scene, &addedAny](Property* const prop) {
            if(addedAny) {
                return;
            }
            const auto layerMask = enve_cast<LayerMaskEffect*>(prop);
            if(!layerMask) {
                return;
            }
            if(auto* const editable = firstAeMaskEditablePath(layerMask->maskPathSource())) {
                scene->addToSelectedProps(editable);
                addedAny = true;
            }
        });
    }
}

void attachLayerMaskEffect(BoundingBox* const target,
                           PathBox* const maskPath) {
    if(!PluginManager::isEnabled(PluginFeature::aeMasks)) {
        return;
    }
    if(!target || !maskPath) return;

    ContainerBox* maskStorage = nullptr;
    if(const auto layer = target->getFirstParentLayerOrSelf()) {
        for(const auto* child : layer->getContainedBoxes()) {
            auto* group = enve_cast<ContainerBox*>(child);
            if(!group || group->prp_getName() != kAeMaskStorageName) continue;
            maskStorage = group;
            break;
        }
        if(!maskStorage) {
            const auto storage = enve::make_shared<ContainerBox>(
                        QString::fromLatin1(kAeMaskStorageName), eBoxType::group);
            storage->SWT_hide();
            storage->setVisibleForScene(false);
            layer->addContained(storage);
            maskStorage = storage.get();
        }
    }

    if(maskStorage) {
        const auto child = maskPath->ref<eBoxOrSound>();
        auto* const oldParent = maskPath->getParentGroup();
        if(oldParent && oldParent != maskStorage) {
            oldParent->removeContained_k(child);
        }
        if(maskPath->getParentGroup() != maskStorage) {
            maskStorage->addContained(child);
        }
    }

    prepareMaskSource(maskPath);
    maskPath->setParentTransformKeepTransform(target->getTransformAnimator());
    if(enve_cast<SmartVectorPath*>(maskPath)) {
        configureAeMaskVectorPath(maskPath, maskPath->prp_getName());
        if(auto* const vectorMask = enve_cast<SmartVectorPath*>(maskPath)) {
            if(auto* const pathAnimator = vectorMask->getPathAnimator()) {
                pathAnimator->anim_setAbsFrame(target->anim_getCurrentAbsFrame());
                pathAnimator->prp_afterChangedCurrent(UpdateReason::userChange);
            }
        }
    }
    maskPath->anim_setAbsFrame(target->anim_getCurrentAbsFrame());

    const auto effect = enve::make_shared<LayerMaskEffect>();
    effect->setClipPathSource(maskPath);
    target->addBlendEffect(effect);
    effect->syncMaskDisplayName();
    target->ensureBlendEffectsVisible();
    target->refreshCanvasControls();
    target->prp_afterWholeInfluenceRangeChanged();
    if(auto* const scene = target->getParentScene()) {
        if(target->isSelected()) {
            syncSelection(scene, target);
        }
        scene->requestUpdate();
    }
}

void finalizeShapePath(Canvas* const scene,
                       PathBox* const maskPath) {
    if(!PluginManager::isEnabled(PluginFeature::aeMasks)) {
        return;
    }
    if(!scene || !maskPath) return;
    if(enve_cast<SmartVectorPath*>(maskPath)) return;

    const auto storage = maskPath->getParentGroup();
    if(!storage || storage->prp_getName() != QString::fromLatin1(kAeMaskStorageName)) {
        return;
    }
    const auto target = storage->getFirstParentLayerOrSelf();
    if(!target) return;

    const auto layerMask = findLayerMaskEffectForPath(target, maskPath);
    if(!layerMask) return;

    auto* const maskParentTransform = maskPath->getParentTransform();
    const auto vectorMask = maskPath->objectToVectorPathBox();
    if(!vectorMask) return;

    prepareMaskSource(vectorMask);
    vectorMask->setParentTransform(maskParentTransform);
    configureAeMaskVectorPath(vectorMask, maskPath->prp_getName());

    layerMask->setClipPathSource(vectorMask);
    maskPath->removeFromParent_k();
    target->prp_afterWholeInfluenceRangeChanged();
    focusAeMaskEditablePath(scene, target, vectorMask);
    if(Document::sInstance) Document::sInstance->actionFinished();
}

BoundingBox* resolveTarget(BoundingBox* const currentBox,
                           const ConnContextObjList<BoundingBox*>& selectedBoxes) {
    if(!PluginManager::isEnabled(PluginFeature::aeMasks)) {
        return nullptr;
    }
    const auto toMaskTarget = [](BoundingBox* const box) -> BoundingBox* {
        if(!box) return nullptr;
        if(box->prp_getName() == QString::fromLatin1(kAeMaskStorageName)) {
            return nullptr;
        }
        return isDrawableTarget(box) ? box : nullptr;
    };
    if(const auto target = toMaskTarget(currentBox)) {
        return target;
    }
    for(const auto& box : selectedBoxes) {
        if(const auto target = toMaskTarget(box)) {
            return target;
        }
    }
    return nullptr;
}

}
