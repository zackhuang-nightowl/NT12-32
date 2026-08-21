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
#include "rtsp_parse.h"
#include "rtsp_rsua.h"
#include "rtsp_stream.h"
#include "rtp_tx.h"
#include "rtsp_mc.h"


/***********************************************************************/
void * rtsp_send_thread(void * argv)
{
    rtsp_media_send_thread(argv);

    return NULL;
}

/***********************************************************************/

int rtsp_start_stream_tx(RSSUA * p_rua)
{
    if (p_rua)
    {
        p_rua->rtp_tx = 1;
        p_rua->tid_media = sys_os_create_thread((void *)rtsp_send_thread, p_rua);
    }

    return 1;
}

int rtsp_stop_stream_tx(RSSUA * p_rua)
{
    if (p_rua)
    {
        p_rua->rtp_tx = 0;

        sys_os_wait_thread(&p_rua->tid_media);
    }
    
    return 1;
}

int rtsp_pause_stream_tx(RSSUA * p_rua)
{
    if (p_rua->mc_src)
    {
        // Don't pause the multicast source stream
        return 0;
    }
    
    p_rua->rtp_pause = 1;

#ifdef MEDIA_FILE
    if (p_rua->media_info.is_file)
    {
        CFileDemux * p_Demux = p_rua->media_info.file_demuxer;
        if (p_Demux)
        {
            p_rua->media_info.curpos = p_Demux->getCurPos();
        }
    }
#endif    
    
    return 1;
}

int rtsp_restart_stream_tx(RSSUA * p_rua)
{
    p_rua->rtp_pause = 0;

    return 1;
}




