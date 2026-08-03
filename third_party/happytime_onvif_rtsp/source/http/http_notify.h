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

#ifndef HTTP_HOOK_H
#define HTTP_HOOK_H

#include "sys_inc.h"

#define HTTP_URL_MAX_LEN    256
#define HTTP_NOTIFY_OK      200

typedef enum 
{
    HTTP_NTF_METHOD_POST,
    HTTP_NTF_METHOD_GET
} HTTP_NTF_METHOD;

typedef enum
{
    HTTP_NTF_EVE_CONNECT,
    HTTP_NTF_EVE_PLAY,
    HTTP_NTF_EVE_PUBLISH,
    HTTP_NTF_EVE_DONE
} HTTP_NTF_EVENT;

typedef struct 
{
    HTTP_NTF_METHOD notify_method;
    
    char    on_connect[HTTP_URL_MAX_LEN];
    char    on_play[HTTP_URL_MAX_LEN];
    char    on_publish[HTTP_URL_MAX_LEN];
    char    on_done[HTTP_URL_MAX_LEN];
} HTTP_NOTIFY_CFG;

#ifdef __cplusplus
extern "C" {
#endif

int http_notify_handler(HTTP_NTF_EVENT evt, HTTP_NOTIFY_CFG * hook, const char * params);

#ifdef __cplusplus
}
#endif

#endif


