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

#include "audiostreamsdata.h"
#include "Sound/soundcomposition.h"
#include "../ffmpegcompat.h"

AudioStreamsData::AudioStreamsData() {
    connect(eSoundSettings::sInstance, &eSoundSettings::settingsChanged,
            this, [this]() {
        updateSwrContext();
    });
}

AudioStreamsData::~AudioStreamsData() {
    if(fOpened) close();
}

stdsptr<const AudioStreamsData> AudioStreamsData::sOpen(
        const QString &path, AVFormatContext * const formatContext) {
    const auto result = std::shared_ptr<AudioStreamsData>(
                new AudioStreamsData, AudioStreamsData::sDestroy);
    result->open(path, formatContext);
    return result;
}

void AudioStreamsData::updateSwrContext() {
    if(mLocked) {
        mUpdateSwrPlanned = true;
        return;
    }
    mUpdateSwrPlanned = false;

    const auto audCodecPars = fAudioStream->codecpar;
    const auto sampleFormat = static_cast<AVSampleFormat>(audCodecPars->format);
    const int srcChannels = audCodecPars->ch_layout.nb_channels > 0 ?
        audCodecPars->ch_layout.nb_channels :
        Friction::FFmpegCompat::codecParametersChannelCount(audCodecPars);
    int dstChannels = eSoundSettings::sChannelCount();
    if (dstChannels <= 0) dstChannels = srcChannels;
    int dstSampleRate = eSoundSettings::sSampleRate();
    if (dstSampleRate <= 0) dstSampleRate = audCodecPars->sample_rate;

    if(fSwrContext) swr_free(&fSwrContext);

    fSwrContext = swr_alloc();
    if(!fSwrContext) {
        qWarning() << "swr_alloc failed";
        return;
    }

    int ret = 0;
#if FRICTION_HAS_AVCHANNEL_LAYOUT
    av_opt_set_chlayout(fSwrContext, "in_chlayout", &audCodecPars->ch_layout, 0);
    {
        AVChannelLayout outLayout;
        av_channel_layout_default(&outLayout, dstChannels);
        av_opt_set_chlayout(fSwrContext, "out_chlayout", &outLayout, 0);
        av_channel_layout_uninit(&outLayout);
    }
#else
    av_opt_set_int(fSwrContext, "in_channel_layout",
                   Friction::FFmpegCompat::codecParametersChannelLayoutMask(audCodecPars), 0);
    av_opt_set_int(fSwrContext, "out_channel_layout", eSoundSettings::sChannelLayout(), 0);
    av_opt_set_int(fSwrContext, "in_channel_count", srcChannels, 0);
    av_opt_set_int(fSwrContext, "out_channel_count", dstChannels, 0);
#endif
    av_opt_set_int(fSwrContext, "in_sample_rate", audCodecPars->sample_rate, 0);
    av_opt_set_int(fSwrContext, "out_sample_rate", dstSampleRate, 0);
    av_opt_set_sample_fmt(fSwrContext, "in_sample_fmt", sampleFormat, 0);
    av_opt_set_sample_fmt(fSwrContext, "out_sample_fmt", eSoundSettings::sSampleFormat(), 0);
    ret = swr_init(fSwrContext);

    if(ret < 0) {
        char errBuf[256] = {};
        av_strerror(ret, errBuf, sizeof(errBuf));
        qWarning() << "swr_init failed:" << errBuf
                   << "| in=" << srcChannels << "ch" << audCodecPars->sample_rate << "Hz"
                   << " out=" << dstChannels << "ch" << dstSampleRate << "Hz";
        swr_free(&fSwrContext);
        fSwrContext = nullptr;
    } else {
        qDebug() << "swr_init OK | in=" << srcChannels << "ch"
                 << audCodecPars->sample_rate << "Hz"
                 << "fmt=" << av_get_sample_fmt_name(sampleFormat)
                 << "| out=" << dstChannels << "ch"
                 << dstSampleRate << "Hz"
                 << "fmt=" << av_get_sample_fmt_name(eSoundSettings::sSampleFormat());
    }
}

