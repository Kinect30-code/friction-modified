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

#include "motionblureffect.h"
#include "Boxes/boxrenderdata.h"
#include "Animators/qrealanimator.h"
#include "Boxes/boundingbox.h"
#include "appsupport.h"

#include <QtMath>

class MotionBlurCaller : public RasterEffectCaller {
    e_OBJECT
    friend class StdSelfRef;
    MotionBlurCaller(const HardwareSupport hwSupport,
                      const QVector<qreal>& weights,
                      const QList<stdsptr<BoxRenderData>>& samples,
                      const qreal totalWeight) :
        RasterEffectCaller(hwSupport),
        mWeights(weights),
        mSamples(samples),
        mTotalWeight(totalWeight) {}
public:
    void processGpu(QGL33 * const gl,
                    GpuRenderTools& renderTools);
    void processCpu(CpuRenderTools& renderTools,
                    const CpuRenderData &data);
    bool srcDstSeparation() const { return false; }
private:
    static void sDrawSample(const stdsptr<BoxRenderData>& sample,
                            const qreal weight,
                            GpuRenderTools &renderTools,
                            QGL33 * const gl);
    static void sInitialize(QGL33 * const gl);
    static bool sInitialized;

    static GLuint sProgramId;
    static GLint sOpacity2Loc;
    static GLint sRect2Loc;

    const QVector<qreal> mWeights;
    const QList<stdsptr<BoxRenderData>> mSamples;
    const qreal mTotalWeight;
};

MotionBlurEffect::MotionBlurEffect() :
    RasterEffect("motion blur",
                 AppSupport::getRasterEffectHardwareSupport("MotionBlur",
                                                            HardwareSupport::cpuOnly),
                 false,
                 RasterEffectType::MOTION_BLUR)
{
    mOpacity = enve::make_shared<QrealAnimator>("opacity");
    mOpacity->setValueRange(0, 999);
    mOpacity->setCurrentBaseValue(100);

    mNumberSamples = enve::make_shared<QrealAnimator>("samples count");
    mNumberSamples->setValueRange(0, 99);
    mNumberSamples->setCurrentBaseValue(1);

    mFrameStep = enve::make_shared<QrealAnimator>("frame step");
    mFrameStep->setValueRange(-999, 999);
    mFrameStep->setCurrentBaseValue(1);

    ca_addChild(mOpacity);
    ca_addChild(mNumberSamples);
    ca_addChild(mFrameStep);

    connect(this, &Property::prp_parentChanged,
            this, [this]() {
        mParentBox = getFirstAncestor<BoundingBox>();
    });
}

class MotionBlurEffectBlock {
public:
    MotionBlurEffectBlock(bool& block) : mBlock(block) { mBlock = true; }
    ~MotionBlurEffectBlock() { mBlock = false; }
private:
    bool& mBlock;
};

stdsptr<RasterEffectCaller> MotionBlurEffect::getEffectCaller(
            const qreal relFrame, const qreal resolution,
            const qreal influence, BoxRenderData * const data) const {
    Q_UNUSED(resolution)
    if(mBlocked) return nullptr;
    const MotionBlurEffectBlock block(mBlocked);
    const auto idRange = mParentBox->prp_getIdenticalRelRange(relFrame);
    const qreal sampleCount = mNumberSamples->getEffectiveValue(relFrame)*influence;
    const qreal opacity = mOpacity->getEffectiveValue(relFrame)*0.01*influence;
    const qreal frameStep = mFrameStep->getEffectiveValue(relFrame);
    if(isZero4Dec(frameStep)) return nullptr;

    const int nSamples = qCeil(sampleCount);
    if(nSamples <= 0) return nullptr;

    qreal sampleRelFrame = relFrame - nSamples*frameStep;
    QList<stdsptr<BoxRenderData>> samples;
    for(int i = 0; i < nSamples; i++) {
        if(!idRange.inRange(sampleRelFrame)) {
            const auto sample = mParentBox->queExternalRender(sampleRelFrame, true);
            if(sample) {
                if(sample->finished()) {
                    data->fOtherGlobalRects << sample->fGlobalRect;
                } else {
                    sample->fMotionBlurTarget = data;
                    sample->addDependent(data);
                }
                samples << sample;
            }
        }
        sampleRelFrame += frameStep;
    }
    if(samples.isEmpty()) return nullptr;

    const int n = samples.size();
    QVector<qreal> weights(n);
    const qreal sigma = n * 0.4;
    qreal sum = 0;
    for(int i = 0; i < n; i++) {
        const qreal x = i - (n - 1) * 0.5;
        const qreal w = qExp(-0.5 * (x * x) / (sigma * sigma));
        weights[i] = w;
        sum += w;
    }
    const qreal totalWeight = qMin(sum, 1.0) * opacity;
    for(int i = 0; i < n; i++) {
        weights[i] = (weights[i] / sum) * totalWeight;
    }

    return enve::make_shared<MotionBlurCaller>(
                instanceHwSupport(), weights, samples, totalWeight);
}

