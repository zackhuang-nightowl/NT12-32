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
#include "gles_input.h"
#include "gles_engine.h"

#ifdef ANDROID
#include <SLES/OpenSLES_AndroidConfiguration.h>
#endif


#ifdef ANDROID
static void bufferQueueCallback(SLAndroidSimpleBufferQueueItf, void *context)
#else
static void bufferQueueCallback(SLBufferQueueItf, void *context)
#endif
{
    GlesInput *audioInput = reinterpret_cast<GlesInput*>(context);
    
    audioInput->processBuffer();
}

GlesInput::GlesInput()
: m_recorderObject(0)
, m_recorder(0)
, m_bufferQueue(0)
, m_bufferSize(1600)
, m_currentBuffer(0)
, m_pCallback(NULL)
, m_pUserdata(NULL)
{
#ifdef ANDROID
    m_recorderPreset = SL_ANDROID_RECORDING_PRESET_GENERIC;
#endif

    for (int i = 0; i < NUM_BUFFERS; i++)
    {
        m_buffers[i] = NULL;
    }
}

GlesInput::~GlesInput()
{
    stop();
}

bool GlesInput::start(int samplerete)
{
    bool ret = false;
    
    if (startRecording(samplerete))
    {
        ret = true;
    }
    else
    {
        log_print(HT_LOG_ERR, "start audio recoding faild\r\n");
        
        stopRecording();        
    }

    return ret;
}

void GlesInput::stop()
{
    stopRecording();
}

bool GlesInput::startRecording(int samplerete)
{
    SLEngineItf engine = GlesEngine::instance()->slEngine();
    if (!engine) 
    {
        log_print(HT_LOG_ERR, "No engine\r\n");
        return false;
    }

    SLresult result;

    // configure audio source
    SLDataLocator_IODevice loc_dev = { SL_DATALOCATOR_IODEVICE, SL_IODEVICE_AUDIOINPUT,
                                       SL_DEFAULTDEVICEID_AUDIOINPUT, NULL };
    SLDataSource audioSrc = { &loc_dev, NULL };

    // configure audio sink
#ifdef ANDROID
    SLDataLocator_AndroidSimpleBufferQueue loc_bq = { SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE,
                                                      NUM_BUFFERS };
#else
    SLDataLocator_BufferQueue loc_bq = { SL_DATALOCATOR_BUFFERQUEUE, NUM_BUFFERS };
#endif

    SLDataFormat_PCM format_pcm;
    
    format_pcm.formatType = SL_DATAFORMAT_PCM;
    format_pcm.numChannels = 2;
    format_pcm.samplesPerSec = samplerete * 1000;
    format_pcm.bitsPerSample = SL_PCMSAMPLEFORMAT_FIXED_16;
    format_pcm.containerSize = SL_PCMSAMPLEFORMAT_FIXED_16;
    format_pcm.channelMask = (SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT);
    format_pcm.endianness = SL_BYTEORDER_LITTLEENDIAN;
    
    SLDataSink audioSnk = { &loc_bq, &format_pcm };

    // create audio recorder
    // (requires the RECORD_AUDIO permission)
#ifdef ANDROID
    const SLInterfaceID id[2] = { SL_IID_ANDROIDSIMPLEBUFFERQUEUE, SL_IID_ANDROIDCONFIGURATION };
    const SLboolean req[2] = { SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE };
#else
    const SLInterfaceID id[1] = { SL_IID_BUFFERQUEUE };
    const SLboolean req[1] = { SL_BOOLEAN_TRUE };
#endif

    result = (*engine)->CreateAudioRecorder(engine, &m_recorderObject,
                                            &audioSrc, &audioSnk,
                                            sizeof(req) / sizeof(SLboolean), id, req);
    if (result != SL_RESULT_SUCCESS) 
    {
        log_print(HT_LOG_ERR, "create audio recorder failed\r\n");
        return false;
    }

#ifdef ANDROID
    // configure recorder source
    SLAndroidConfigurationItf configItf;
    result = (*m_recorderObject)->GetInterface(m_recorderObject, SL_IID_ANDROIDCONFIGURATION,
                                               &configItf);
    if (result != SL_RESULT_SUCCESS) 
    {
        log_print(HT_LOG_ERR, "get audio configuration interface failed\r\n");
        return false;
    }

    result = (*configItf)->SetConfiguration(configItf, SL_ANDROID_KEY_RECORDING_PRESET,
                                            &m_recorderPreset, sizeof(SLuint32));

    SLuint32 presetValue = SL_ANDROID_RECORDING_PRESET_NONE;
    SLuint32 presetSize = 2*sizeof(SLuint32); // intentionally too big
    result = (*configItf)->GetConfiguration(configItf, SL_ANDROID_KEY_RECORDING_PRESET,
                                            &presetSize, (void*)&presetValue);

    if (result != SL_RESULT_SUCCESS || presetValue == SL_ANDROID_RECORDING_PRESET_NONE) 
    {
        log_print(HT_LOG_ERR, "set audio record configuration failed\r\n");
        return false;
    }
#endif

    // realize the audio recorder
    result = (*m_recorderObject)->Realize(m_recorderObject, SL_BOOLEAN_FALSE);
    if (result != SL_RESULT_SUCCESS) 
    {
        log_print(HT_LOG_ERR, "realize audio record object failed\r\n");
        return false;
    }

    // get the record interface
    result = (*m_recorderObject)->GetInterface(m_recorderObject, SL_IID_RECORD, &m_recorder);
    if (result != SL_RESULT_SUCCESS) 
    {
        log_print(HT_LOG_ERR, "get audio record object failed\r\n");
        return false;
    }

    // get the buffer queue interface
#ifdef ANDROID
    SLInterfaceID bufferqueueItfID = SL_IID_ANDROIDSIMPLEBUFFERQUEUE;
#else
    SLInterfaceID bufferqueueItfID = SL_IID_BUFFERQUEUE;
#endif
    result = (*m_recorderObject)->GetInterface(m_recorderObject, bufferqueueItfID, &m_bufferQueue);
    if (result != SL_RESULT_SUCCESS) 
    {
        log_print(HT_LOG_ERR, "get audio record buff queue failed\r\n");
        return false;
    }

    // register callback on the buffer queue
    result = (*m_bufferQueue)->RegisterCallback(m_bufferQueue, ::bufferQueueCallback, this);
    if (result != SL_RESULT_SUCCESS) 
    {
        log_print(HT_LOG_ERR, "set buffer queue callback failed\r\n");
        return false;
    }

    if (m_bufferSize <= 0) 
    {
        m_bufferSize = (16*2/8)*50*samplerete/1000;
    } 
    else 
    {
        int minimumBufSize = (16*2/8)*5*samplerete/1000;
        if (m_bufferSize < minimumBufSize)
            m_bufferSize = minimumBufSize;
    }

    // enqueue empty buffers to be filled by the recorder
    for (int i = 0; i < NUM_BUFFERS; ++i) 
    {
        m_buffers[i] = (uint8 *)malloc(m_bufferSize);
        if (NULL == m_buffers[i])
        {
            return false;
        }

        memset(m_buffers[i], 0, m_bufferSize);
        
        result = (*m_bufferQueue)->Enqueue(m_bufferQueue, m_buffers[i], m_bufferSize);
        if (result != SL_RESULT_SUCCESS) 
        {
            log_print(HT_LOG_ERR, "audio record enqueue failed\r\n");
            return false;
        }
    }

    // start recording
    result = (*m_recorder)->SetRecordState(m_recorder, SL_RECORDSTATE_RECORDING);
    if (result != SL_RESULT_SUCCESS) 
    {
        log_print(HT_LOG_ERR, "set audio record state failed\r\n");
        return false;
    }

    return true;
}

