#include "deepgloweffect.h"

#include "Animators/coloranimator.h"
#include "Animators/qrealanimator.h"
#include "appsupport.h"
#include "pluginmanager.h"
#include "skia/skiahelpers.h"
#include "include/core/SkData.h"
#include "include/effects/SkRuntimeEffect.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace {
QMargins radiusToMargin(const qreal radius)
{
    return QMargins() + qCeil(radius);
}

float clamp01(const float value)
{
    return std::max(0.f, std::min(1.f, value));
}

static sk_sp<SkRuntimeEffect> sDeepGlowRuntimeEffect;
static bool sDeepGlowRuntimeEffectFailed = false;

const char *kDeepGlowColorFilterSkSL = R"(
uniform float threshold;
uniform float exposure;
uniform half4 color;

void main(inout half4 pixel) {
    if (pixel.a <= 0) {
        pixel = half4(0);
        return;
    }

    half3 unpremul = pixel.rgb / pixel.a;
    half luminance = dot(unpremul, half3(0.2126, 0.7152, 0.0722));
    half thresholdSpan = max(0.001, 1.0 - threshold);
    half normalized = saturate((luminance - threshold) / thresholdSpan);
    half highlight = normalized * normalized * (3.0 - 2.0 * normalized);
    half glowAmount = pixel.a * (1.0 - exp(-highlight * max(exposure, 0.001) * 4.0)) * color.a;

    pixel = half4(unpremul * color.rgb * glowAmount, glowAmount);
}
)";

struct GlowLayer {
    float radiusScale;
    float weight;
};

constexpr std::array<GlowLayer, 4> kGlowLayers{{
    {0.2f, 0.46f},
    {0.45f, 0.28f},
    {0.75f, 0.18f},
    {1.0f, 0.12f}
}};

sk_sp<SkRuntimeEffect> deepGlowRuntimeEffect()
{
    if (sDeepGlowRuntimeEffect || sDeepGlowRuntimeEffectFailed) {
        return sDeepGlowRuntimeEffect;
    }

    SkString errorText;
    std::tie(sDeepGlowRuntimeEffect, errorText) =
        SkRuntimeEffect::Make(SkString(kDeepGlowColorFilterSkSL));
    if (!sDeepGlowRuntimeEffect) {
        sDeepGlowRuntimeEffectFailed = true;
        return nullptr;
    }

    return sDeepGlowRuntimeEffect;
}

sk_sp<SkData> createDeepGlowUniformData(const sk_sp<SkRuntimeEffect> &effect,
                                        const float threshold,
                                        const float exposure,
                                        const QColor &color)
{
    if (!effect) {
        return nullptr;
    }

    const size_t uniformSize = effect->uniformSize();
    if (uniformSize == 0u) {
        return nullptr;
    }

    auto data = SkData::MakeUninitialized(uniformSize);
    if (!data) {
        return nullptr;
    }

    std::memset(data->writable_data(), 0, uniformSize);

    const float thresholdValue = threshold;
    const float exposureValue = exposure;
    const float colorValue[] = {
        static_cast<float>(color.redF()),
        static_cast<float>(color.greenF()),
        static_cast<float>(color.blueF()),
        static_cast<float>(color.alphaF())
    };

    for (const auto &input : effect->inputs()) {
        if (input.fQualifier != SkRuntimeEffect::Variable::Qualifier::kUniform) {
            continue;
        }

        auto *const dst =
            static_cast<char*>(data->writable_data()) + input.fOffset;
        if (input.fName.equals("threshold")) {
            std::memcpy(dst, &thresholdValue, sizeof(thresholdValue));
        } else if (input.fName.equals("exposure")) {
            std::memcpy(dst, &exposureValue, sizeof(exposureValue));
        } else if (input.fName.equals("color")) {
            std::memcpy(dst, colorValue, sizeof(colorValue));
        }
    }

    return data;
}

class DeepGlowEffectCaller final : public RasterEffectCaller {
public:
    DeepGlowEffectCaller(const HardwareSupport hwSupport,
                         const qreal radius,
                         const qreal threshold,
                         const qreal intensity,
                         const qreal exposure,
                         const QColor &color)
        : RasterEffectCaller(hwSupport, true, radiusToMargin(radius))
        , mRadius(static_cast<float>(radius))
        , mThreshold(clamp01(static_cast<float>(threshold)))
        , mIntensity(std::max(0.f, static_cast<float>(intensity)))
        , mExposure(std::max(0.f, static_cast<float>(exposure)))
        , mColor(color) {}

