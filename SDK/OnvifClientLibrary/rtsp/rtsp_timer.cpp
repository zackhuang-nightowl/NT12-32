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
#include "rtsp_timer.h"
#include "rtsp_rsua.h"
#include "rtsp_srv.h"
#include "rtsp_stream.h"


/***************************************************************************************/
extern RTSP_CLASS    g_rtsp;

/***************************************************************************************
 timer message handler
****************************************************************************************/
void rtsp_timer()
{
    BOOL timeout;
    uint32 i;
    uint32 cur_time = sys_os_get_uptime();
    RSSUA * p_sua = NULL;
    
    for (i = 0; i < MAX_NUM_RUA; i++)
    {
        p_sua = (RSSUA *)rsua_get_by_index(i);
        if (p_sua == NULL)
        {
            continue;
        }

        if (!p_sua->used_flag)
        {
            continue;
        }

        timeout = FALSE;
        
        if (p_sua->state != RSS_PLAYING && p_sua->state != RSS_RECORDING && cur_time - p_sua->last_rx_time > 10)
        {
            timeout = TRUE;
        }
        else if (cur_time - p_sua->last_rx_time > g_rtsp.session_timeout + 10)
        {
            timeout = TRUE;
        }

        if (timeout)
        {
            // the session timeout, close it

            log_print(HT_LOG_INFO, "%s, session timeout\r\n", __FUNCTION__);
            rtsp_close_rua(p_sua);
            continue;
        }

#ifdef RTSP_REPLAY
        if (p_sua->replay && p_sua->play_range && p_sua->play_range_end != 0)
        {
            uint32 diff1 = (sys_os_get_ms() - p_sua->s_replay_time) / 1000;
            uint32 diff2 = p_sua->play_range_end - p_sua->play_range_begin;

            if (diff1 > diff2) // Playback arrival end time
            {
                rtsp_pause_stream_tx(p_sua);
            }
        }
#endif
    }
}

#if __WINDOWS_OS__

#pragma comment(lib, "winmm.lib")

#ifdef _WIN64
void CALLBACK rtsp_win_timer(UINT uTimerID, UINT uMsg, DWORD_PTR dwUser, DWORD_PTR dw1, DWORD_PTR dw2)
#else
void CALLBACK rtsp_win_timer(UINT uID, UINT uMsg, DWORD dwUser, DWORD dw1, DWORD dw2)
#endif
{
    if (g_rtsp.sys_init_flag)
    {
        RTSPMSG stm;
        memset(&stm, 0, sizeof(RTSPMSG));
        
        stm.msg_src = RTSP_TIMER_SRC;
        
        hqueue_put(g_rtsp.msg_queue, (char *)&stm);
    }
}

void rtsp_timer_init()
{
    g_rtsp.timer_id = timeSetEvent(1000, 0, rtsp_win_timer, 0, TIME_PERIODIC);
}

void rtsp_timer_deinit()
{
    timeKillEvent(g_rtsp.timer_id);
}

#else

void * rtsp_timer_task(void * argv)
{
    RTSPMSG stm;
    struct timespec ts;
    
    memset(&stm, 0, sizeof(RTSPMSG));
    
    while (g_rtsp.sys_timer_run)
    {        
        ts.tv_sec = 1;
        ts.tv_nsec = 0;
        
        nanosleep(&ts, NULL);
        
        stm.msg_src = RTSP_TIMER_SRC;
        
        hqueue_put(g_rtsp.msg_queue, (char *)&stm);
    }
    
    log_print(HT_LOG_DBG, "rtsp timer task exit\r\n");

    return NULL;
}

void rtsp_timer_init()
{
    g_rtsp.sys_timer_run = 1;

    pthread_t tid = sys_os_create_thread((void *)rtsp_timer_task, NULL);
    if (tid == 0)
    {
        log_print(HT_LOG_ERR, "%s, rtsp_timer_task failed\r\n", __FUNCTION__);
        return;
    }

    g_rtsp.timer_id = tid;

    log_print(HT_LOG_DBG, "%s, create rtsp timer thread sucessful\r\n", __FUNCTION__);
}

void rtsp_timer_deinit()
{
    g_rtsp.sys_timer_run = 0;

    sys_os_wait_thread(&g_rtsp.timer_id);
}

#endif



