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
#include "About.h"
#include "rtsp_srv.h"
#include <QScreen>


About::About(QWidget *parent)
: QDialog(parent)
{
    ui.setupUi(this);

    ui.labVersion->setText(QString("Happytime Onvif Rtsp Server"));
    
    connect(ui.btnOK, SIGNAL(clicked()), this, SLOT(close()));

    showMaximized();
}


About::~About()
{
}



    
