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
#include "rtsp_rsua.h"
#include "rtsp_mc.h"
#include "rtsp_srv.h"

/***********************************************************************/

extern RTSP_CLASS    g_rtsp;

/***********************************************************************/
void rmc_proxy_init()
{
    g_rtsp.rmc_fl = pps_ctx_fl_init(MAX_NUM_RUA, sizeof(RMCUA), TRUE);
    g_rtsp.rmc_ul = pps_ctx_ul_init(g_rtsp.rmc_fl, TRUE);
    g_rtsp.rmc_mutex = sys_os_create_mutex();
}

void rmc_proxy_deinit()
{
    if (g_rtsp.rmc_ul)
    {
        pps_ul_free(g_rtsp.rmc_ul);
        g_rtsp.rmc_ul = NULL;
    }

    if (g_rtsp.rmc_fl)
    {
        pps_fl_free(g_rtsp.rmc_fl);
        g_rtsp.rmc_fl = NULL;
    }

    if (g_rtsp.rmc_mutex)
    {
        sys_os_destroy_mutex(g_rtsp.rmc_mutex);
        g_rtsp.rmc_mutex = NULL;
    }
}

RMCUA * rmc_get_idle()
{
    RMCUA * p_rua = (RMCUA *)pps_fl_pop(g_rtsp.rmc_fl);
    if (p_rua)
    {
        memset(p_rua, 0, sizeof(RMCUA));
    }
    else
    {
        log_print(HT_LOG_ERR, "%s, don't have idle rtsp rua!!!\r\n", __FUNCTION__);
    }

    return p_rua;
}

void rmc_set_online(RMCUA * p_rua)
{
    pps_ctx_ul_add(g_rtsp.rmc_ul, p_rua);
}

void rmc_set_idle(RMCUA * p_rua)
{
    pps_ctx_ul_del(g_rtsp.rmc_ul, p_rua);
    
    memset(p_rua, 0, sizeof(RMCUA));
    
    pps_fl_push_tail(g_rtsp.rmc_fl, p_rua);
}

RMCUA * rmc_lookup_start()
{
    return (RMCUA *)pps_lookup_start(g_rtsp.rmc_ul);
}

RMCUA * rmc_lookup_next(RMCUA * p_rua)
{
    return (RMCUA *)pps_lookup_next(g_rtsp.rmc_ul, p_rua);
}

void rmc_lookup_stop()
{
    pps_lookup_end(g_rtsp.rmc_ul);
}

uint32 rmc_get_index(RMCUA * p_rua)
{
    return pps_get_index(g_rtsp.rmc_fl, p_rua);
}

RMCUA * rmc_get_by_index(uint32 index)
{
    return (RMCUA *)pps_get_node_by_index(g_rtsp.rmc_fl, index);
}

/***********************************************************************/

RMCUA * rtsp_mc_find(RSSUA * p_rua)
{
    RMCUA * p_mcua = rmc_lookup_start();
    while (p_mcua)
    {
        if (strncmp(p_rua->uri, p_mcua->uri, strlen(p_mcua->uri)) == 0)
        {
            break;
        }
        
        p_mcua = rmc_lookup_next(p_mcua);
    }
    rmc_lookup_stop();

    return p_mcua;
}

BOOL rtsp_mc_add_src(RSSUA * p_rua)
{
    BOOL ret = FALSE;
    RMCUA * p_mcua;
    
    sys_os_mutex_enter(g_rtsp.rmc_mutex);
    
    p_mcua = rmc_get_idle();
    if (p_mcua)
    {
        p_rua->mc_src = 1;
        strcpy(p_mcua->uri, p_rua->uri);
        p_mcua->mcuaidx = rsua_get_index(p_rua);
        p_mcua->refcnt = 1;

        rmc_set_online(p_mcua);

        ret = TRUE;
    }
    
    sys_os_mutex_leave(g_rtsp.rmc_mutex);
    
    return ret;
}

int rtsp_mc_del_src(RSSUA * p_rua)
{
    int refcnt = 0;
    RMCUA * p_mcua = rtsp_mc_find(p_rua);
    if (NULL == p_mcua)
    {
        return 0;
    }
    
    sys_os_mutex_enter(g_rtsp.rmc_mutex);

    if (0 == p_rua->mc_del)
    {
        p_rua->mc_src = 0;
        p_rua->mc_del = 1;
        p_mcua->refcnt--;
    }
    
    refcnt = p_mcua->refcnt;

    if (0 == refcnt)
    {
        rmc_set_idle(p_mcua);
    }
    
    sys_os_mutex_leave(g_rtsp.rmc_mutex);

    return refcnt;
}

RMCUA * rtsp_mc_add_ref(RSSUA * p_rua)
{
    sys_os_mutex_enter(g_rtsp.rmc_mutex);
    
    RMCUA * p_mcua = rtsp_mc_find(p_rua);
    if (p_mcua)
    {
        p_rua->mc_ref = 1;
        p_mcua->refcnt++;
    }

    sys_os_mutex_leave(g_rtsp.rmc_mutex);
    
    return p_mcua;
}

void rtsp_mc_del_ref(RSSUA * p_rua)
{
    RMCUA * p_mcua = rtsp_mc_find(p_rua);
    if (NULL == p_mcua)
    {
        return;
    }
    
    sys_os_mutex_enter(g_rtsp.rmc_mutex);

    if (0 == p_rua->mc_del)
    {
        p_rua->mc_ref = 0;
        p_rua->mc_del = 1;
        p_mcua->refcnt--;
    }

    if (0 == p_mcua->refcnt)
    {
        rmc_set_idle(p_mcua);
    }
    
    sys_os_mutex_leave(g_rtsp.rmc_mutex);
}



