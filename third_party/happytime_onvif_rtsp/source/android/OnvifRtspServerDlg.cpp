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

#include "OnvifRtspServerDlg.h"
#include "utils.h"
#include "video_capture_qt.h"
#include "About.h"
#include "media_format.h"
#include "rtsp_srv.h"
#include "rtsp_cfg.h"
#include "onvif.h"
#include "onvif_cfg.h"
#include "onvif_srv.h"
#include <QUrl>
#include <QFile>
#include <QCloseEvent>
#include <QMessageBox>
#include <QMediaDevices>
#include <QCameraDevice>
#include <QJniObject>
#include <QJniEnvironment>
#include <QDesktopServices>
#include <QStandardPaths>


/**************************************************************************************/

QJniEnvironment g_env;

extern RTSP_CLASS g_rtsp;

extern "C" ONVIF_CLS    g_onvif_cls;
extern "C" ONVIF_CFG    g_onvif_cfg;

/**************************************************************************************/

OnvifRtspServerDlg::OnvifRtspServerDlg(QWidget *parent) :
QDialog(parent),
ui(new Ui::OnvifRtspServerDlg)
{
    ui->setupUi(this);

    initDialog();
    connSignalSlot();

    copyAssetsFiles();

    CQVideoCapture::initJNI(g_env.jniEnv());

    QJniObject::callStaticMethod<void>("org/happytimesoft/util/HtUtil", 
        "disableLockScreen",
        "(Landroid/content/Context;)V",
        QNativeInterface::QAndroidApplication::context());

#ifdef DEMO
    QMessageBox::information(this, tr("Tips"), tr("Happytime ONVIF RTSP Server DEMO version"));
#endif

    QJniObject str = QJniObject::fromString("android.permission.CAMERA");
    QJniObject::callStaticMethod<jint>("org/happytimesoft/util/HtUtil", 
        "requestPermission",
        "(Landroid/content/Context;Ljava/lang/String;)I",
        QNativeInterface::QAndroidApplication::context(),
        str.object<jstring>());

    QJniObject str1 = QJniObject::fromString("android.permission.RECORD_AUDIO");
    QJniObject::callStaticMethod<jint>("org/happytimesoft/util/HtUtil", 
        "requestPermission",
        "(Landroid/content/Context;Ljava/lang/String;)I",
        QNativeInterface::QAndroidApplication::context(),
        str1.object<jstring>());
}

OnvifRtspServerDlg::~OnvifRtspServerDlg()
{
    if (ui->btnStop->isEnabled())
    {
        rtsp_stop();
        onvif_stop();
    }

    QJniObject::callStaticMethod<void>("org/happytimesoft/util/HtUtil", "enableLockScreen");

#ifdef DEMO    
    QDesktopServices::openUrl(QUrl("https://www.happytimesoft.com/", QUrl::TolerantMode));
#endif

    delete ui;
}

void OnvifRtspServerDlg::closeEvent(QCloseEvent * event)
{
    if (QMessageBox::Yes == QMessageBox::question(this, tr("Quit"), tr("Are you sure want to quit?")))
    {
        event->accept();
    }
    else
    {
        event->ignore();
    }
}

void OnvifRtspServerDlg::initIpAddress()
{
    int ret;
    uint32 size = 15000;
    char * buff;
    HT_IFINFOTABLE * table;

    buff = (char *) malloc(size);
    if (NULL == buff)
    {
        return;
    }
    
    table = (HT_IFINFOTABLE *) buff;
    ret = get_ifinfo_table(table, &size);
    if (ret == 0)
    {
        free(buff);
        buff = (char *) malloc(size);
        if (NULL == buff)
        {
            return;
        }
        
        table = (HT_IFINFOTABLE *) buff;
        ret = get_ifinfo_table(table, &size);
    }

    if (ret > 0)
    {
        uint32 i;

        for (i = 0; i < table->count; i++)
        {
            if (inet_addr(table->table[i].ip) == htonl(INADDR_LOOPBACK))
            {
                continue;
            }

            ui->cmbServerIp->addItem(table->table[i].ip);
        }
    }

    free(buff);
}

