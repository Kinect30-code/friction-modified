#ifndef AEMASKMODULE_H
#define AEMASKMODULE_H

#include <QString>

template <typename T>
class ConnContextObjList;

class BoundingBox;
class Canvas;
class ContainerBox;
class PathBox;

namespace AeMaskModule {

bool isDrawableTarget(BoundingBox* const box);

QString nextMaskName(BoundingBox* const target,
                     ContainerBox* const parent);

void attachLayerMaskEffect(BoundingBox* const target,
                           PathBox* const maskPath);

void finalizeShapePath(Canvas* const scene,
                       PathBox* const maskPath);

BoundingBox* resolveTarget(BoundingBox* const currentBox,
                           const ConnContextObjList<BoundingBox*>& selectedBoxes);

void syncSelection(Canvas* const scene,
                   BoundingBox* const target);

}

#endif // AEMASKMODULE_H
