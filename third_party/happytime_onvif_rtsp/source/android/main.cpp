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
#include "onvif_srv.h"
#include "rtsp_srv.h"
#include "http_parse.h"
#include "OnvifRtspServerDlg.h"
#include <QApplication>


/**************************************************************************************/

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    QApplication::setApplicationName("onvifrtspserver");
    QApplication::setOrganizationName("happytimesoft");

    signal(SIGPIPE, SIG_IGN);

    sys_buf_init(MAX_NUM_RUA * 2 + 8);
    http_msg_buf_init(MAX_NUM_RUA * 2 + 8);

    OnvifRtspServerDlg w;
    
    w.showMaximized();

    return a.exec();
}



