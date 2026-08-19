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

#ifndef ONVIF_RTSP_SERVER_DLG_H
#define ONVIF_RTSP_SERVER_DLG_H

#include "sys_inc.h"
#include <QDialog>
#include <QVideoFrame>
#include "ui_OnvifRtspServerDlg.h"


class OnvifRtspServerDlg : public QDialog
{
    Q_OBJECT

public:
    explicit OnvifRtspServerDlg(QWidget *parent = 0);
    ~OnvifRtspServerDlg();

protected:
    void closeEvent(QCloseEvent * event);

private slots:
    void slotStart();
    void slotStop();
    void slotAbout();
    void slotVideo(bool checked);
    void slotAudio(bool checked);
    void slotCameraChanged(int index);
    void slotServerIpChanged(int index);
    void slotPortChanged(int port);

private:
    void initIpAddress();
    void initDialog();
    void connSignalSlot();
    BOOL copyFileFromResource(const QString& sourcePath, const QString& targetPath);
    BOOL copyAssetsFiles();
    void setComboItem(QComboBox * item, QVariant data);
    void updateStreamUrl(QString ip, int port, int video, int cameraidx, int audio);

private:
    Ui::OnvifRtspServerDlg * ui;
};

#endif // ONVIF_RTSP_SERVER_DLG_H