FrameRange MotionBlurEffect::getMotionBlurPropsIdenticalRange(const int relFrame) const
{
    auto range = mParentBox ? mParentBox->getMotionBlurIdenticalRange(relFrame, true) : FrameRange::EMINMAX;
    if(range == FrameRange::EMINMAX) return range;
    const qreal nSamples = mNumberSamples->getEffectiveValue(relFrame);
    const qreal frameStep = mFrameStep->getEffectiveValue(relFrame);
    const qreal marginF = nSamples*frameStep;
    const int margin = marginF > 0 ? qCeil(marginF) : qFloor(marginF);
    if(margin == 0) return range;
    const int positive = margin > 0 ? margin : 0;
    const int negative = margin < 0 ? -margin : 0;
    if(relFrame - range.fMin < positive ||
       range.fMax - relFrame < negative) return {relFrame, relFrame};
    if(range.fMin == FrameRange::EMIN) {
        range.fMax -= negative;
    } else if(range.fMax == FrameRange::EMAX) {
        range.fMin += positive;
    } else {
        range = {qMin(relFrame, range.fMin + positive),
                 qMax(relFrame, range.fMax - negative)};
    }
    return range;
}

FrameRange MotionBlurEffect::prp_getIdenticalRelRange(const int relFrame) const {
    const auto propsRange = getMotionBlurPropsIdenticalRange(relFrame);
    const auto effectRange = RasterEffect::prp_getIdenticalRelRange(relFrame);
    return propsRange*effectRange;
}

static void blendPixelsAdditive(const int x0, const int y0,
                                const SkPixmap &dst,
                                const SkPixmap &src,
                                const qreal weight) {
    uint8_t *dstD = static_cast<uint8_t*>(dst.writable_addr());
    const uint8_t *srcD = static_cast<const uint8_t*>(src.addr());
    const int dstRowWidth = dst.rowBytesAsPixels();
    const int srcRowWidth = src.rowBytesAsPixels();

    int dstId = (y0 * dstRowWidth + x0) * 4;
    int srcId = 0;
    const int yMax = qMin(src.height(), dst.height() - y0);
    const int xMax = qMin(src.width(), dst.width() - x0);
    const int iDstYInc = qMax(0, dstRowWidth - xMax) * 4;
    const int iSrcYInc = qMax(0, srcRowWidth - xMax) * 4;
    for(int y = 0; y < yMax; y++) {
        for(int x = 0; x < xMax; x++) {
            const qreal sa = (srcD[srcId + 3] / 255.0) * weight;
            dstD[dstId]     = static_cast<uint8_t>(qBound(0., dstD[dstId]     * (1.0 - sa) + srcD[srcId]     * sa, 255.));
            dstD[dstId + 1] = static_cast<uint8_t>(qBound(0., dstD[dstId + 1] * (1.0 - sa) + srcD[srcId + 1] * sa, 255.));
            dstD[dstId + 2] = static_cast<uint8_t>(qBound(0., dstD[dstId + 2] * (1.0 - sa) + srcD[srcId + 2] * sa, 255.));
            dstD[dstId + 3] = static_cast<uint8_t>(qBound(0., dstD[dstId + 3] + srcD[srcId + 3] * weight * 0.5, 255.));
            dstId += 4;
            srcId += 4;
        }
        dstId += iDstYInc;
        srcId += iSrcYInc;
    }
}

bool MotionBlurCaller::sInitialized = false;
GLuint MotionBlurCaller::sProgramId = 0;
GLint MotionBlurCaller::sOpacity2Loc = 0;
GLint MotionBlurCaller::sRect2Loc = 0;