stdsptr<AudioStreamsData> AudioStreamsData::sOpen(const QString &path) {
    const auto result = std::shared_ptr<AudioStreamsData>(
                new AudioStreamsData, AudioStreamsData::sDestroy);
    result->open(path);
    return result;
}

void AudioStreamsData::open(const QString &path,
                            AVFormatContext * const formatContext) {
    try {
        fPath = path;
        open(formatContext);
    } catch(...) {
        fPath.clear();
        RuntimeThrow("Failed to set audio file path to '" + path + "'.");
    }
}

void AudioStreamsData::open(const QString &path) {
    try {
        fPath = path;
        open();
    } catch(...) {
        fPath.clear();
        RuntimeThrow("Failed to set audio file path to '" + path + "'.");
    }
}

void AudioStreamsData::close() {
    fOpened = false;

    if(fDecodedFrame) av_frame_free(&fDecodedFrame);
    if(fPacket) av_packet_free(&fPacket);
    if(fSwrContext) swr_free(&fSwrContext);
    if(fCodecContext) {
        avcodec_free_context(&fCodecContext);
    }
    if(fFormatContext) avformat_close_input(&fFormatContext);

    fAudioStreamIndex = -1;
    fAudioStream = nullptr;
}

void AudioStreamsData::open() {
    if(fOpened) return;
    const auto stdString = fPath.toStdString();
    const char * const path = stdString.c_str();
    try {
        open(path);
    } catch(...) {
        close();
        RuntimeThrow("Failed to setup audio stream for '" + path + "'.");
    }
}

void AudioStreamsData::open(const char * const path) {
    auto formatContext = avformat_alloc_context();
    if(!formatContext) RuntimeThrow("Error allocating AVFormatContext");
    if(avformat_open_input(&formatContext, path, nullptr, nullptr) != 0) {
        RuntimeThrow("Could not open file");
    }
    if(avformat_find_stream_info(formatContext, nullptr) < 0) {
        RuntimeThrow("Could not retrieve stream info");
    }
    open(formatContext);
}

void AudioStreamsData::open(AVFormatContext * const formatContext) {
    fFormatContext = formatContext;
    // Find the index of the first audio stream
    fAudioStreamIndex = -1;
    const AVCodecParameters *audCodecPars = nullptr;
    const AVCodec *audCodec = nullptr;
    fAudioStream = nullptr;
    for(uint i = 0; i < fFormatContext->nb_streams; i++) {
        AVStream * const  iStream = fFormatContext->streams[i];
        const AVCodecParameters * const iCodecPars = iStream->codecpar;
        const AVMediaType &iMediaType = iCodecPars->codec_type;
        if(iMediaType == AVMEDIA_TYPE_AUDIO) {
            fAudioStreamIndex = static_cast<int>(i);
            audCodecPars = iCodecPars;
            audCodec = avcodec_find_decoder(audCodecPars->codec_id);
            fAudioStream = iStream;
            if(fAudioStream->duration == 0)
                RuntimeThrow("Unknown stream duration");
            const qreal mult = qreal(fAudioStream->time_base.num)/
                                     fAudioStream->time_base.den;
            fDurationSec = fAudioStream->duration*mult;
            break;
        }
    }
    if(fAudioStreamIndex == -1)
        RuntimeThrow("Could not retrieve audio stream");
    if(!audCodec) RuntimeThrow("Unsupported codec");

    fCodecContext = avcodec_alloc_context3(audCodec);
    if(!fCodecContext) RuntimeThrow("Error allocating AVCodecContext");
    if(avcodec_parameters_to_context(fCodecContext, audCodecPars) < 0) {
        RuntimeThrow("Failed to copy codec params to codec context");
    }

    if(avcodec_open2(fCodecContext, audCodec, nullptr) < 0) {
        RuntimeThrow("Failed to open codec");
    }

    updateSwrContext();

    fPacket = av_packet_alloc();
    if(!fPacket) RuntimeThrow("Error allocating AVPacket");
    fDecodedFrame = av_frame_alloc();
    if(!fDecodedFrame) RuntimeThrow("Error allocating AVFrame");

    fOpened = true;
}
