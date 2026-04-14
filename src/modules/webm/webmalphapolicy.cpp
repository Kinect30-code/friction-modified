#include "webmalphapolicy.h"

#include "../../core/FileCacheHandlers/videostreamsdata.h"
#include "../../core/pluginmanager.h"

#include <QFileInfo>

namespace WebmAlphaPolicy {

bool shouldNormalizeTransparentEdges(const VideoStreamsData& openedVideo,
                                     const bool srcHasAlpha) {
    if(!PluginManager::isEnabled(PluginFeature::webmAlphaPolicy)) {
        return false;
    }
    if(!srcHasAlpha) {
        return false;
    }
    const QString suffix =
            QFileInfo(openedVideo.fPath).suffix().trimmed().toLower();
    return suffix == QStringLiteral("webm");
}

void premultiplyRgbaBuffer(unsigned char* rgbaPixels,
                           const int width,
                           const int height,
                           const int bytesPerLine) {
    if(!rgbaPixels || width <= 0 || height <= 0 || bytesPerLine <= 0) {
        return;
    }
    for(int y = 0; y < height; ++y) {
        unsigned char* row = rgbaPixels + y*bytesPerLine;
        for(int x = 0; x < width; ++x) {
            unsigned char* px = row + x*4;
            const unsigned char a = px[3];
            px[0] = static_cast<unsigned char>((px[0]*a + 127)/255);
            px[1] = static_cast<unsigned char>((px[1]*a + 127)/255);
            px[2] = static_cast<unsigned char>((px[2]*a + 127)/255);
        }
    }
}

}
