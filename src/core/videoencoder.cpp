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

// Fork of enve - Copyright (C) 2016-2020 Maurycy Liebner

#include "videoencoder.h"
#include <QByteArray>
#include "Boxes/boxrendercontainer.h"
#include "CacheHandlers/sceneframecontainer.h"
#include "canvas.h"
#include "skia/skiahelpers.h"

extern "C" {
    #include <libavutil/pixdesc.h>
}

#define AV_RuntimeThrow(errId, message) \
{ \
    char * const errMsg = new char[AV_ERROR_MAX_STRING_SIZE]; \
    av_make_error_string(errMsg, AV_ERROR_MAX_STRING_SIZE, errId); \
    try { \
        RuntimeThrow(errMsg); \
    } catch(...) { \
        delete[] errMsg; \
        RuntimeThrow(message); \
    } \
}

using namespace Friction::Core;

VideoEncoder *VideoEncoder::sInstance = nullptr;

namespace {

bool exportDebugEnabled()
{
    static const bool enabled = qEnvironmentVariableIsSet("FRICTION_EXPORT_DEBUG");
    return enabled;
}

static bool sampleFormatSupported(const AVCodec *codec,
                                  const AVSampleFormat format)
{
    if (!codec || format == AV_SAMPLE_FMT_NONE) { return false; }
    if (!codec->sample_fmts) { return true; }
    for (const AVSampleFormat *it = codec->sample_fmts; *it != AV_SAMPLE_FMT_NONE; ++it) {
        if (*it == format) { return true; }
    }
    return false;
}

static AVSampleFormat chooseSupportedSampleFormat(const AVCodec *codec,
                                                  const AVSampleFormat requested)
{
    if (!codec) { return requested; }
    if (sampleFormatSupported(codec, requested)) { return requested; }
    if (!codec->sample_fmts) { return requested; }

    const AVSampleFormat preferred[] = {
        AV_SAMPLE_FMT_FLTP,
        AV_SAMPLE_FMT_S16P,
        AV_SAMPLE_FMT_S16,
        AV_SAMPLE_FMT_S32P,
        AV_SAMPLE_FMT_S32
    };
    for (const auto format : preferred) {
        if (sampleFormatSupported(codec, format)) { return format; }
    }
    return codec->sample_fmts[0];
}

static bool sampleRateSupported(const AVCodec *codec,
                                const int rate)
{
    if (!codec || rate <= 0) { return false; }
    if (!codec->supported_samplerates) { return true; }
    for (const int *it = codec->supported_samplerates; *it != 0; ++it) {
        if (*it == rate) { return true; }
    }
    return false;
}

static int chooseSupportedSampleRate(const AVCodec *codec,
                                     const int requested)
{
    if (!codec) { return requested; }
    if (sampleRateSupported(codec, requested)) { return requested; }
    if (!codec->supported_samplerates) {
        return requested > 0 ? requested : 48000;
    }

    const int preferredRates[] = {48000, 44100, 32000};
    for (const auto rate : preferredRates) {
        if (sampleRateSupported(codec, rate)) { return rate; }
    }
    return codec->supported_samplerates[0];
}

static bool channelLayoutSupported(const AVCodec *codec,
                                   const uint64_t layout)
{
    if (!codec || layout == 0) { return false; }
#if FRICTION_HAS_AVCHANNEL_LAYOUT
    const auto *layouts = Friction::FFmpegCompat::codecChannelLayouts(codec);
    if (!layouts) { return true; }

    AVChannelLayout requestedLayout{};
    if (!Friction::FFmpegCompat::setChannelLayoutFromMask(&requestedLayout, layout)) {
        return false;
    }

    bool supported = false;
    for (const AVChannelLayout *it = layouts; it && it->nb_channels > 0; ++it) {
        if (av_channel_layout_compare(&requestedLayout, it) == 0) {
            supported = true;
            break;
        }
    }
    av_channel_layout_uninit(&requestedLayout);
    return supported;
#else
    const auto *layouts = Friction::FFmpegCompat::codecChannelLayouts(codec);
    if (!layouts) { return true; }
    for (const uint64_t *it = layouts; *it != 0; ++it) {
        if (*it == layout) { return true; }
    }
    return false;
#endif
}

static uint64_t chooseSupportedChannelLayout(const AVCodec *codec,
                                             const uint64_t requested)
{
    if (!codec) { return requested; }
    if (channelLayoutSupported(codec, requested)) { return requested; }
    const auto *layouts = Friction::FFmpegCompat::codecChannelLayouts(codec);
    if (!layouts) {
        return requested != 0 ? requested : AV_CH_LAYOUT_STEREO;
    }
    if (channelLayoutSupported(codec, AV_CH_LAYOUT_STEREO)) { return AV_CH_LAYOUT_STEREO; }
#if FRICTION_HAS_AVCHANNEL_LAYOUT
    return Friction::FFmpegCompat::channelLayoutMask(layouts[0]);
#else
    return layouts[0];
#endif
}

static void sanitizeAudioSettings(OutputSettings &settings)
{
    const AVCodec * const codec = settings.fAudioCodec;
    if (!codec) { return; }

    settings.fAudioSampleFormat =
        chooseSupportedSampleFormat(codec, settings.fAudioSampleFormat);
    settings.fAudioSampleRate =
        chooseSupportedSampleRate(codec, settings.fAudioSampleRate);
    settings.fAudioChannelsLayout =
        chooseSupportedChannelLayout(codec, settings.fAudioChannelsLayout);

    if (settings.fAudioBitrate <= 0) {
        settings.fAudioBitrate = 192000;
    }
}

static bool pixelFormatHasAlpha(const AVPixelFormat format)
{
    const AVPixFmtDescriptor * const desc = av_pix_fmt_desc_get(format);
    return desc && (desc->flags & AV_PIX_FMT_FLAG_ALPHA);
}

}

VideoEncoder::VideoEncoder() {
    Q_ASSERT(!sInstance);
    sInstance = this;
}