void OnvifRtspServerDlg::initDialog()
{
    initIpAddress();
    
    ui->spinRtspPort->setValue(getRtspPort());
    ui->spinOnvifPort->setValue(getOnvifPort());
    
    ui->cmbVideoCodec->addItem("H264", VIDEO_CODEC_H264);
    ui->cmbVideoCodec->addItem("H265", VIDEO_CODEC_H265);

    ui->cmbAudioCodec->addItem("G711A", AUDIO_CODEC_G711A);
    ui->cmbAudioCodec->addItem("G711U", AUDIO_CODEC_G711U);
    ui->cmbAudioCodec->addItem("OPUS", AUDIO_CODEC_OPUS);
    ui->cmbAudioCodec->addItem("AAC", AUDIO_CODEC_AAC);

    ui->cmbVideoSize->addItem("default", QSize(0, 0));
    ui->cmbVideoSize->addItem("1920*1080", QSize(1920, 1080));
    ui->cmbVideoSize->addItem("1280*720", QSize(1280, 720));
    ui->cmbVideoSize->addItem("640*480", QSize(640, 480));

    ui->cmbVideoFps->addItem("30", 30);
    ui->cmbVideoFps->addItem("25", 25);
    ui->cmbVideoFps->addItem("20", 20);
    ui->cmbVideoFps->addItem("15", 15);
    ui->cmbVideoFps->addItem("10", 10);

    ui->cmbVideoBps->addItem("default", 0);
    ui->cmbVideoBps->addItem("4000", 4000);
    ui->cmbVideoBps->addItem("3000", 3000);
    ui->cmbVideoBps->addItem("2500", 2500);
    ui->cmbVideoBps->addItem("2000", 2000);
    ui->cmbVideoBps->addItem("1500", 1500);
    ui->cmbVideoBps->addItem("1000", 1000);
    ui->cmbVideoBps->addItem("800", 800);
    ui->cmbVideoBps->addItem("500", 500);
    ui->cmbVideoBps->addItem("300", 300);
    ui->cmbVideoBps->addItem("100", 100);

    ui->cmbSampleRate->addItem("48000", 48000);
    ui->cmbSampleRate->addItem("44100", 44100);
    ui->cmbSampleRate->addItem("32000", 32000);
    ui->cmbSampleRate->addItem("24000", 24000);
    ui->cmbSampleRate->addItem("22050", 22050);
    ui->cmbSampleRate->addItem("16000", 16000);
    ui->cmbSampleRate->addItem("12000", 12000);
    ui->cmbSampleRate->addItem("11025", 11025);
    ui->cmbSampleRate->addItem("8000", 8000);

    ui->cmbChannel->addItem("stereo", 2);
    ui->cmbChannel->addItem("Mono", 1);    

    ui->cmbAudioBps->addItem("default", 0);
    ui->cmbAudioBps->addItem("500", 500);
    ui->cmbAudioBps->addItem("300", 300);
    ui->cmbAudioBps->addItem("100", 100);
    ui->cmbAudioBps->addItem("64", 64);
    ui->cmbAudioBps->addItem("32", 32);

    ui->cmbLogLevel->addItem("TRACE", HT_LOG_TRC);
    ui->cmbLogLevel->addItem("DEBUG", HT_LOG_DBG);
    ui->cmbLogLevel->addItem("INFO", HT_LOG_INFO);
    ui->cmbLogLevel->addItem("WARNING", HT_LOG_WARN);
    ui->cmbLogLevel->addItem("ERROR", HT_LOG_ERR);
    ui->cmbLogLevel->addItem("FATAL", HT_LOG_FATAL);

    QList<QCameraDevice> cameras = QMediaDevices::videoInputs();
    foreach (const QCameraDevice &camera, cameras)
    {
        ui->cmbCamera->addItem(camera.description(), camera.position());
    }

    ui->grpVideo->setChecked(getVideoFlag());
    ui->grpAudio->setChecked(getAudioFlag());
    ui->cmbCamera->setCurrentIndex(getCameraIndex());
    ui->chkLogEnable->setChecked(getLogEnable());
    setComboItem(ui->cmbVideoCodec, getVideoCodec());
    setComboItem(ui->cmbAudioCodec, getAudioCodec());
    setComboItem(ui->cmbVideoSize, getResolution());
    setComboItem(ui->cmbVideoFps, getFrameRate());
    setComboItem(ui->cmbVideoBps, getVideoBitrate());
    setComboItem(ui->cmbSampleRate, getSampleRate());
    setComboItem(ui->cmbChannel, getChannel());
    setComboItem(ui->cmbAudioBps, getAudioBitrate());
    setComboItem(ui->cmbLogLevel, getLogLevel());

    updateStreamUrl(ui->cmbServerIp->currentText(), 
        ui->spinRtspPort->value(), ui->grpVideo->isChecked(), 
        ui->cmbCamera->currentIndex(), ui->grpAudio->isChecked());

    ui->btnStop->setEnabled(false);
    ui->labStatus->setText(tr("Ready"));
}

