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

#ifndef VIDIO_CAPTURE_QT_H
#define VIDIO_CAPTURE_QT_H

#include "sys_inc.h"
#include "video_capture.h"
#include <QObject>
#include <QJniObject>


/***************************************************************************************/
class CQVideoCapture : public QObject, public CVideoCapture
{
    Q_OBJECT
    
public:
    CQVideoCapture(QObject * parent = NULL);
    ~CQVideoCapture();
    
    // get video capture devices numbers
    static int  getDeviceNums();
    // list avaivalbe video capture device
    static void listDevice();
    // get device index by device name
    static int  getDeviceIndex(const char * name);

    // get the single instance
    static CVideoCapture * getInstance(int devid);

    BOOL    initCapture(int codec, int width, int height, double framerate, int bitrate);
    BOOL    startCapture();
    void    processFrame(uint8 * data, int size, int width, int height, int format, int bpl);
    void    processError(int error);
    void    cameraThread();
    
    static void initJNI(JNIEnv *env);

signals:
    void    imageReady(uint8 * data, int size, int width, int height, int format, int bpl);
    
private:
    BOOL    startCamera();
    BOOL    startPreivew();
    void    stopCamera();
    void    stopCapture();
    void    checkError();
    
public:
    QJniObject  m_camera;
    QJniObject  m_parameters;
    QJniObject  m_cameraListener;
    QJniObject  m_surfaceTexture;
    
    int         m_nCodec;
    int         m_nDstWidth;
    int         m_nDstHeight;
    int         m_nFrameCount;
    BOOL        m_bError;
    BOOL        m_bRunning;
    BOOL        m_bStartCamera;
    BOOL        m_bStartCameraResult;
    BOOL        m_bStopCamera;
    BOOL        m_bStartPreview;
    BOOL        m_bStartPreviewResult;
    pthread_t   m_pCameraThread;
};

#endif // VIDIO_CAPTURE_QT_H


