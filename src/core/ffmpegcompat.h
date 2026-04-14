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

#ifndef FFMPEGCOMPAT_H
#define FFMPEGCOMPAT_H

#include <cstring>

extern "C" {
    #include <libavcodec/avcodec.h>
    #include <libavcodec/codec_par.h>
    #include <libavutil/channel_layout.h>
    #include <libavutil/frame.h>
}

#if LIBAVUTIL_VERSION_MAJOR >= 57 && LIBAVCODEC_VERSION_MAJOR >= 59
#define FRICTION_HAS_AVCHANNEL_LAYOUT 1
#else
#define FRICTION_HAS_AVCHANNEL_LAYOUT 0
#endif

namespace Friction {
namespace FFmpegCompat {

inline int channelCountForMask(const uint64_t layoutMask)
{
#if FRICTION_HAS_AVCHANNEL_LAYOUT
    if (layoutMask == 0) { return 0; }
    AVChannelLayout layout{};
    if (av_channel_layout_from_mask(&layout, layoutMask) < 0) {
        return 0;
    }
    const int count = layout.nb_channels;
    av_channel_layout_uninit(&layout);
    return count;
#else
    return av_get_channel_layout_nb_channels(layoutMask);
#endif
}

#if FRICTION_HAS_AVCHANNEL_LAYOUT
inline void clearChannelLayout(AVChannelLayout *layout)
{
    if (!layout) { return; }
    av_channel_layout_uninit(layout);
    std::memset(layout, 0, sizeof(*layout));
}

inline bool setChannelLayoutFromMask(AVChannelLayout *layout,
                                     const uint64_t layoutMask)
{
    if (!layout) { return false; }
    clearChannelLayout(layout);
    if (layoutMask == 0) { return false; }
    return av_channel_layout_from_mask(layout, layoutMask) >= 0;
}

inline uint64_t channelLayoutMask(const AVChannelLayout &layout)
{
    if (layout.order == AV_CHANNEL_ORDER_NATIVE) {
        return layout.u.mask;
    }
    return 0;
}

inline int codecParametersChannelCount(const AVCodecParameters *codecParameters)
{
    return codecParameters ? codecParameters->ch_layout.nb_channels : 0;
}

inline uint64_t codecParametersChannelLayoutMask(const AVCodecParameters *codecParameters)
{
    return codecParameters ? channelLayoutMask(codecParameters->ch_layout) : 0;
}

inline int codecContextChannelCount(const AVCodecContext *codecContext)
{
    return codecContext ? codecContext->ch_layout.nb_channels : 0;
}

inline uint64_t codecContextChannelLayoutMask(const AVCodecContext *codecContext)
{
    return codecContext ? channelLayoutMask(codecContext->ch_layout) : 0;
}

inline bool setCodecContextChannelLayoutMask(AVCodecContext *codecContext,
                                             const uint64_t layoutMask)
{
    return codecContext &&
           setChannelLayoutFromMask(&codecContext->ch_layout, layoutMask);
}

inline int frameChannelCount(const AVFrame *frame)
{
    return frame ? frame->ch_layout.nb_channels : 0;
}

inline uint64_t frameChannelLayoutMask(const AVFrame *frame)
{
    return frame ? channelLayoutMask(frame->ch_layout) : 0;
}

inline bool setFrameChannelLayoutMask(AVFrame *frame,
                                      const uint64_t layoutMask)
{
    return frame &&
           setChannelLayoutFromMask(&frame->ch_layout, layoutMask);
}

inline const AVChannelLayout *codecChannelLayouts(const AVCodec *codec)
{
    return codec ? codec->ch_layouts : nullptr;
}
#else
inline int codecParametersChannelCount(const AVCodecParameters *codecParameters)
{
    return codecParameters ? codecParameters->channels : 0;
}

inline uint64_t codecParametersChannelLayoutMask(const AVCodecParameters *codecParameters)
{
    return codecParameters ? codecParameters->channel_layout : 0;
}

inline int codecContextChannelCount(const AVCodecContext *codecContext)
{
    return codecContext ? codecContext->channels : 0;
}

inline uint64_t codecContextChannelLayoutMask(const AVCodecContext *codecContext)
{
    return codecContext ? codecContext->channel_layout : 0;
}

inline bool setCodecContextChannelLayoutMask(AVCodecContext *codecContext,
                                             const uint64_t layoutMask)
{
    if (!codecContext) { return false; }
    codecContext->channel_layout = layoutMask;
    codecContext->channels = channelCountForMask(layoutMask);
    return true;
}

inline int frameChannelCount(const AVFrame *frame)
{
    return frame ? frame->channels : 0;
}

inline uint64_t frameChannelLayoutMask(const AVFrame *frame)
{
    return frame ? frame->channel_layout : 0;
}

inline bool setFrameChannelLayoutMask(AVFrame *frame,
                                      const uint64_t layoutMask)
{
    if (!frame) { return false; }
    frame->channel_layout = layoutMask;
    frame->channels = channelCountForMask(layoutMask);
    return true;
}

inline const uint64_t *codecChannelLayouts(const AVCodec *codec)
{
    return codec ? codec->channel_layouts : nullptr;
}
#endif

} // namespace FFmpegCompat
} // namespace Friction

#endif // FFMPEGCOMPAT_H