    void processGpu(QGL33 * const gl,
                    GpuRenderTools& renderTools) override;
    void processCpu(CpuRenderTools& renderTools,
                    const CpuRenderData& data) override;

private:
    void buildGlowSource(const SkBitmap &src, SkBitmap &glow) const;

    const float mRadius;
    const float mThreshold;
    const float mIntensity;
    const float mExposure;
    const QColor mColor;
};
}

DeepGlowEffect::DeepGlowEffect()
    : RasterEffect("deep glow",
                   AppSupport::getRasterEffectHardwareSupport("DeepGlow",
                                                              HardwareSupport::cpuPreffered),
                   false,
                   RasterEffectType::DEEP_GLOW)
{
    mThreshold = enve::make_shared<QrealAnimator>(0.55, 0, 1, 0.01, "threshold");
    mRadius = enve::make_shared<QrealAnimator>(40, 0, 999.999, 1, "radius");
    mIntensity = enve::make_shared<QrealAnimator>(2.0, 0, 10, 0.01, "intensity");
    mExposure = enve::make_shared<QrealAnimator>(2.5, 0, 10, 0.01, "exposure");
    mColor = enve::make_shared<ColorAnimator>();

    mColor->setColor(Qt::white);

    ca_addChild(mThreshold);
    ca_addChild(mRadius);
    ca_addChild(mIntensity);
    ca_addChild(mExposure);
    ca_addChild(mColor);

    connect(mRadius.get(), &QrealAnimator::effectiveValueChanged,
            this, &RasterEffect::forcedMarginChanged);

    ca_setGUIProperty(mThreshold.get());
}

stdsptr<RasterEffectCaller> DeepGlowEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData* const data) const
{
    Q_UNUSED(data)

    if(!PluginManager::isEnabled(PluginFeature::deepGlow)) {
        return nullptr;
    }

    const qreal radius = mRadius->getEffectiveValue(relFrame)*resolution;
    const qreal intensity = mIntensity->getEffectiveValue(relFrame)*influence;
    if (isZero4Dec(radius) || isZero4Dec(intensity)) {
        return nullptr;
    }

    return enve::make_shared<DeepGlowEffectCaller>(
                instanceHwSupport(),
                radius,
                mThreshold->getEffectiveValue(relFrame),
                intensity,
                mExposure->getEffectiveValue(relFrame),
                mColor->getColor(relFrame));
}

QMargins DeepGlowEffect::getMargin() const
{
    return radiusToMargin(mRadius->getEffectiveValue());
}

void DeepGlowEffectCaller::processGpu(QGL33 * const gl,
                                      GpuRenderTools &renderTools)
{
    Q_UNUSED(gl)

    const auto effect = deepGlowRuntimeEffect();
    const auto uniformData = createDeepGlowUniformData(effect, mThreshold,
                                                       mExposure, mColor);
    renderTools.switchToSkia();
    const auto canvas = renderTools.requestTargetCanvas();
    canvas->clear(SK_ColorTRANSPARENT);

    const auto srcTex = renderTools.requestSrcTextureImageWrapper();

    if (!effect || !uniformData) {
        canvas->drawImage(srcTex, 0, 0);
        canvas->flush();
        renderTools.swapTextures();
        return;
    }

    const float sigma = mRadius*0.3333333f;

    canvas->drawImage(srcTex, 0, 0);

    const auto glowColorFilter = effect->makeColorFilter(uniformData);
    for (const auto &layer : kGlowLayers) {
        float remainingStrength = layer.weight*mIntensity;
        if (remainingStrength <= 0.f) {
            continue;
        }

        const float layerSigma = std::max(0.001f, sigma*layer.radiusScale);
        while (remainingStrength > 0.f) {
            const float layerOpacity = std::min(remainingStrength, 1.f);
            SkPaint glowPaint;
            glowPaint.setBlendMode(SkBlendMode::kPlus);
            glowPaint.setAlphaf(layerOpacity);
            glowPaint.setImageFilter(
                SkImageFilters::Blur(
                    layerSigma, layerSigma,
                    SkImageFilters::ColorFilter(glowColorFilter, nullptr)));
            canvas->drawImage(srcTex, 0, 0, &glowPaint);
            remainingStrength -= layerOpacity;
        }
    }
    canvas->flush();

    renderTools.swapTextures();
}

