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

#include "sys_inc.h"
#include "audio_play_qt.h"
#include <QMutexLocker>
#include <QMediaDevices>


CQAudioPlay::CQAudioPlay(QObject * parent) : QObject(parent), CAudioPlay()
, m_pSink(NULL)
, m_pBuffer(NULL)
{
    
}

CQAudioPlay::~CQAudioPlay()
{
    stopPlay();
}

BOOL CQAudioPlay::startPlay(int samplerate, int channels)
{
    QAudioFormat format;
    
    format.setSampleRate(samplerate);
    format.setChannelCount(channels);
    format.setSampleFormat(QAudioFormat::Int16);

    QAudioDevice device(QMediaDevices::defaultAudioOutput());
    if (!device.isFormatSupported(format)) 
    {
        log_print(HT_LOG_ERR, "Raw audio format not supported by backend, cannot play audio\r\n");
        return FALSE;
    }
    
    m_nSamplerate = samplerate;
    m_nChannels = channels;

    m_pSink = new QAudioSink(device, format, NULL);
    
    m_pSink->setBufferSize(8192);
    m_pBuffer = m_pSink->start();
    if (NULL == m_pBuffer)
    {
        log_print(HT_LOG_ERR, "%s, audio output start failed\r\n", __FUNCTION__);
        return FALSE;
    }

    m_bInited = TRUE;
    
    return TRUE;
}

void CQAudioPlay::stopPlay()
{
    QMutexLocker locker(&m_mutex);
    
    if (m_pSink)
    {
        m_pSink->stop();
        delete m_pSink;

        m_pSink = NULL;
    }

    m_pBuffer = NULL;

    m_bInited = FALSE;
}

void CQAudioPlay::playAudio(uint8 * data, int size) 
{
    QMutexLocker locker(&m_mutex);

    if (!m_bInited || NULL == m_pBuffer)
    {
        return;   
    }
    
    int rlen = m_pBuffer->write((char *)data, size);
    while (rlen < size)
    {
        if (rlen < 0)
        {
            break; // error
        }
        else
        {
            size -= rlen;
            data += rlen;
        }

        rlen = m_pBuffer->write((char *)data, size);
    }
}

BOOL CQAudioPlay::setVolume(int volume)
{
    QMutexLocker locker(&m_mutex);
    
    if (m_pSink)
    {
        double db = (double) volume / (HTVOLUME_MAX - HTVOLUME_MIN);
        if (db < 0)
        {
            db = 0.0;
        }
        else if (db > 1.0)
        {
            db = 1.0;
        }

        log_print(HT_LOG_DBG, "%s, volume=%d, db=%f\r\n", __FUNCTION__, volume, db);

        m_pSink->setVolume(db);

        return TRUE;
    }

    return FALSE;
}

int CQAudioPlay::getVolume()
{
    double volume = HTVOLUME_MIN;

    QMutexLocker locker(&m_mutex);
    
    if (m_pSink)
    {
        volume = m_pSink->volume();
    } 

    int nv = (HTVOLUME_MAX - HTVOLUME_MIN) * volume + HTVOLUME_MIN;

    return nv; 
}



