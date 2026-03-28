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

#include "displacementmapeffect.h"

#include "Animators/qrealanimator.h"
#include "Boxes/boxrenderdata.h"
#include "Boxes/boundingbox.h"
#include "RasterEffects/rastereffectcaller.h"
#include "appsupport.h"

namespace {

class DisplacementMapCaller : public RasterEffectCaller {
public:
    DisplacementMapCaller(const qreal amountX,
                          const qreal amountY,
                          const stdsptr<BoxRenderData>& mapData) :
        RasterEffectCaller(HardwareSupport::cpuOnly),
        mAmountX(amountX),
        mAmountY(amountY),
        mMapData(mapData) {}

    void processCpu(CpuRenderTools& renderTools,
                    const CpuRenderData& data) override {
        const auto mapData = mMapData;
        if(!mapData) return;
        const auto mapImage = mapData->fRenderedImage;
        if(!mapImage) return;
        const auto mapRaster = mapImage->makeRasterImage();
        if(!mapRaster) return;

        SkPixmap mapPixmap;
        if(!mapRaster->peekPixels(&mapPixmap)) return;

        const int xMin = data.fTexTile.left();
        const int xMax = data.fTexTile.right();
        const int yMin = data.fTexTile.top();
        const int yMax = data.fTexTile.bottom();

        const auto& src = renderTools.fSrcBtmp;
        auto& dst = renderTools.fDstBtmp;
        const QRect mapRect = mapData->fGlobalRect;
        const QPoint texPos = data.fPos;

        for(int yi = yMin; yi <= yMax; ++yi) {
            auto *dstPx = static_cast<uchar*>(dst.getAddr(0, yi - yMin));
            for(int xi = xMin; xi <= xMax; ++xi) {
                const int gx = texPos.x() + xi;
                const int gy = texPos.y() + yi;

                const int mapX = gx - mapRect.x();
                const int mapY = gy - mapRect.y();

                qreal dispX = 0.;
                qreal dispY = 0.;
                if(mapX >= 0 && mapY >= 0 &&
                   mapX < mapPixmap.width() && mapY < mapPixmap.height()) {
                    const auto *mapPx = static_cast<const uchar*>(mapPixmap.addr(mapX, mapY));
                    // red/green channels define signed displacement around 0.5
                    dispX = ((qreal(mapPx[0]) / 255.) - 0.5) * 2. * mAmountX;
                    dispY = ((qreal(mapPx[1]) / 255.) - 0.5) * 2. * mAmountY;
                }

                const int sampleX = qBound(0, qRound(xi + dispX), src.width() - 1);
                const int sampleY = qBound(0, qRound(yi + dispY), src.height() - 1);
                const auto *srcPx = static_cast<const uchar*>(src.getAddr(sampleX, sampleY));

                *dstPx++ = srcPx[0];
                *dstPx++ = srcPx[1];
                *dstPx++ = srcPx[2];
                *dstPx++ = srcPx[3];
            }
        }
    }

private:
    const qreal mAmountX;
    const qreal mAmountY;
    const stdsptr<BoxRenderData> mMapData;
};

}

DisplacementMapEffect::DisplacementMapEffect() :
    RasterEffect("displacement map",
                 AppSupport::getRasterEffectHardwareSupport("DisplacementMap",
                                                            HardwareSupport::cpuOnly),
                 false,
                 RasterEffectType::DISPLACEMENT_MAP) {
    mMapSource = enve::make_shared<BoxTargetProperty>("map source");
    ca_addChild(mMapSource);

    mAmountX = enve::make_shared<QrealAnimator>(20, -2000, 2000, 1, "amount x");
    mAmountY = enve::make_shared<QrealAnimator>(20, -2000, 2000, 1, "amount y");
    ca_addChild(mAmountX);
    ca_addChild(mAmountY);

    connect(mMapSource.get(), &BoxTargetProperty::targetSet,
            this, [this](BoundingBox* const target) {
        auto& conn = mMapConn.assign(target);
        if(target) {
            conn << connect(target, &Property::prp_currentFrameChanged,
                            this, &Property::prp_currentFrameChanged);
            conn << connect(target, &Property::prp_absFrameRangeChanged,
                            this, &Property::prp_afterChangedAbsRange);
        }
    });
}

BoundingBox* DisplacementMapEffect::mapSource() const {
    return mMapSource ? mMapSource->getTarget() : nullptr;
}

stdsptr<RasterEffectCaller> DisplacementMapEffect::getEffectCaller(
        const qreal relFrame,
        const qreal resolution,
        const qreal influence,
        BoxRenderData * const data) const {
    Q_UNUSED(resolution)
    if(!data) return nullptr;
    const auto source = mapSource();
    if(!source) return nullptr;
    const auto owner = getFirstAncestor<BoundingBox>();
    if(owner && source == owner) return nullptr;

    const qreal amountX = mAmountX->getEffectiveValue(relFrame) * influence;
    const qreal amountY = mAmountY->getEffectiveValue(relFrame) * influence;
    if(isZero4Dec(amountX) && isZero4Dec(amountY)) return nullptr;

    const qreal absFrame = prp_relFrameToAbsFrameF(relFrame);
    const qreal sourceRelFrame = source->prp_absFrameToRelFrameF(absFrame);

    auto mapData = source->getCurrentRenderData(sourceRelFrame);
    if(!mapData) {
        mapData = source->queExternalRender(sourceRelFrame, true);
    }
    if(!mapData) return nullptr;

    if(!mapData->finished()) {
        mapData->addDependent(data);
    }

    return enve::make_shared<DisplacementMapCaller>(amountX, amountY, mapData);
}

FrameRange DisplacementMapEffect::prp_getIdenticalRelRange(const int relFrame) const {
    const auto local = RasterEffect::prp_getIdenticalRelRange(relFrame);
    const auto source = mapSource();
    if(!source) return local;

    const int absFrame = prp_relFrameToAbsFrame(relFrame);
    const int sourceRelFrame = source->prp_absFrameToRelFrame(absFrame);
    const auto sourceRange = source->prp_getIdenticalRelRange(sourceRelFrame);

    const int srcAbsMin = source->prp_relFrameToAbsFrame(sourceRange.fMin);
    const int srcAbsMax = source->prp_relFrameToAbsFrame(sourceRange.fMax);
    const FrameRange mapped = {prp_absFrameToRelFrame(srcAbsMin),
                               prp_absFrameToRelFrame(srcAbsMax)};
    return local * mapped;
}
