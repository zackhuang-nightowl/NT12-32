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

#ifndef RTSP_MC_H
#define RTSP_MC_H

typedef struct _rtsp_multicast_ua
{
    char    uri[512];
    uint32  mcuaidx;
    int     refcnt;
} RMCUA;

#ifdef __cplusplus
extern "C" {
#endif

void    rmc_proxy_init();
void    rmc_proxy_deinit();
RMCUA * rmc_get_idle();
void    rmc_set_online(RMCUA * p_rua);
void    rmc_set_idle(RMCUA * p_rua);
RMCUA * rmc_lookup_start();
RMCUA * rmc_lookup_next(RMCUA * p_rua);
void    rmc_lookup_stop();
uint32  rmc_get_index(RMCUA * p_rua);
RMCUA * rmc_get_by_index(uint32 index);

RMCUA * rtsp_mc_find(RSSUA * p_rua);
BOOL    rtsp_mc_add_src(RSSUA * p_rua);
int     rtsp_mc_del_src(RSSUA * p_rua);
RMCUA * rtsp_mc_add_ref(RSSUA * p_rua);
void    rtsp_mc_del_ref(RSSUA * p_rua);

#ifdef __cplusplus
}
#endif

#endif