void OnvifRtspServerDlg::connSignalSlot()
{
    connect(ui->btnStart, SIGNAL(clicked()), this, SLOT(slotStart()));
    connect(ui->btnStop, SIGNAL(clicked()), this, SLOT(slotStop()));
    connect(ui->btnAbout, SIGNAL(clicked()), this, SLOT(slotAbout()));
    connect(ui->grpVideo, SIGNAL(clicked(bool)), this, SLOT(slotVideo(bool)));
    connect(ui->grpAudio, SIGNAL(clicked(bool)), this, SLOT(slotAudio(bool)));
    connect(ui->cmbCamera, SIGNAL(currentIndexChanged(int)), this, SLOT(slotCameraChanged(int)));
    connect(ui->cmbServerIp, SIGNAL(currentIndexChanged(int)), this, SLOT(slotServerIpChanged(int)));
    connect(ui->spinRtspPort, SIGNAL(valueChanged(int)), this, SLOT(slotPortChanged(int)));
}

BOOL OnvifRtspServerDlg::copyFileFromResource(const QString& sourcePath, const QString& targetPath)
{
    QFile sourceFile(sourcePath);
    
    if (!sourceFile.open(QIODevice::ReadOnly)) 
    {
        return FALSE;
    }

    QFile targetFile(targetPath);
    
    if (!targetFile.open(QIODevice::WriteOnly))
    {
        sourceFile.close();
        return FALSE;
    }

    targetFile.write(sourceFile.readAll());
    sourceFile.close();
    targetFile.close();

    return TRUE;
}

BOOL OnvifRtspServerDlg::copyAssetsFiles()
{
    QString targetDir = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).last();
    QString sourceDir = "assets:";

    copyFileFromResource(sourceDir+"/snapshot.jpg", targetDir+"/snapshot.jpg");

    return TRUE;
}

void OnvifRtspServerDlg::setComboItem(QComboBox * item, QVariant data)
{
    for (int i = 0; i < item->count(); i++)
    {
        if (item->itemData(i) == data)
        {
            item->setCurrentIndex(i);
            break;
        }
    }
}