void DeepGlowEffectCaller::processCpu(CpuRenderTools &renderTools,
                                      const CpuRenderData &data)
{
    SkCanvas canvas(renderTools.fDstBtmp);
    canvas.clear(SK_ColorTRANSPARENT);

    const int radCeil = static_cast<int>(std::ceil(mRadius));
    const auto &srcBtmp = renderTools.fSrcBtmp;
    const auto &texTile = data.fTexTile;
    auto srcRect = texTile.makeOutset(radCeil, radCeil);
    if (!srcRect.intersect(srcRect, srcBtmp.bounds())) {
        return;
    }

    SkBitmap tileSrc;
    srcBtmp.extractSubset(&tileSrc, srcRect);

    SkBitmap glowSource;
    glowSource.allocPixels(tileSrc.info());
    buildGlowSource(tileSrc, glowSource);

    const int drawX = srcRect.left() - texTile.left();
    const int drawY = srcRect.top() - texTile.top();

    canvas.drawBitmap(tileSrc, drawX, drawY);

    const float sigma = mRadius*0.3333333f;
    for (const auto &layer : kGlowLayers) {
        float remainingStrength = layer.weight*mIntensity;
        if (remainingStrength <= 0.f) {
            continue;
        }

        const float layerSigma = std::max(0.001f, sigma*layer.radiusScale);
        while (remainingStrength > 0.f) {
            const float layerOpacity = std::min(remainingStrength, 1.f);
            SkPaint addPaint;
            addPaint.setBlendMode(SkBlendMode::kPlus);
            addPaint.setAlphaf(layerOpacity);
            addPaint.setImageFilter(
                SkImageFilters::Blur(layerSigma, layerSigma, nullptr));
            canvas.drawBitmap(glowSource, drawX, drawY, &addPaint);
            remainingStrength -= layerOpacity;
        }
    }
}

void DeepGlowEffectCaller::buildGlowSource(const SkBitmap &src, SkBitmap &glow) const
{
    for (int y = 0; y < src.height(); ++y) {
        const auto *srcRow = src.getAddr32(0, y);
        auto *dstRow = glow.getAddr32(0, y);
        for (int x = 0; x < src.width(); ++x) {
            const SkColor pixel = srcRow[x];
            const float alpha = SkColorGetA(pixel)/255.f;
            if (alpha <= 0.f) {
                dstRow[x] = SK_ColorTRANSPARENT;
                continue;
            }

            const float premulR = SkColorGetR(pixel)/255.f;
            const float premulG = SkColorGetG(pixel)/255.f;
            const float premulB = SkColorGetB(pixel)/255.f;

            const float r = clamp01(premulR/alpha);
            const float g = clamp01(premulG/alpha);
            const float b = clamp01(premulB/alpha);

            const float luminance = 0.2126f*r + 0.7152f*g + 0.0722f*b;
            const float thresholdSpan = std::max(0.001f, 1.f - mThreshold);
            const float normalized =
                clamp01((luminance - mThreshold)/thresholdSpan);
            const float highlight =
                normalized*normalized*(3.f - 2.f*normalized);
            const float glowAmount =
                alpha*(1.f - std::exp(-highlight*std::max(0.001f, mExposure)*4.f))
                * static_cast<float>(mColor.alphaF());

            if (glowAmount <= 0.f) {
                dstRow[x] = SK_ColorTRANSPARENT;
                continue;
            }

            const float tintedR = clamp01(r*static_cast<float>(mColor.redF()));
            const float tintedG = clamp01(g*static_cast<float>(mColor.greenF()));
            const float tintedB = clamp01(b*static_cast<float>(mColor.blueF()));

            dstRow[x] = SkColorSetARGB(
                static_cast<U8CPU>(std::round(glowAmount*255.f)),
                static_cast<U8CPU>(std::round(tintedR*glowAmount*255.f)),
                static_cast<U8CPU>(std::round(tintedG*glowAmount*255.f)),
                static_cast<U8CPU>(std::round(tintedB*glowAmount*255.f))
            );
        }
    }
}