void VideoEncoder::addContainer(const QueuedVideoFrame& cont) {
    if(!cont.fImage || !cont.fRange.isValid()) return;
    mNextContainers.append(cont);
    if(exportDebugEnabled()) {
        qWarning() << "[export-debug] encoder-enqueue-video"
                   << "range=[" << cont.fRange.fMin << "," << cont.fRange.fMax << "]"
                   << "queuedVideo" << mNextContainers.count()
                   << "state" << static_cast<int>(getState());
    }
    if(getState() < eTaskState::qued || getState() > eTaskState::processing) queTask();
}

void VideoEncoder::addContainer(const stdsptr<Samples>& cont) {
    if(!cont) return;
    mNextSoundConts.append(cont);
    if(exportDebugEnabled()) {
        qWarning() << "[export-debug] encoder-enqueue-audio"
                   << "sampleRange=[" << cont->fSampleRange.fMin << "," << cont->fSampleRange.fMax << "]"
                   << "queuedAudio" << mNextSoundConts.count()
                   << "state" << static_cast<int>(getState());
    }
    if(getState() < eTaskState::qued || getState() > eTaskState::processing) queTask();
}

void VideoEncoder::allAudioProvided() {
    mAllAudioProvided = true;
    if(exportDebugEnabled()) {
        qWarning() << "[export-debug] encoder-all-audio-provided"
                   << "state" << static_cast<int>(getState())
                   << "queuedAudio" << mNextSoundConts.count();
    }
    if(getState() < eTaskState::qued || getState() > eTaskState::processing) queTask();
}

bool VideoEncoder::isValidProfile(const AVCodec *codec,
                                  int profile)
{
    if (profile < 0 || !codec) { return false; }
    switch (codec->id) {
    case AV_CODEC_ID_H264:
        switch (profile) {
        case AV_PROFILE_H264_BASELINE:
        case AV_PROFILE_H264_MAIN:
        case AV_PROFILE_H264_HIGH:
            return true;
        default:;
        }
        break;
    case AV_CODEC_ID_PRORES:
        switch (profile) {
        case AV_PROFILE_PRORES_PROXY:
        case AV_PROFILE_PRORES_LT:
        case AV_PROFILE_PRORES_STANDARD:
        case AV_PROFILE_PRORES_HQ:
        case AV_PROFILE_PRORES_4444:
        case AV_PROFILE_PRORES_XQ:
            return true;
        default:;
        }
        break;
    case AV_CODEC_ID_AV1:
        switch (profile) {
        case AV_PROFILE_AV1_MAIN:
        case AV_PROFILE_AV1_HIGH:
        case AV_PROFILE_AV1_PROFESSIONAL:
            return true;
        default:;
        }
        break;
    case AV_CODEC_ID_VP9:
        switch (profile) {
        case AV_PROFILE_VP9_0:
        case AV_PROFILE_VP9_1:
        case AV_PROFILE_VP9_2:
        case AV_PROFILE_VP9_3:
            return true;
        default:;
        }
        break;
    case AV_CODEC_ID_MPEG4:
        switch (profile) {
        case AV_PROFILE_MPEG4_SIMPLE:
        case AV_PROFILE_MPEG4_CORE:
        case AV_PROFILE_MPEG4_MAIN:
            return true;
        default:;
        }
        break;
    case AV_CODEC_ID_VC1:
        switch (profile) {
        case AV_PROFILE_VC1_SIMPLE:
        case AV_PROFILE_VC1_MAIN:
        case AV_PROFILE_VC1_COMPLEX:
        case AV_PROFILE_VC1_ADVANCED:
            return true;
        default:;
        }
        break;
    default:;
    }
    return false;
}

static AVFrame *allocPicture(enum AVPixelFormat pix_fmt,
                             const int width, const int height) {
    AVFrame * const picture = av_frame_alloc();
    if(!picture) RuntimeThrow("Could not allocate frame");

    picture->format = pix_fmt;
    picture->width  = width;
    picture->height = height;

    /* allocate the buffers for the frame data */
    const int ret = av_frame_get_buffer(picture, 32);
    if(ret < 0) AV_RuntimeThrow(ret, "Could not allocate frame data")

    return picture;
}

static void openVideo(const AVCodec * const codec, OutputStream * const ost) {
    AVCodecContext * const c = ost->fCodec;
    ost->fNextPts = 0;
    /* open the codec */
    int ret = avcodec_open2(c, codec, nullptr);
    if(ret < 0) AV_RuntimeThrow(ret, "Could not open codec")

    // allocate and init a re-usable frame
    if(c->time_base.num <= 0 || c->time_base.den <= 0 ||
       c->time_base.num != ost->fStream->time_base.num ||
       c->time_base.den != ost->fStream->time_base.den) {
        c->time_base = ost->fStream->time_base;
    }

    // Some formats want stream headers to be separate.
    const AVRational fps = av_inv_q(ost->fStream->time_base);
    if(fps.num > 0 && fps.den > 0) {
        ost->fStream->avg_frame_rate = fps;
        ost->fStream->r_frame_rate = fps;
        c->framerate = fps;
    }

    /* Allocate the encoded raw picture. */
    ost->fDstFrame = allocPicture(c->pix_fmt, c->width, c->height);
    if(!ost->fDstFrame) RuntimeThrow("Could not allocate picture");

    /* copy the stream parameters to the muxer */
    ret = avcodec_parameters_from_context(ost->fStream->codecpar, c);
    if(ret < 0) AV_RuntimeThrow(ret, "Could not copy the stream parameters")
}