void GlesInput::stopRecording()
{
    if (m_recorder)
    {
        (*m_recorder)->SetRecordState(m_recorder, SL_RECORDSTATE_STOPPED);
    }

    if (m_bufferQueue)
    {
        (*m_bufferQueue)->Clear(m_bufferQueue);
    }

    if (m_recorderObject)
    {
        (*m_recorderObject)->Destroy(m_recorderObject);
    }
    
    m_recorder = NULL;
    m_bufferQueue = NULL;
    m_recorderObject = NULL;

    for (int i = 0; i < NUM_BUFFERS; ++i)
    {
        if (m_buffers[i])
        {
            free(m_buffers[i]);
            m_buffers[i] = NULL;
        }
    }
    
    m_currentBuffer = 0;    
}

void GlesInput::setBufferSize(int value)
{
    m_bufferSize = value;
}

int GlesInput::bufferSize() const
{
    return m_bufferSize;
}

void GlesInput::processBuffer()
{
    uint8 *processedBuffer = m_buffers[m_currentBuffer];

    if (m_pCallback)
    {
        m_pCallback(processedBuffer, m_bufferSize, m_pUserdata);
    }

    // Re-enqueue the buffer
    SLresult result = (*m_bufferQueue)->Enqueue(m_bufferQueue,
                                                processedBuffer,
                                                m_bufferSize);

    m_currentBuffer = (m_currentBuffer + 1) % NUM_BUFFERS;

    // If the buffer queue is empty (shouldn't happen), stop recording.
#ifdef ANDROID
    SLAndroidSimpleBufferQueueState state;
#else
    SLBufferQueueState state;
#endif
    result = (*m_bufferQueue)->GetState(m_bufferQueue, &state);
    if (result != SL_RESULT_SUCCESS || state.count == 0) 
    {
        log_print(HT_LOG_ERR, "processBuffer::state.count == 0\r\n");
    }
}

void GlesInput::setCallback(GlesInputCB cb, void * puser)
{
    m_pCallback = cb;
    m_pUserdata = puser;
}



