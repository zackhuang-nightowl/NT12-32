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
#include "gles_engine.h"

#include <SLES/OpenSLES_Android.h>

#define CheckError(message) if (result != SL_RESULT_SUCCESS) { log_print(HT_LOG_ERR, "%s\r\n", message); return; }


static GlesEngine * g_glesEngine;

GlesEngine::GlesEngine()
: m_engineObject(0)
, m_engine(0)
{
    SLresult result;

    result = slCreateEngine(&m_engineObject, 0, 0, 0, 0, 0);
    CheckError("Failed to create engine");

    result = (*m_engineObject)->Realize(m_engineObject, SL_BOOLEAN_FALSE);
    CheckError("Failed to realize engine");

    result = (*m_engineObject)->GetInterface(m_engineObject, SL_IID_ENGINE, &m_engine);
    CheckError("Failed to get engine interface");
}

GlesEngine::~GlesEngine()
{
    if (m_engineObject)
    {
        (*m_engineObject)->Destroy(m_engineObject);
    }    
}

GlesEngine *GlesEngine::instance()
{
    if (NULL == g_glesEngine)
    {
        g_glesEngine = new GlesEngine();
    }
    
    return g_glesEngine;
}

bool GlesEngine::inputFormatIsSupported(SLDataFormat_PCM format)
{
    SLresult result;
    SLObjectItf recorder = 0;
    SLDataLocator_IODevice loc_dev = { SL_DATALOCATOR_IODEVICE, SL_IODEVICE_AUDIOINPUT,
                                       SL_DEFAULTDEVICEID_AUDIOINPUT, NULL };
    SLDataSource audioSrc = { &loc_dev, NULL };

#ifdef ANDROID
    SLDataLocator_AndroidSimpleBufferQueue loc_bq = { SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 1 };
#else
    SLDataLocator_BufferQueue loc_bq = { SL_DATALOCATOR_BUFFERQUEUE, 1 };
#endif
    SLDataSink audioSnk = { &loc_bq, &format };

    result = (*m_engine)->CreateAudioRecorder(m_engine, &recorder, &audioSrc, &audioSnk, 0, 0, 0);
    if (result == SL_RESULT_SUCCESS)
    {
        result = (*recorder)->Realize(recorder, false);
    }
    
    if (result == SL_RESULT_SUCCESS) 
    {
        (*recorder)->Destroy(recorder);
        return true;
    }

    return false;
}