void OnvifRtspServerDlg::slotStart()
{
    memset(&g_rtsp_cfg, 0, sizeof(g_rtsp_cfg));

    QString ip = ui->cmbServerIp->currentText();

    if (!ip.isEmpty())
    {
        strcpy(g_rtsp_cfg.serverip, ip.toStdString().c_str());
    }
    
    g_rtsp_cfg.log_enable = ui->chkLogEnable->isChecked();
    g_rtsp_cfg.log_level = ui->cmbLogLevel->currentIndex();
    g_rtsp_cfg.rtsp_port = ui->spinRtspPort->value();
#ifdef RTSP_METADATA    
    g_rtsp_cfg.metadata = 1;
#endif

    int video = ui->grpVideo->isChecked();
    int audio = ui->grpAudio->isChecked();
    int cameraidx = ui->cmbCamera->currentIndex();
    char url[256] = {'\0'};
    
    if (video && audio)
    {
        if (cameraidx > 0)
        {
            snprintf(url, sizeof(url), "videodevice%d+audiodevice", cameraidx);
        }
        else
        {
            snprintf(url, sizeof(url), "videodevice+audiodevice");
        }
    }
    else if (video)
    {
        if (cameraidx > 0)
        {
            snprintf(url, sizeof(url), "videodevice%d", cameraidx);
        }
        else
        {
            snprintf(url, sizeof(url), "videodevice");
        }
    }
    else if (audio)
    {
        snprintf(url, sizeof(url), "audiodevice");
    }
    else
    {
        QMessageBox::information(this, tr("Tips"), tr("Please check video or audio."));
        return;
    }

#ifdef DEMO
    if (VIDEO_CODEC_H265 == ui->cmbVideoCodec->currentData().toInt())
    {
        ui->cmbVideoCodec->setFocus();
        QMessageBox::information(this, tr("Tips"), tr("DEMO version does not support H265."));
        return;
    }
#endif

    QSize size = ui->cmbVideoSize->currentData().toSize();

    MEDIA_OUTPUT * p_output = rtsp_add_output(&g_rtsp_cfg.output);
    if (NULL == p_output)
    {
        QMessageBox::information(this, tr("Tips"), tr("Add output configuration failed."));
        return;
    }

    memset(p_output, 0, sizeof(MEDIA_OUTPUT));

    // Do not check the URL, all URLs use the settings on the UI, 
    //  if you need to check the URL, do not comment the following line 
    // strcpy(p_output->url, url);
    
    p_output->video.codec = ui->cmbVideoCodec->currentData().toInt();
    p_output->video.width = size.width();
    p_output->video.height = size.height();
    p_output->video.framerate = ui->cmbVideoFps->currentData().toInt();
    p_output->video.bitrate = ui->cmbVideoBps->currentData().toInt();

    p_output->audio.codec = ui->cmbAudioCodec->currentData().toInt();
    p_output->audio.samplerate = ui->cmbSampleRate->currentData().toInt();
    p_output->audio.channels = ui->cmbChannel->currentData().toInt();
    p_output->audio.bitrate = ui->cmbAudioBps->currentData().toInt();

    saveRtspPort(g_rtsp_cfg.rtsp_port);
    saveOnvifPort(ui->spinOnvifPort->value());
    saveVideoFlag(video);
    saveVideoCodec(p_output->video.codec);
    saveCameraIndex(cameraidx);
    saveFrameRate(p_output->video.framerate);
    saveVideoBitrate(p_output->video.bitrate);
    saveResolution(size);
    saveAudioFlag(audio);
    saveAudioCodec(p_output->audio.codec);
    saveSampleRate(p_output->audio.samplerate);
    saveChannel(p_output->audio.channels);
    saveAudioBitrate(p_output->audio.bitrate);
    saveLogEnable(g_rtsp_cfg.log_enable);
    saveLogLevel(g_rtsp_cfg.log_level);

    if (g_rtsp_cfg.log_enable)
    {
        char file[512] = {'\0'};
        QString path = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).last();

        snprintf(file, sizeof(file), "%s/%s", path.toStdString().c_str(), ONVIF_LOG_FILE);
        
        log_init(file);
        log_set_level(g_rtsp_cfg.log_level);
    }
    else
    {
        log_close();
    }

    QJniObject::callStaticMethod<void>("org/happytimesoft/util/HtUtil", 
        "enableMulticast", 
        "(Landroid/content/Context;)V",
        QNativeInterface::QAndroidApplication::context());
            
    if (rtsp_start1() && (g_rtsp.rtsp.ipv4_fd > 0 || g_rtsp.rtsp.ipv6_fd > 0))
    {
        onvif_init_def_cfg();

        QFile file("assets:/onvif.cfg");
        if (file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            QByteArray bytes = file.readAll();

            onvif_parse_cfg(bytes.data(), bytes.size());
        }

        strcpy(g_onvif_cfg.server_ip, g_rtsp_cfg.serverip);
        
        onvif_init_cfg();
        
        g_onvif_cfg.http_port = ui->spinOnvifPort->value();

        QString path = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).last();
        
        path += "/snapshot.jpg";
        
        strncpy(g_onvif_cfg.snapshot,  path.toStdString().c_str(), sizeof(g_onvif_cfg.snapshot)-1);

        onvif_start1();

        g_onvif_cls.rtsp_port = g_rtsp_cfg.rtsp_port;
    
        ui->labStatus->setText(tr("Started"));

        ui->btnStart->setEnabled(false);
        ui->btnStop->setEnabled(true);
    }
    else
    {
        ui->labStatus->setText(tr("Bind port failed"));
    }
}

