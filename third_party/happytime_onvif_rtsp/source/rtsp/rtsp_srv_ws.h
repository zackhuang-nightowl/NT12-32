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

#ifndef RTSP_SRV_WS_H
#define RTSP_SRV_WS_H

#include "sys_inc.h"
#include "http.h"
#include "ws.h"


#ifdef __cplusplus
extern "C" {
#endif

BOOL rtsp_ws_msg_process(HTTPCLN * p_user, HTTPMSG * rx_msg);
BOOL rtsp_ws_data_process(HTTPCLN * p_user, char * buff, int len);

#ifdef __cplusplus
}
#endif


#endif



