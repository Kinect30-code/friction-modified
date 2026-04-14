#include "pluginmanager.h"

#include "appsupport.h"

namespace {

const QList<PluginInfo>& pluginInfos() {
    static const QList<PluginInfo> kPlugins = {
        {PluginFeature::oraImport,
         QStringLiteral("ORA Import"),
         QStringLiteral("Importer"),
         QStringLiteral("OpenRaster import routed through the ORA module."),
         QStringLiteral("OraImportEnabled"),
         true},
        {PluginFeature::aeMasks,
         QStringLiteral("AE Masks"),
         QStringLiteral("Tools"),
         QStringLiteral("AE-style mask creation helpers and mask-path workflow."),
         QStringLiteral("AeMasksEnabled"),
         true},
        {PluginFeature::puppetTool,
         QStringLiteral("Puppet Pin Tool"),
         QStringLiteral("Tools"),
         QStringLiteral("Puppet pin placement workflow routed through the puppet module."),
         QStringLiteral("PuppetToolEnabled"),
         true},
        {PluginFeature::webmAlphaPolicy,
         QStringLiteral("WEBM Alpha Policy"),
         QStringLiteral("Media"),
         QStringLiteral("Transparent-edge premultiply fixup for WEBM alpha video frames."),
         QStringLiteral("WebmAlphaPolicyEnabled"),
         true},
        {PluginFeature::particleSystem,
         QStringLiteral("Particle System"),
         QStringLiteral("Raster Effect"),
         QStringLiteral("Module-owned particle raster effect."),
         QStringLiteral("ParticleSystemPluginEnabled"),
         true},
        {PluginFeature::deepGlow,
         QStringLiteral("Deep Glow"),
         QStringLiteral("Raster Effect"),
         QStringLiteral("Optional Deep Glow raster effect exposed through the plugin manager."),
         QStringLiteral("DeepGlowPluginEnabled"),
         true},
        {PluginFeature::glbViewer,
         QStringLiteral("GLB Viewer"),
         QStringLiteral("3D"),
         QStringLiteral("Sandboxed GLB/glTF layer importer with placeholder preview, transform controls, and animation list metadata."),
         QStringLiteral("GlbViewerPluginEnabled"),
         true}
    };
    return kPlugins;
}

}

QList<PluginInfo> PluginManager::plugins() {
    return pluginInfos();
}

PluginInfo PluginManager::info(const PluginFeature feature) {
    for(const auto& info : pluginInfos()) {
        if(info.fFeature == feature) {
            return info;
        }
    }
    return {feature,
            QStringLiteral("Unknown"),
            QStringLiteral("Unknown"),
            QStringLiteral("Unknown plugin feature."),
            QStringLiteral("UnknownPluginEnabled"),
            true};
}

bool PluginManager::isEnabled(const PluginFeature feature) {
    const auto plugin = info(feature);
    return AppSupport::getSettings(QStringLiteral("plugins"),
                                   plugin.fSettingKey,
                                   plugin.fEnabledByDefault).toBool();
}

bool PluginManager::defaultEnabled(const PluginFeature feature) {
    return info(feature).fEnabledByDefault;
}

void PluginManager::setEnabled(const PluginFeature feature, const bool enabled) {
    const auto plugin = info(feature);
    AppSupport::setSettings(QStringLiteral("plugins"),
                            plugin.fSettingKey,
                            enabled);
}
