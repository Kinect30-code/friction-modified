#ifndef WEBMALPHAPOLICY_H
#define WEBMALPHAPOLICY_H

#include <QString>

struct VideoStreamsData;

namespace WebmAlphaPolicy {

bool shouldNormalizeTransparentEdges(const VideoStreamsData& openedVideo,
                                     const bool srcHasAlpha);

void premultiplyRgbaBuffer(unsigned char* rgbaPixels,
                           const int width,
                           const int height,
                           const int bytesPerLine);

}

#endif // WEBMALPHAPOLICY_H
