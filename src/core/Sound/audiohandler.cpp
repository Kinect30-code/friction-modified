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

#include "audiohandler.h"
#include "soundcomposition.h"
#include "appsupport.h"

#include <iostream>
#include <QProcess>

AudioHandler* AudioHandler::sInstance = nullptr;

AudioHandler::AudioHandler()
{
    Q_ASSERT(!sInstance);
    sInstance = this;
}

const int BufferSize = 32768;

QAudioFormat::SampleType toQtAudioFormat(const AVSampleFormat avFormat)
{
    if (avFormat == AV_SAMPLE_FMT_S32) {
        return QAudioFormat::SignedInt;
    } else if (avFormat == AV_SAMPLE_FMT_FLT) {
        return QAudioFormat::Float;
    } else if (avFormat == AV_SAMPLE_FMT_U8) {
        return QAudioFormat::UnSignedInt;
    }
    qWarning() << "Unknown AV sample format" << avFormat
               << "falling back to SignedInt";
    return QAudioFormat::SignedInt;
}

AVSampleFormat toAVAudioFormat(const QAudioFormat::SampleType qFormat)
{
    if (qFormat == QAudioFormat::SignedInt) {
        return AV_SAMPLE_FMT_S32;
    } else if (qFormat == QAudioFormat::Float) {
        return AV_SAMPLE_FMT_FLT;
    } else if (qFormat == QAudioFormat::UnSignedInt) {
        return AV_SAMPLE_FMT_U8;
    }
    qWarning() << "Unknown audio sample type" << qFormat
               << "falling back to S32";
    return AV_SAMPLE_FMT_S32;
}

void AudioHandler::initializeAudio(eSoundSettingsData& soundSettings,
                                   const QString &deviceName)
{
    if (mAudioOutput) { delete mAudioOutput; }

    mAudioBuffer = QByteArray(BufferSize, 0);

    mAudioDevice = findDevice(deviceName);
    qDebug() << "Using audio device" << mAudioDevice.deviceName();

    mAudioFormat.setSampleRate(qMax(1, soundSettings.fSampleRate));
    mAudioFormat.setChannelCount(qMax(1, soundSettings.channelCount()));
    mAudioFormat.setSampleSize(8*qMax(1, soundSettings.bytesPerSample()));
    mAudioFormat.setCodec("audio/pcm");
    mAudioFormat.setByteOrder(QAudioFormat::LittleEndian);
    mAudioFormat.setSampleType(toQtAudioFormat(soundSettings.fSampleFormat));

    QAudioDeviceInfo info(mAudioDevice);
    if (!info.isFormatSupported(mAudioFormat)) {
        mAudioFormat = info.nearestFormat(mAudioFormat);
    }

    if (mAudioFormat.sampleRate() <= 0) mAudioFormat.setSampleRate(44100);
    if (mAudioFormat.channelCount() <= 0) mAudioFormat.setChannelCount(2);
    if (mAudioFormat.sampleSize() <= 0) mAudioFormat.setSampleSize(32);
    if (mAudioFormat.sampleType() == QAudioFormat::Unknown)
        mAudioFormat.setSampleType(QAudioFormat::SignedInt);

    soundSettings.fSampleRate = mAudioFormat.sampleRate();
    soundSettings.fSampleFormat = toAVAudioFormat(mAudioFormat.sampleType());

    mAudioOutput = new QAudioOutput(mAudioDevice, mAudioFormat, this);
    mAudioOutput->setNotifyInterval(128);
    emit deviceChanged();
}

void AudioHandler::initializeAudio(const QString &deviceName,
                                   bool save)
{
    if (mAudioOutput) { delete mAudioOutput; }

    mAudioBuffer = QByteArray(BufferSize, 0);

    mAudioDevice = findDevice(deviceName);
    qDebug() << "Using audio device" << mAudioDevice.deviceName();
    if (save) {
        AppSupport::setSettings(QString::fromUtf8("audio"),
                                QString::fromUtf8("output"),
                                mAudioDevice.deviceName());
    }

    QAudioDeviceInfo info(mAudioDevice);
    if (!info.isFormatSupported(mAudioFormat)) {
        mAudioFormat = info.nearestFormat(mAudioFormat);
    }

    mAudioOutput = new QAudioOutput(mAudioDevice, mAudioFormat, this);
    mAudioOutput->setNotifyInterval(128);
    emit deviceChanged();
}

void AudioHandler::startAudio() {
    mAudioIOOutput = mAudioOutput->start();
    if (!mAudioIOOutput) {
        qWarning() << "QAudioOutput::start() returned null — audio will not play"
                   << "device:" << mAudioDevice.deviceName()
                   << "state:" << mAudioOutput->state()
                   << "error:" << mAudioOutput->error();
    }
}

void AudioHandler::pauseAudio()
{
    mAudioOutput->suspend();
}

void AudioHandler::resumeAudio()
{
    mAudioOutput->resume();
}

void AudioHandler::stopAudio()
{
    mAudioIOOutput = nullptr;
    mAudioOutput->stop();
    mAudioOutput->reset();
}

void AudioHandler::setVolume(const int value)
{
    if (!mAudioOutput) { return; }
    mAudioOutput->setVolume(qreal(value) / 100);
}

qreal AudioHandler::getVolume()
{
    if (mAudioOutput) { return mAudioOutput->volume(); }
    return 0;
}

const QString AudioHandler::getDeviceName()
{
    return mAudioDevice.deviceName();
}

QAudioDeviceInfo AudioHandler::findDevice(const QString &deviceName)
{
    if (!deviceName.isEmpty() && deviceName != QString::fromUtf8("System Default")) {
        const auto deviceInfos = QAudioDeviceInfo::availableDevices(QAudio::AudioOutput);
        for (const QAudioDeviceInfo &deviceInfo : deviceInfos) {
            if (deviceInfo.deviceName() == deviceName) return deviceInfo;
        }
    }

    const auto deviceInfos = QAudioDeviceInfo::availableDevices(QAudio::AudioOutput);
    for (const QAudioDeviceInfo &di : deviceInfos) {
        if (!di.deviceName().isEmpty()) return di;
    }

    const auto defaultDev = QAudioDeviceInfo::defaultOutputDevice();
    if (!defaultDev.deviceName().isEmpty()) return defaultDev;

    return QAudioDeviceInfo::defaultOutputDevice();
}

const QStringList AudioHandler::listDevices()
{
    QStringList devices;
    const auto deviceInfos = QAudioDeviceInfo::availableDevices(QAudio::AudioOutput);
    for (const QAudioDeviceInfo &deviceInfo : deviceInfos) {
        const auto name = deviceInfo.deviceName();
        if (!name.isEmpty()) devices << name;
    }
    if (devices.isEmpty()) {
        QProcess pactl;
        pactl.start(QString::fromUtf8("pactl"), {QString::fromUtf8("list"), QString::fromUtf8("short"), QString::fromUtf8("sinks")});
        pactl.waitForFinished(2000);
        const auto lines = QString::fromUtf8(pactl.readAllStandardOutput()).split('\n');
        for (const auto& line : lines) {
            const auto parts = line.split('\t');
            if (parts.size() >= 2) {
                const auto name = parts.at(1).trimmed();
                if (!name.isEmpty()) devices << name;
            }
        }
    }
    if (devices.isEmpty()) {
        const auto defaultDevice = QAudioDeviceInfo::defaultOutputDevice();
        const auto defaultName = defaultDevice.deviceName();
        if (!defaultName.isEmpty()) {
            devices << defaultName;
        } else {
            devices << QString::fromUtf8("System Default");
        }
    }
    return devices;
}