void OnvifRtspServerDlg::slotStop()
{
    rtsp_stop();
    onvif_stop();
    
    ui->btnStart->setEnabled(true);
    ui->btnStop->setEnabled(false);

    ui->labStatus->setText(tr("Ready"));

    QJniObject::callStaticMethod<void>("org/happytimesoft/util/HtUtil", "disableMulticast");
}

void OnvifRtspServerDlg::slotAbout()
{
    About dlg;

    dlg.exec();
}

void OnvifRtspServerDlg::updateStreamUrl(QString ip, int port, int video, int cameraidx, int audio)
{
    QString addr, suffix, url;

    if (port == 554)
    {
        addr = QString("rtsp://%1").arg(ip);
    }
    else
    {
        addr = QString("rtsp://%1:%2").arg(ip).arg(port);
    }
            
    if (video && audio)
    {
        if (cameraidx > 0)
        {
            suffix = QString("videodevice%1+audiodevice").arg(cameraidx);
        }
        else
        {
            suffix = QString("videodevice+audiodevice");
        }
    }
    else if (video)
    {
        if (cameraidx > 0)
        {
            suffix = QString("videodevice%1").arg(cameraidx);
        }
        else
        {
            suffix = QString("videodevice");
        }
    }
    else if (audio)
    {
        suffix = QString("audiodevice");
    }
    else
    {
        suffix = "";
    }

    url = addr + "/" + suffix;

    ui->editRtspUrl->setText(url);
}

void OnvifRtspServerDlg::slotVideo(bool checked)
{
    updateStreamUrl(ui->cmbServerIp->currentText(), 
        ui->spinRtspPort->value(), checked, 
        ui->cmbCamera->currentIndex(), ui->grpAudio->isChecked());
}

void OnvifRtspServerDlg::slotAudio(bool checked)
{
    updateStreamUrl(ui->cmbServerIp->currentText(), 
        ui->spinRtspPort->value(), ui->grpVideo->isChecked(), 
        ui->cmbCamera->currentIndex(), checked);
}

void OnvifRtspServerDlg::slotCameraChanged(int index)
{
    updateStreamUrl(ui->cmbServerIp->currentText(),
        ui->spinRtspPort->value(), ui->grpVideo->isChecked(), 
        index, ui->grpAudio->isChecked());
}

void OnvifRtspServerDlg::slotServerIpChanged(int index)
{
    updateStreamUrl(ui->cmbServerIp->itemText(index),
        ui->spinRtspPort->value(), ui->grpVideo->isChecked(),
        ui->cmbCamera->currentIndex(), ui->grpAudio->isChecked());
}

void OnvifRtspServerDlg::slotPortChanged(int port)
{
    updateStreamUrl(ui->cmbServerIp->currentText(),
        port, ui->grpVideo->isChecked(), 
        ui->cmbCamera->currentIndex(), ui->grpAudio->isChecked());
}