void MotionBlurCaller::sInitialize(QGL33 * const gl) {
    try {
        gIniProgram(gl, sProgramId, GL_TEXTURED_VERT,
                   ":/shaders/motionblur.frag");
    } catch(...) {
        RuntimeThrow("Could not initialize a program for MotionBlurCaller");
    }

    gl->glUseProgram(sProgramId);
    const GLint texture1 = gl->glGetUniformLocation(sProgramId, "tex1");
    gl->glUniform1i(texture1, 0);
    const GLint texture2 = gl->glGetUniformLocation(sProgramId, "tex2");
    gl->glUniform1i(texture2, 1);
    sOpacity2Loc = gl->glGetUniformLocation(sProgramId, "opacity2");
    sRect2Loc = gl->glGetUniformLocation(sProgramId, "rect2");
}

void MotionBlurCaller::sDrawSample(const stdsptr<BoxRenderData>& sample,
                                   const qreal weight,
                                   GpuRenderTools &renderTools,
                                   QGL33 * const gl) {
    const auto& sampleImg = sample->fRenderedImage;
    if(!sampleImg) return;
    eTexture texture2;
    renderTools.imageToTexture(sampleImg, texture2);
    renderTools.switchToOpenGL(gl);

    gl->glActiveTexture(GL_TEXTURE1);
    texture2.bind(gl);
    gl->glUniform1f(sOpacity2Loc, static_cast<float>(weight));

    {
        const auto& srcGlobalRect = sample->fGlobalRect;
        const auto& dstGlobalRect = renderTools.fGlobalRect;
        const float left = srcGlobalRect.left() - dstGlobalRect.left();
        const float right = srcGlobalRect.right() - dstGlobalRect.left();
        const float top = srcGlobalRect.top() - dstGlobalRect.top();
        const float bottom = srcGlobalRect.bottom() - dstGlobalRect.top();
        const float width = dstGlobalRect.width();
        const float height = dstGlobalRect.height();
        gl->glUniform4f(sRect2Loc, left/width, top/height,
                        (right + 1)/width, (bottom + 1)/height);
    }

    gl->glBindVertexArray(renderTools.getSquareVAO());
    gl->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void MotionBlurCaller::processGpu(QGL33 * const gl,
                                  GpuRenderTools &renderTools) {
    renderTools.switchToOpenGL(gl);
    if(!sInitialized) {
        sInitialize(gl);
        sInitialized = true;
    }
    gl->glUseProgram(sProgramId);

    for(int i = 0; i < mSamples.size(); i++) {
        renderTools.requestTargetFbo().bind(gl);
        gl->glClear(GL_COLOR_BUFFER_BIT);
        gl->glActiveTexture(GL_TEXTURE0);
        renderTools.getSrcTexture().bind(gl);
        sDrawSample(mSamples[i], mWeights[i], renderTools, gl);
        renderTools.swapTextures();
    }
}

void MotionBlurCaller::processCpu(CpuRenderTools& renderTools,
                                   const CpuRenderData &data) {
    const auto& bitmap = renderTools.fDstBtmp;
    SkPixmap pixmap;
    if(!bitmap.peekPixels(&pixmap)) return;
    for(int i = 0; i < mSamples.size(); i++) {
        const auto& sample = mSamples[i];
        const qreal weight = mWeights[i];
        const auto& srcImg = sample->fRenderedImage;
        if(!srcImg) continue;
        const auto rasterImg = srcImg->makeRasterImage();
        if(!rasterImg) continue;
        SkPixmap samplePixmap;
        if(!rasterImg->peekPixels(&samplePixmap)) continue;
        QPoint offset = sample->fGlobalRect.topLeft() - data.fPos;
        SkPixmap samplePart;
        const auto sampleTile = data.fTexTile.makeOffset(-offset.x(), -offset.y());
        if(!samplePixmap.extractSubset(&samplePart, sampleTile)) continue;
        const int drawX = sampleTile.x() < 0 ? offset.x() - data.fTexTile.x() : 0;
        const int drawY = sampleTile.y() < 0 ? offset.y() - data.fTexTile.y() : 0;
        blendPixelsAdditive(drawX, drawY, pixmap, samplePart, weight);
    }
}
