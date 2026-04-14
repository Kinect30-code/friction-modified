#ifndef PLUGINMANAGER_H
#define PLUGINMANAGER_H

#include <QList>
#include <QString>

#include "core_global.h"

enum class PluginFeature {
    oraImport,
    aeMasks,
    puppetTool,
    webmAlphaPolicy,
    particleSystem,
    deepGlow,
    glbViewer
};

struct CORE_EXPORT PluginInfo {
    PluginFeature fFeature;
    QString fName;
    QString fCategory;
    QString fDescription;
    QString fSettingKey;
    bool fEnabledByDefault = true;
};

class CORE_EXPORT PluginManager {
public:
    static QList<PluginInfo> plugins();
    static PluginInfo info(const PluginFeature feature);

    static bool isEnabled(const PluginFeature feature);
    static bool defaultEnabled(const PluginFeature feature);
    static void setEnabled(const PluginFeature feature, const bool enabled);
};

#endif // PLUGINMANAGER_H