static void addVideoStream(OutputStream * const ost,
                           AVFormatContext * const oc,
                           const OutputSettings &outSettings,
                           const RenderSettings &renSettings) {
    const AVCodec * const codec = outSettings.fVideoCodec;

//    if(!codec) {
//        /* find the video encoder */
//        codec = avcodec_find_encoder(codec_id);
//        if(!codec) {
//            fprintf(stderr, "codec not found\n");
//            return false;
//        }
//    }

    ost->fStream = avformat_new_stream(oc, nullptr);
    if(!ost->fStream) RuntimeThrow("Could not alloc stream");

    AVCodecContext * const c = avcodec_alloc_context3(codec);
    if(!c) RuntimeThrow("Could not alloc an encoding context");

    ost->fCodec = c;

    /* Put sample parameters. */
    c->bit_rate = outSettings.fVideoBitrate;//settings->getVideoBitrate();
    /* Resolution must be a multiple of two. */
    c->width    = renSettings.fVideoWidth;
    c->height   = renSettings.fVideoHeight;
    /* timebase: This is the fundamental unit of time (in seconds) in terms
     * of which frame timestamps are represented. For fixed-fps content,
     * timebase should be 1/framerate and timestamp increments should be
     * identical to 1. */
    ost->fStream->time_base = renSettings.fTimeBase;

    const AVRational targetFps = av_inv_q(renSettings.fTimeBase);
    if(targetFps.num > 0 && targetFps.den > 0) {
        ost->fStream->avg_frame_rate = targetFps;
        ost->fStream->r_frame_rate = targetFps;
        c->framerate = targetFps;
    }

    c->time_base       = ost->fStream->time_base;

    if (VideoEncoder::isValidProfile(codec,
                                     outSettings.fVideoProfile)) {
        c->profile = outSettings.fVideoProfile;
    }

    for (const auto &opt : outSettings.fVideoOptions.fValues) {
        switch (opt.fType) {
        case FormatType::fTypeCodec:
            av_opt_set(c->priv_data,
                       opt.fKey.toStdString().c_str(),
                       opt.fValue.toStdString().c_str(), 0);
            break;
        case FormatType::fTypeFormat:
            av_opt_set(oc->priv_data,
                       opt.fKey.toStdString().c_str(),
                       opt.fValue.toStdString().c_str(), 0);
            break;
        case FormatType::fTypeMeta:
            av_dict_set(&oc->metadata,
                        opt.fKey.toStdString().c_str(),
                        opt.fValue.toStdString().c_str(), 0);
            break;
        default:;
        }
    }

    c->gop_size      = 12; /* emit one intra frame every twelve frames at most */
    c->pix_fmt       = outSettings.fVideoPixelFormat;//RGBA;
    if(c->codec_id == AV_CODEC_ID_MPEG2VIDEO) {
        /* just for testing, we also add B-frames */
        c->max_b_frames = 2;
    } else if(c->codec_id == AV_CODEC_ID_MPEG1VIDEO) {
        /* Needed to avoid using macroblocks in which some coeffs overflow.
         * This does not happen with normal video, it just happens here as
         * the motion of the chroma plane does not match the luma plane. */
        c->mb_decision = 2;
    }
    /* Some formats want stream headers to be separate. */
    if(oc->oformat->flags & AVFMT_GLOBALHEADER) {
        c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
}

static AVFrame *getVideoFrame(OutputStream * const ost,
                              const sk_sp<SkImage> &image) {
    AVCodecContext *c = ost->fCodec;
    if(!image) RuntimeThrow("No image available for video encoding");

    /* check if we want to generate more frames */
//    if(av_compare_ts(ost->next_pts, c->time_base,
//                      STREAM_DURATION, (AVRational) { 1, 1 }) >= 0)
//        return nullptr;

    auto rasterImage = image;
    SkPixmap pixmap;
    if(!rasterImage->peekPixels(&pixmap)) {
        rasterImage = rasterImage->makeRasterImage();
        if(!rasterImage || !rasterImage->peekPixels(&pixmap)) {
            RuntimeThrow("Could not access rendered image pixels");
        }
    }

    const int srcWidth = pixmap.width();
    const int srcHeight = pixmap.height();
    if(srcWidth <= 0 || srcHeight <= 0) {
        RuntimeThrow("Rendered image has invalid size");
    }

    /* as we only generate a rgba picture, we must convert it
     * to the codec pixel format if needed */
    ost->fSwsCtx = sws_getCachedContext(ost->fSwsCtx,
                                        srcWidth, srcHeight,
                                        AV_PIX_FMT_RGBA,
                                        c->width, c->height,
                                        c->pix_fmt, SWS_BICUBIC,
                                        nullptr, nullptr, nullptr);
    if(!ost->fSwsCtx) RuntimeThrow("Cannot initialize the conversion context");

    // Keep the temporary bitmap alive until after sws_scale() if pixmap
    // is redirected to its pixels.
    SkBitmap unpremulBitmap;

    // Skia gives us premultiplied pixels. Straight-alpha export formats need
    // unpremultiplied input to avoid dark edges around transparency.
    const bool unpremul = pixelFormatHasAlpha(c->pix_fmt);
    if (unpremul) {
        SkImageInfo unpremulInfo = SkImageInfo::Make(pixmap.width(),
                                                     pixmap.height(),
                                                     kRGBA_8888_SkColorType,
                                                     kUnpremul_SkAlphaType,
                                                     pixmap.info().refColorSpace());
        if (unpremulBitmap.tryAllocPixels(unpremulInfo)) {
            const bool converted = rasterImage->readPixels(unpremulInfo,
                                                           unpremulBitmap.getPixels(),
                                                           unpremulBitmap.rowBytes(),
                                                           0, 0);
            if (converted) { unpremulBitmap.peekPixels(&pixmap); }
        }
    }

    const uint8_t * const dstSk[] = {
        static_cast<const uint8_t*>(pixmap.addr())
    };
    int linesizesSk[4] = {
        static_cast<int>(pixmap.rowBytes()),
        0,
        0,
        0
    };
    const int ret = av_frame_make_writable(ost->fDstFrame) ;
    if(ret < 0) AV_RuntimeThrow(ret, "Could not make AVFrame writable")

    sws_scale(ost->fSwsCtx, dstSk,
              linesizesSk, 0, srcHeight, ost->fDstFrame->data,
              ost->fDstFrame->linesize);

    ost->fDstFrame->pts = ost->fNextPts++;

    return ost->fDstFrame;
}

static void writeVideoFrame(AVFormatContext * const oc,
                            OutputStream * const ost,
                            const sk_sp<SkImage> &image,
                            bool * const encodeVideo) {
    AVCodecContext * const c = ost->fCodec;

    AVFrame * frame;
    try {
        frame = getVideoFrame(ost, image);
    } catch(...) {
        RuntimeThrow("Failed to retrieve video frame");
    }


    // encode the image
    const int ret = avcodec_send_frame(c, frame);
    if(ret < 0) {
        qWarning() << "writeVideoFrame failed"
                   << "codec" << (c && c->codec ? c->codec->name : "null")
                   << "pix_fmt" << av_get_pix_fmt_name(c->pix_fmt)
                   << "size" << c->width << "x" << c->height
                   << "frame_pts" << frame->pts
                   << "time_base" << c->time_base.num << "/" << c->time_base.den
                   << "bit_rate" << c->bit_rate;
        AV_RuntimeThrow(ret, "Error submitting a frame for encoding")
    }

    while(ret >= 0) {
        AVPacket pkt = {};

        const int recRet = avcodec_receive_packet(c, &pkt);
        if(recRet >= 0) {
            av_packet_rescale_ts(&pkt, c->time_base, ost->fStream->time_base);
            // if we did not set frame duration earlier, do it now
            if(ost->fFrameDuration <= 0) {
                AVRational frameBase;
                if(ost->fStream->avg_frame_rate.num > 0 && ost->fStream->avg_frame_rate.den > 0)
                    frameBase = av_inv_q(ost->fStream->avg_frame_rate);
                else
                    frameBase = c->time_base;
                ost->fFrameDuration = av_rescale_q(1, frameBase, ost->fStream->time_base);
                if(ost->fFrameDuration <= 0) ost->fFrameDuration = 1;
            }
            pkt.duration = ost->fFrameDuration;
            pkt.stream_index = ost->fStream->index;

            // Write the compressed frame to the media file.
            const int interRet = av_interleaved_write_frame(oc, &pkt);
            if(interRet < 0) AV_RuntimeThrow(interRet, "Error while writing video frame")
        } else if(recRet == AVERROR(EAGAIN) || recRet == AVERROR_EOF) {
            *encodeVideo = recRet != AVERROR_EOF;
            break;
        } else {
            AV_RuntimeThrow(recRet, "Error encoding a video frame")
        }

        av_packet_unref(&pkt);
    }
}

static void addAudioStream(OutputStream * const ost,
                           AVFormatContext * const oc,
                           const OutputSettings &settings,
                           const eSoundSettingsData& inSound) {
    const AVCodec * const codec = settings.fAudioCodec;

//    /* find the audio encoder */
//    codec = avcodec_find_encoder(codec_id);
//    if(!codec) {
//        fprintf(stderr, "codec not found\n");
//        return false;
//    }

    ost->fStream = avformat_new_stream(oc, nullptr);
    if(!ost->fStream) RuntimeThrow("Could not alloc stream");

    AVCodecContext * const c = avcodec_alloc_context3(codec);
    if(!c) RuntimeThrow("Could not alloc an encoding context");
    ost->fCodec = c;

    /* put sample parameters */
    c->sample_fmt     = settings.fAudioSampleFormat;
    c->sample_rate    = settings.fAudioSampleRate;
    if (!Friction::FFmpegCompat::setCodecContextChannelLayoutMask(c,
                                                                  settings.fAudioChannelsLayout)) {
        RuntimeThrow("Could not set audio channel layout");
    }
    c->bit_rate       = settings.fAudioBitrate;
    c->time_base      = { 1, c->sample_rate };

    ost->fStream->time_base = { 1, c->sample_rate };

    // some formats want stream headers to be separate
    if(oc->oformat->flags & AVFMT_GLOBALHEADER)
        c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    /* initialize sample format conversion;
     * to simplify the code, we always pass the data through lavr, even
     * if the encoder supports the generated format directly -- the price is
     * some extra data copying;
     */

    if(ost->fSwrCtx) swr_free(&ost->fSwrCtx);
    ost->fSwrCtx = swr_alloc();
    if(!ost->fSwrCtx) RuntimeThrow("Error allocating the resampling context");
    av_opt_set_int(ost->fSwrCtx, "in_channel_count",  inSound.channelCount(), 0);
    av_opt_set_int(ost->fSwrCtx, "out_channel_count",
                   Friction::FFmpegCompat::codecContextChannelCount(c), 0);
    av_opt_set_int(ost->fSwrCtx, "in_channel_layout",  inSound.fChannelLayout, 0);
    av_opt_set_int(ost->fSwrCtx, "out_channel_layout",
                   Friction::FFmpegCompat::codecContextChannelLayoutMask(c), 0);
    av_opt_set_int(ost->fSwrCtx, "in_sample_rate", inSound.fSampleRate, 0);
    av_opt_set_int(ost->fSwrCtx, "out_sample_rate", c->sample_rate, 0);
    av_opt_set_sample_fmt(ost->fSwrCtx, "in_sample_fmt", inSound.fSampleFormat, 0);
    av_opt_set_sample_fmt(ost->fSwrCtx, "out_sample_fmt", c->sample_fmt,  0);
    swr_init(ost->fSwrCtx);
    if(!swr_is_initialized(ost->fSwrCtx)) {
        RuntimeThrow("Resampler has not been properly initialized");
    }

#ifdef QT_DEBUG
    qDebug() << "name" << "src" << "output";
    qDebug() << "channels" << inSound.channelCount() <<
                              Friction::FFmpegCompat::codecContextChannelCount(c);
    qDebug() << "channel layout" << inSound.fChannelLayout <<
                                    Friction::FFmpegCompat::codecContextChannelLayoutMask(c);
    qDebug() << "sample rate" << inSound.fSampleRate <<
                                 c->sample_rate;
    qDebug() << "sample format" << av_get_sample_fmt_name(inSound.fSampleFormat) <<
                                   av_get_sample_fmt_name(c->sample_fmt);
    qDebug() << "bitrate" << settings.fAudioBitrate;
#endif
}

static AVFrame *allocAudioFrame(enum AVSampleFormat sample_fmt,
                                const uint64_t& channel_layout,
                                const int sample_rate,
                                const int nb_samples) {
    AVFrame *frame = av_frame_alloc();

    if(!frame) RuntimeThrow("Error allocating an audio frame");

    frame->format = sample_fmt;
    if (!Friction::FFmpegCompat::setFrameChannelLayoutMask(frame, channel_layout)) {
        av_frame_free(&frame);
        RuntimeThrow("Could not set frame audio channel layout");
    }
    frame->sample_rate = sample_rate;
    frame->nb_samples = nb_samples;

    if(nb_samples) {
        const int ret = av_frame_get_buffer(frame, 0);
        if(ret < 0) AV_RuntimeThrow(ret, "Error allocating an audio buffer")
    }

    return frame;
}

static void openAudio(const AVCodec * const codec, OutputStream * const ost,
                      const eSoundSettingsData& inSound) {
    AVCodecContext * const c = ost->fCodec;

    /* open it */

    const int ret = avcodec_open2(c, codec, nullptr);
    if(ret < 0) {
        qWarning() << "openAudio failed"
                   << "codec" << (codec ? codec->name : "null")
                   << "sample_fmt" << av_get_sample_fmt_name(c->sample_fmt)
                   << "sample_rate" << c->sample_rate
                   << "channel_layout" << Qt::hex
                   << Friction::FFmpegCompat::codecContextChannelLayoutMask(c) << Qt::dec
                   << "channels" << Friction::FFmpegCompat::codecContextChannelCount(c)
                   << "bit_rate" << c->bit_rate
                   << "input_sample_fmt" << av_get_sample_fmt_name(inSound.fSampleFormat)
                   << "input_sample_rate" << inSound.fSampleRate
                   << "input_channel_layout" << Qt::hex << inSound.fChannelLayout << Qt::dec
                   << "input_channels" << inSound.channelCount();
        AV_RuntimeThrow(ret, "Could not open codec")
    }

    ost->fNextPts = 0;

    const bool varFS = c->codec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE;
    const int nb_samples = varFS ? 10000 : c->frame_size;

    ost->fDstFrame = allocAudioFrame(c->sample_fmt,
                                     Friction::FFmpegCompat::codecContextChannelLayoutMask(c),
                                     c->sample_rate, nb_samples);
    if(!ost->fDstFrame) RuntimeThrow("Could not alloc audio frame");

    ost->fSrcFrame = allocAudioFrame(inSound.fSampleFormat,
                                     inSound.fChannelLayout,
                                     inSound.fSampleRate,
                                     nb_samples);
    if(!ost->fSrcFrame) RuntimeThrow("Could not alloc temporary audio frame");

    /* copy the stream parameters to the muxer */
    const int parRet = avcodec_parameters_from_context(ost->fStream->codecpar, c);
    if(parRet < 0) AV_RuntimeThrow(parRet, "Could not copy the stream parameters")
}

/* if a frame is provided, send it to the encoder, otherwise flush the encoder;
 * return 1 when encoding is finished, 0 otherwise
 */
static void encodeAudioFrame(AVFormatContext * const oc,
                             OutputStream * const ost,
                             AVFrame * const frame,
                             bool * const encodeAudio) {
    const int ret = avcodec_send_frame(ost->fCodec, frame);
    if(ret < 0) AV_RuntimeThrow(ret, "Error submitting a frame for encoding")

    while(true) {
        AVPacket pkt = {};

        const int recRet = avcodec_receive_packet(ost->fCodec, &pkt);
        if(recRet >= 0) {
            av_packet_rescale_ts(&pkt, ost->fCodec->time_base, ost->fStream->time_base);
            pkt.stream_index = ost->fStream->index;

            /* Write the compressed frame to the media file. */
            const int interRet = av_interleaved_write_frame(oc, &pkt);
            if(interRet < 0) AV_RuntimeThrow(interRet, "Error while writing audio frame")
        } else if(recRet == AVERROR(EAGAIN) || recRet == AVERROR_EOF) {
            av_packet_unref(&pkt);
            *encodeAudio = recRet == AVERROR(EAGAIN);
            break;
        } else {
            av_packet_unref(&pkt);
            AV_RuntimeThrow(recRet, "Error encoding an audio frame")
        }
        av_packet_unref(&pkt);
    }
}

static void processAudioStream(AVFormatContext * const oc,
                               OutputStream * const ost,
                               SoundIterator &iterator,
                               bool * const audioEnabled) {
    iterator.fillFrame(ost->fSrcFrame);
    bool gotOutput = ost->fSrcFrame;

//    const int nb_samples =
//            swr_convert(ost->fSwrCtx,
//                        ost->fDstFrame->data, ost->fDstFrame->nb_samples,
//                        const_cast<const uint8_t**>(ost->fSrcFrame->data),
//                        ost->fSrcFrame->nb_samples);
//    ost->fDstFrame->nb_samples = nb_samples;
//    ost->fDstFrame->pts = ost->fNextPts;

//    ost->fNextPts += ost->fDstFrame->nb_samples;

    ost->fSrcFrame->pts = ost->fNextPts;
    ost->fNextPts += ost->fSrcFrame->nb_samples;

    try {
        encodeAudioFrame(oc, ost, ost->fSrcFrame, &gotOutput);
    } catch(...) {
        RuntimeThrow("Error while encoding audio frame");
    }

    *audioEnabled = gotOutput;
}

#include "Sound/soundcomposition.h"
void VideoEncoder::startEncodingNow() {
    Q_ASSERT(mRenderInstanceSettings);
    if(!mOutputFormat) {
        mOutputFormat = av_guess_format(nullptr, mPathByteArray.data(), nullptr);
        if(!mOutputFormat) {
            RuntimeThrow("No AVOutputFormat provided. "
                         "Could not guess AVOutputFormat from file extension");
        }
    }
    const auto scene = mRenderInstanceSettings->getTargetCanvas();
    mFormatContext = avformat_alloc_context();
    if(!mFormatContext) RuntimeThrow("Error allocating AVFormatContext");

    mFormatContext->oformat = const_cast<AVOutputFormat*>(mOutputFormat);
    mFormatContext->url = av_strdup(mPathByteArray.constData());

    _mCurrentContainerFrame = 0;
    // add streams
    mAllAudioProvided = false;
    mEncodeVideo = false;
    mEncodeAudio = false;
    if(mOutputSettings.fVideoCodec && mOutputSettings.fVideoEnabled) {
        try {
            addVideoStream(&mVideoStream, mFormatContext,
                           mOutputSettings, mRenderSettings);
        } catch (...) {
            RuntimeThrow("Error adding video stream");
        }
        mEncodeVideo = true;
    }
    const auto soundComp = scene->getSoundComposition();
    if(mOutputFormat->audio_codec != AV_CODEC_ID_NONE &&
       mOutputSettings.fAudioEnabled && soundComp->hasAnySounds()) {
        sanitizeAudioSettings(mOutputSettings);
        eSoundSettings::sSave();
        eSoundSettings::sSetSampleRate(mOutputSettings.fAudioSampleRate);
        eSoundSettings::sSetSampleFormat(mOutputSettings.fAudioSampleFormat);
        eSoundSettings::sSetChannelLayout(mOutputSettings.fAudioChannelsLayout);
        mInSoundSettings = eSoundSettings::sData();
        try {
            addAudioStream(&mAudioStream, mFormatContext, mOutputSettings,
                           mInSoundSettings);
        } catch (...) {
            RuntimeThrow("Error adding audio stream");
        }
        mEncodeAudio = true;
    }
    if(!mEncodeAudio && !mEncodeVideo) RuntimeThrow("No streams to render");
    // open streams
    if(mEncodeVideo) {
        try {
            openVideo(mOutputSettings.fVideoCodec, &mVideoStream);
        } catch (...) {
            RuntimeThrow("Error opening video stream");
        }
    }
    if(mEncodeAudio) {
        try {
            openAudio(mOutputSettings.fAudioCodec, &mAudioStream,
                      mInSoundSettings);
        } catch (...) {
            RuntimeThrow("Error opening audio stream");
        }
    }

    //av_dump_format(mFormatContext, 0, mPathByteArray.data(), 1);
    if(!(mOutputFormat->flags & AVFMT_NOFILE)) {
        const int avioRet = avio_open(&mFormatContext->pb,
                                      mPathByteArray.data(),
                                      AVIO_FLAG_WRITE);
        if(avioRet < 0) AV_RuntimeThrow(avioRet,
                                        "Could not open " + mPathByteArray.data())
    }

    const int whRet = avformat_write_header(mFormatContext, nullptr);
    if(whRet < 0) AV_RuntimeThrow(whRet,
                                  "Could not write header to " + mPathByteArray.data())
}

bool VideoEncoder::startEncoding(RenderInstanceSettings * const settings) {
    if(mCurrentlyEncoding) return false;
    mRenderInstanceSettings = settings;
    mRenderInstanceSettings->renderingAboutToStart();
    mOutputSettings = mRenderInstanceSettings->getOutputRenderSettings();
    mRenderSettings = mRenderInstanceSettings->getRenderSettings();
    mPathByteArray = mRenderInstanceSettings->getOutputDestination().toUtf8();

    mOutputFormat = mOutputSettings.fOutputFormat;
    mSoundIterator = SoundIterator();
    try {
        startEncodingNow();
        mCurrentlyEncoding = true;
        mEncodingFinished = false;
        mRenderInstanceSettings->setCurrentState(RenderState::rendering);
        mEmitter.encodingStarted();
        return true;
    } catch(const std::exception& e) {
        gPrintExceptionCritical(e);
        mRenderInstanceSettings->setCurrentState(RenderState::error, e.what());
        mEmitter.encodingStartFailed();
        return false;
    }
}

void VideoEncoder::interrupEncoding() {
    if(!mCurrentlyEncoding) return;
    mRenderInstanceSettings->setCurrentState(RenderState::none, "Interrupted");
    finishEncodingNow();
    mEmitter.encodingInterrupted();
}

void VideoEncoder::finishEncodingSuccess() {
    mRenderInstanceSettings->setCurrentState(RenderState::finished);
    mEncodingSuccesfull = true;
    finishEncodingNow();
    mEmitter.encodingFinished();
}

static void flushStream(OutputStream * const ost,
                        AVFormatContext * const formatCtx) {
    if(!ost) return;
    if(!ost->fCodec) return;
    if(exportDebugEnabled()) {
        qWarning() << "flushStream begin"
                   << "codec" << (ost->fCodec->codec ? ost->fCodec->codec->name : "null")
                   << "next_pts" << ost->fNextPts;
    }
    int ret = avcodec_send_frame(ost->fCodec, nullptr);
    if(ret == AVERROR_EOF) {
        if(exportDebugEnabled()) {
            qWarning() << "flushStream already drained"
                       << "codec" << (ost->fCodec->codec ? ost->fCodec->codec->name : "null");
        }
        return;
    }
    if(ret < 0) {
        if(exportDebugEnabled()) {
            qWarning() << "flushStream send null frame failed"
                       << "codec" << (ost->fCodec->codec ? ost->fCodec->codec->name : "null")
                       << "ret" << ret;
        }
        return;
    }
    while(ret >= 0) {
        AVPacket pkt = {};
        ret = avcodec_receive_packet(ost->fCodec, &pkt);
        if(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            av_packet_unref(&pkt);
            if(exportDebugEnabled()) {
                qWarning() << "flushStream end"
                           << "codec" << (ost->fCodec->codec ? ost->fCodec->codec->name : "null")
                           << "ret" << ret;
            }
            break;
        }
        if(ret < 0) {
            av_packet_unref(&pkt);
            AV_RuntimeThrow(ret, "Error encoding a video frame during flush")
        }

        if(pkt.pts != AV_NOPTS_VALUE)
            pkt.pts = av_rescale_q(pkt.pts, ost->fCodec->time_base,
                                   ost->fStream->time_base);
        if(pkt.dts != AV_NOPTS_VALUE)
            pkt.dts = av_rescale_q(pkt.dts, ost->fCodec->time_base,
                                   ost->fStream->time_base);
        if(ost->fFrameDuration <= 0) {
            // if we did not set frame duration earlier, do it now
            AVRational frameBase;
            if(ost->fStream->avg_frame_rate.num > 0 && ost->fStream->avg_frame_rate.den > 0)
                frameBase = av_inv_q(ost->fStream->avg_frame_rate);
            else
                frameBase = ost->fCodec->time_base;
            ost->fFrameDuration = av_rescale_q(1, frameBase, ost->fStream->time_base);
            if(ost->fFrameDuration <= 0) ost->fFrameDuration = 1;
        }
        // set duration if not set
        pkt.duration = ost->fFrameDuration;
        pkt.stream_index = ost->fStream->index;
        ret = av_interleaved_write_frame(formatCtx, &pkt);
        av_packet_unref(&pkt);
        if(ret < 0) {
            AV_RuntimeThrow(ret, "Error while writing packet during flush")
        }
    }
}

static void closeStream(OutputStream * const ost) {
    if(!ost) return;
    if(ost->fCodec) {
        avcodec_free_context(&ost->fCodec);
    }
    if(ost->fDstFrame) av_frame_free(&ost->fDstFrame);
    if(ost->fSrcFrame) av_frame_free(&ost->fSrcFrame);
    if(ost->fSwsCtx) sws_freeContext(ost->fSwsCtx);
    if(ost->fSwrCtx) swr_free(&ost->fSwrCtx);
    *ost = OutputStream();
}

void VideoEncoder::finishEncodingNow() {
    if(!mCurrentlyEncoding) return;

    if(exportDebugEnabled()) {
        qWarning() << "finishEncodingNow begin"
                   << "video" << mEncodeVideo
                   << "audio" << mEncodeAudio
                   << "success" << mEncodingSuccesfull;
    }

    if(mEncodeVideo) flushStream(&mVideoStream, mFormatContext);
    if(mEncodeAudio) flushStream(&mAudioStream, mFormatContext);

    // set the number of frames in the video stream
    if(mEncodeVideo && mVideoStream.fStream && mVideoStream.fCodec) {
        mVideoStream.fStream->nb_frames = mVideoStream.fNextPts;
        const AVRational fps = av_inv_q(mRenderSettings.fTimeBase);
        if(fps.num > 0 && fps.den > 0) {
            mVideoStream.fStream->avg_frame_rate = fps;
            mVideoStream.fStream->r_frame_rate = fps;
        }
    }

    if(mEncodingSuccesfull) {
        if(exportDebugEnabled()) {
            qWarning() << "av_write_trailer begin";
        }
        const int trailerRet = av_write_trailer(mFormatContext);
        if(exportDebugEnabled()) {
            qWarning() << "av_write_trailer end" << trailerRet;
        }
    }

    /* Close each codec. */
    if(mEncodeVideo) closeStream(&mVideoStream);
    if(mEncodeAudio) closeStream(&mAudioStream);

    if(mOutputFormat) {
        if(!(mOutputFormat->flags & AVFMT_NOFILE)) {
            avio_close(mFormatContext->pb);
        }
    } else if(mFormatContext) {
        avio_close(mFormatContext->pb);
    }
    if(mFormatContext) {
        avformat_free_context(mFormatContext);
        mFormatContext = nullptr;
    }

    mEncodeAudio = false;
    mEncodeVideo = false;
    mCurrentlyEncoding = false;
    mEncodingSuccesfull = false;
    mNextContainers.clear();
    mNextSoundConts.clear();
    clearContainers();

    eSoundSettings::sRestore();
    if(exportDebugEnabled()) {
        qWarning() << "finishEncodingNow end";
    }
}

void VideoEncoder::clearContainers() {
    _mContainers.clear();
    mSoundIterator.clear();
}

void VideoEncoder::process() {
    bool hasVideo = !_mContainers.isEmpty(); // local encode
    bool hasAudio;
    if(mEncodeAudio) {
        if(_mAllAudioProvided) {
            hasAudio = mSoundIterator.hasValue();
        } else {
            hasAudio = mSoundIterator.hasSamples(mAudioStream.fSrcFrame->nb_samples);
        }
    } else hasAudio = false;
    if(exportDebugEnabled()) {
        qWarning() << "[export-debug] encoder-process-begin"
                   << "containers" << _mContainers.count()
                   << "currentContainerId" << _mCurrentContainerId
                   << "hasVideo" << hasVideo
                   << "hasAudio" << hasAudio
                   << "allAudioProvided" << _mAllAudioProvided
                   << "pendingVideo" << mNextContainers.count()
                   << "pendingAudio" << mNextSoundConts.count();
    }
    while((mEncodeVideo && hasVideo) || (mEncodeAudio && hasAudio)) {
        bool videoAligned = true;
        if(mEncodeVideo && mEncodeAudio) {
            videoAligned = av_compare_ts(mVideoStream.fNextPts,
                                         mVideoStream.fCodec->time_base,
                                         mAudioStream.fNextPts,
                                         mAudioStream.fCodec->time_base) <= 0;
        }
        const bool encodeVideo = mEncodeVideo && hasVideo && videoAligned;
        if(encodeVideo) {
            const auto &cacheCont = _mContainers.at(_mCurrentContainerId);
            const auto contRange = cacheCont.fRange*_mRenderRange;
            const int nFrames = contRange.span();
            try {
                writeVideoFrame(mFormatContext, &mVideoStream,
                                cacheCont.fImage, &hasVideo);
            } catch(...) {
                RuntimeThrow("Failed to write video frame");
            }
            if(++_mCurrentContainerFrame >= nFrames) {
                _mCurrentContainerId++;
                _mCurrentContainerFrame = 0;
                hasVideo = _mCurrentContainerId < _mContainers.count();
            }
        }
        bool audioAligned = true;
        if(mEncodeVideo && mEncodeAudio) {
            audioAligned = av_compare_ts(mVideoStream.fNextPts,
                                         mVideoStream.fCodec->time_base,
                                         mAudioStream.fNextPts,
                                         mAudioStream.fCodec->time_base) >= 0;
        }
        const bool encodeAudio = mEncodeAudio && hasAudio && audioAligned;
        if(encodeAudio) {
            try {
                processAudioStream(mFormatContext, &mAudioStream,
                                   mSoundIterator, &hasAudio);
            } catch(...) {
                RuntimeThrow("Failed to process audio stream");
            }
            hasAudio = _mAllAudioProvided ? mSoundIterator.hasValue() :
                                            mSoundIterator.hasSamples(mAudioStream.fSrcFrame->nb_samples);
        }
        if(!encodeVideo && !encodeAudio) {
            if(exportDebugEnabled()) {
                qWarning() << "[export-debug] encoder-process-stall"
                           << "currentContainerId" << _mCurrentContainerId
                           << "containerCount" << _mContainers.count()
                           << "hasVideo" << hasVideo
                           << "hasAudio" << hasAudio
                           << "videoAligned" << videoAligned
                           << "audioAligned" << audioAligned
                           << "nextVideoPts" << (mEncodeVideo ? mVideoStream.fNextPts : -1)
                           << "nextAudioPts" << (mEncodeAudio ? mAudioStream.fNextPts : -1)
                           << "allAudioProvided" << _mAllAudioProvided
                           << "pendingVideo" << mNextContainers.count()
                           << "pendingAudio" << mNextSoundConts.count();
            }
            break;
        }
    }
}


void VideoEncoder::beforeProcessing(const Hardware) {
    _mCurrentContainerId = 0;
    _mAllAudioProvided = mAllAudioProvided;
    if(_mContainers.isEmpty()) {
        _mContainers.swap(mNextContainers);
    } else {
        for(const auto& cont : mNextContainers)
            _mContainers.append(cont);
        mNextContainers.clear();
    }
    for(const auto& sound : mNextSoundConts)
        mSoundIterator.add(sound);
    mNextSoundConts.clear();
    _mRenderRange = {mRenderSettings.fMinFrame, mRenderSettings.fMaxFrame};
    if(!mCurrentlyEncoding) clearContainers();
}

void VideoEncoder::afterProcessing() {
    const auto currCanvas = mRenderInstanceSettings->getTargetCanvas();
    if(_mCurrentContainerId != 0) {
        const auto &lastEncoded = _mContainers.at(_mCurrentContainerId - 1);
        if(currCanvas) {
            currCanvas->setMinFrameUseRange(lastEncoded.fRange.fMax + 1);
        }
    }

    for(int i = _mContainers.count() - 1; i >= _mCurrentContainerId; i--) {
        const auto &cont = _mContainers.at(i);
        mNextContainers.prepend(cont);
    }
    _mContainers.clear();
    const bool pendingVideo = !mNextContainers.isEmpty();
    const bool pendingAudio = !mNextSoundConts.isEmpty();
    const bool audioProvisionUpdated = mAllAudioProvided != _mAllAudioProvided;
    if(exportDebugEnabled()) {
        qWarning() << "[export-debug] encoder-after-processing"
                   << "pendingVideo" << pendingVideo
                   << "pendingAudio" << pendingAudio
                   << "audioProvisionUpdated" << audioProvisionUpdated
                   << "encodingFinished" << mEncodingFinished
                   << "interrupt" << mInterruptEncoding
                   << "currentContainerId" << _mCurrentContainerId;
    }

    if(mInterruptEncoding) {
        interrupEncoding();
        mInterruptEncoding = false;
    } else if(unhandledException()) {
        gPrintExceptionCritical(takeException());
        mRenderInstanceSettings->setCurrentState(RenderState::error, "Error");
        finishEncodingNow();
        mEmitter.encodingFailed();
    } else if(mEncodingFinished) finishEncodingSuccess();
    else if(pendingVideo || pendingAudio || audioProvisionUpdated) {
        if(exportDebugEnabled()) {
            qWarning() << "[export-debug] encoder-requeue"
                       << "reasonVideo" << pendingVideo
                       << "reasonAudio" << pendingAudio
                       << "reasonAudioState" << audioProvisionUpdated;
        }
        queTask();
    }
}

void VideoEncoder::sFinishEncoding() {
    sInstance->finishCurrentEncoding();
}

bool VideoEncoder::sEncodingSuccessfulyStarted() {
    return sInstance->getCurrentlyEncoding();
}

bool VideoEncoder::sEncodeAudio() {
    return sInstance->mEncodeAudio;
}

void VideoEncoder::sInterruptEncoding() {
    sInstance->interruptCurrentEncoding();
}

bool VideoEncoder::sStartEncoding(RenderInstanceSettings *settings) {
    return sInstance->startNewEncoding(settings);
}

void VideoEncoder::sAddCacheContainerToEncoder(const QueuedVideoFrame &cont) {
    sInstance->addContainer(cont);
}

void VideoEncoder::sAddCacheContainerToEncoder(
        const stdsptr<Samples> &cont) {
    sInstance->addContainer(cont);
}

void VideoEncoder::sAllAudioProvided() {
    sInstance->allAudioProvided();
}
