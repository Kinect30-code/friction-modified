#ifndef PUPPETTOOLMODULE_H
#define PUPPETTOOLMODULE_H

#include <QPointF>

template <typename T>
class ConnContextObjList;

class BoundingBox;
class PuppetEffect;

namespace PuppetToolModule {

BoundingBox* resolveTarget(BoundingBox* const currentBox,
                           const ConnContextObjList<BoundingBox*>& selectedBoxes);

PuppetEffect* findEffect(BoundingBox* const target);
PuppetEffect* ensureEffect(BoundingBox* const target);

QPointF absoluteToNormalized(BoundingBox* const target,
                             const QPointF& absPos);

bool addPinAtCanvasPos(BoundingBox* const target,
                       const QPointF& absPos);

}

#endif // PUPPETTOOLMODULE_H
