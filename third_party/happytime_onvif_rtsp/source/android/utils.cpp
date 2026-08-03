/***************************************************************************************
 *
 *  IMPORTANT: READ BEFORE DOWNLOADING, COPYING, INSTALLING OR USING.
 *
 *  By downloading, copying, installing or using the software you agree to this license.
 *  If you do not agree to this license, do not download, install, 
 *  copy or use the software.
 *
 *  Copyright (C) 2014-2025, Happytimesoft Corporation, all rights reserved.
 *
 *  Redistribution and use in binary forms, with or without modification, are permitted.
 *
 *  Unless required by applicable law or agreed to in writing, software distributed 
 *  under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 *  CONDITIONS OF ANY KIND, either express or implied. See the License for the specific
 *  language governing permissions and limitations under the License.
 *
****************************************************************************************/

#include <QSettings>
#include "utils.h"
#include "media_format.h"


int getRtspPort()
{
    QSettings setting;

    return setting.value("RtspPort", 8554).toInt();
}

int getOnvifPort()
{
    QSettings setting;

    return setting.value("OnvifPort", 8000).toInt();
}

int getVideoFlag()
{
    QSettings setting;

    return setting.value("VideoFlag", 1).toInt();
}

int getVideoCodec()
{
    QSettings setting;

    return setting.value("VideoCodec", VIDEO_CODEC_H264).toInt();
}

int getCameraIndex()
{
    QSettings setting;

    return setting.value("CameraIndex", 0).toInt();
}

QSize getResolution()
{
    QSettings setting;

    return setting.value("Resolution", QSize(0, 0)).toSize();
}

int getFrameRate()
{
    QSettings setting;

    return setting.value("FrameRate", 25).toInt();
}

int getVideoBitrate()
{
    QSettings setting;

    return setting.value("VideoBitrate", 0).toInt();
}

int getAudioFlag()
{
    QSettings setting;

    return setting.value("AudioFlag", 1).toInt();
}

int getAudioCodec()
{
    QSettings setting;

    return setting.value("AudioCodec", AUDIO_CODEC_G711A).toInt();
}

int getSampleRate()
{
    QSettings setting;

    return setting.value("SampleRate", 44100).toInt();
}

int getChannel()
{
    QSettings setting;

    return setting.value("Channel", 2).toInt();
}

int getAudioBitrate()
{
    QSettings setting;

    return setting.value("AudioBitrate", 0).toInt();
}

int getLogEnable()
{
    QSettings setting;

    return setting.value("LogEnable", 1).toInt();
}

int getLogLevel()
{
    QSettings setting;

    return setting.value("LogLevel", 1).toInt();
}

void saveRtspPort(int value)
{
    QSettings setting;

    setting.setValue("RtspPort", value);
}

void saveOnvifPort(int value)
{
    QSettings setting;

    setting.setValue("OnvifPort", value);
}

void saveVideoFlag(int value)
{
    QSettings setting;

    setting.setValue("VideoFlag", value);
}

void saveVideoCodec(int value)
{
    QSettings setting;

    setting.setValue("VideoCodec", value);
}

void saveCameraIndex(int value)
{
    QSettings setting;

    setting.setValue("CameraIndex", value);
}

void saveResolution(QSize value)
{
    QSettings setting;

    setting.setValue("Resolution", value);
}

void saveFrameRate(int value)
{
    QSettings setting;

    setting.setValue("FrameRate", value);
}

void saveVideoBitrate(int value)
{
    QSettings setting;

    setting.setValue("VideoBitrate", value);
}

void saveAudioFlag(int value)
{
    QSettings setting;

    setting.setValue("AudioFlag", value);
}

void saveAudioCodec(int value)
{
    QSettings setting;

    setting.setValue("AudioCodec", value);
}

void saveSampleRate(int value)
{
    QSettings setting;

    setting.setValue("SampleRate", value);
}

void saveChannel(int value)
{
    QSettings setting;

    setting.setValue("Channel", value);
}

void saveAudioBitrate(int value)
{
    QSettings setting;

    setting.setValue("AudioBitrate", value);
}

void saveLogEnable(int value)
{
    QSettings setting;

    setting.setValue("LogEnable", value);
}

void saveLogLevel(int value)
{
    QSettings setting;

    setting.setValue("LogLevel", value);
}




