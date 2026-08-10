/***************************************************************************************
 *  nvr_playback.c — 本机录像回放引擎(见 nvr_playback.h)。
 *  单通道全屏回放:rsdk_group_query 找段 → rsdk_group_play_next2 取解密 Annex-B 帧
 *  →(按 hdr.stream 过滤 + 跳音频 + I 帧起播)→ mhal_vdec_send 解码到全屏窗口。
 *  接管窗口0:play 前 nvr_preview_fullscreen + 关 live 解码;stop 后归还 live。
 ***************************************************************************************/
#include "nvr_playback.h"
#include "nvr_streaming.h"
#include "nvr_preview.h"
#include "mhal_vdec.h"
#include "mhal_vout.h"
#include "rsdk_balance.h"
#include "rsdk_types.h"
#include "nvr_log.h"

#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>

#define PB_WIN       0        /* 全屏显示窗口 */
#define PB_MAX_SEGS  512

struct nvr_playback {
    nvr_playback_cfg_t cfg;
    pthread_mutex_t    lock;
    pthread_t          th;
    int                have_thread;
    volatile int       running;      /* feeder 存活 */
    volatile int       paused;
    volatile int       chn0;         /* 当前回放通道(0-based),-1=无 */
    volatile int       want_stream;  /* NVR_STREAM_MAIN/SUB(超预算回退子) */
    volatile int       speed_num;    /* 1/2/4/8:倍速(节奏) */
    uint32_t           start_wall;   /* 起播 epoch */
    int                backward;
    char               status[16], speed[16], direction[16];
    volatile uint32_t  cur_wall;     /* 当前回放位置 */
    int                disp_mode;    /* 回放布局(GUI_setPlaybackMode 记录) */
    int                ch_list[16];  /* 1-based 通道 */
    int                ch_count;
    int                saved_mode, saved_page;  /* 进回放前的 live 布局,stop 时恢复 */
    int                saved_valid;
};

static long now_ms(void){ struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts);
                          return ts.tv_sec*1000L + ts.tv_nsec/1000000L; }
static int parse_speed(const char *s){ if(!s||!s[0])return 1; int n=atoi(s); return (n>=1&&n<=16)?n:1; }

/* feeder:读段组帧 → 过滤 → 解码上屏,按 pts 节奏。pb->running=0 退出。 */
static void *pb_feeder(void *arg)
{
    nvr_playback_t *pb = arg;
    rsdk_group_t   *g  = pb->cfg.group;
    int chn0 = pb->chn0;
    uint32_t start_wall = pb->start_wall;

    rsdk_index_slot_t *segs = (rsdk_index_slot_t*)malloc(sizeof(rsdk_index_slot_t)*PB_MAX_SEGS);
    if(!segs){ pb->running=0; return NULL; }
    int nseg = rsdk_group_query(g, start_wall, start_wall + 24*3600, chn0,
                                RSDK_REC_CONTINUOUS, segs, PB_MAX_SEGS);
    if(nseg<=0){ NVR_LOGW("pb","chn%d 起播 %u 无录像段", chn0+1, start_wall);
                 snprintf(pb->status,sizeof(pb->status),"stopped"); free(segs); pb->running=0; return NULL; }

    rsdk_group_player_t *gp=NULL;
    if(rsdk_group_play_open(g, segs, nseg, &gp)!=RSDK_OK || !gp){ free(segs); pb->running=0; return NULL; }
    free(segs);

    mhal_vdec_t *vdec=NULL; int opened=0;
    long base_ms=0; uint64_t base_pts=0;

    while(pb->running){
        if(pb->paused){ usleep(40000); continue; }
        rsdk_frame_hdr_t h; const uint8_t *data=NULL; uint32_t len=0; int disk=0, gap=0;
        if(rsdk_group_play_next2(gp, &h, &data, &len, &disk, &gap)!=RSDK_OK) break;   /* 放完 */
        if(h.frame_type==RSDK_FRAME_AUDIO) continue;             /* 跳音频 */
        if((int)h.stream != pb->want_stream)  continue;          /* 只放选中码流(主/子) */
        if((uint32_t)h.wall_time < start_wall) continue;         /* 跳过起播点之前 */

        if(!opened){
            if(h.frame_type != RSDK_FRAME_I) continue;           /* 从 I 帧起 */
            mhal_codec_t mc = (h.codec==RSDK_CODEC_H265)?MHAL_CODEC_H265:MHAL_CODEC_H264;
            int w=1920,ht=1080,fps=25; nvr_stream_dim(pb->cfg.sm, chn0, pb->want_stream, &w,&ht,&fps);
            int orc = mhal_vdec_open(chn0, mc, w, ht, fps, PB_WIN, &vdec);
            if(orc==MHAL_VDEC_EBUDGET && pb->want_stream==NVR_STREAM_MAIN){
                pb->want_stream=NVR_STREAM_SUB; vdec=NULL;       /* 主超预算 → 退子流,等子 I 帧 */
                NVR_LOGW("pb","chn%d 主流超解码预算 → 改子流回放", chn0+1);
                continue;
            }
            if(orc!=0 || !vdec){ vdec=NULL; NVR_LOGE("pb","chn%d 开解码失败 %d", chn0+1, orc); break; }
            /* 视频区:从(0,0)占 0.8×W × 0.8×H(其余留给 GUI 时间轴/控件) */
            int vw = pb->cfg.hdmi_w>0 ? pb->cfg.hdmi_w*4/5 : 1536;
            int vh = pb->cfg.hdmi_h>0 ? pb->cfg.hdmi_h*4/5 : 864;
            mhal_vout_bind_rect(chn0, 0, 0, vw, vh);
            opened=1; base_ms=now_ms(); base_pts=h.pts;
            NVR_LOGI("pb","chn%d ▶回放 win%d %s @%u", chn0+1, PB_WIN,
                     pb->want_stream==NVR_STREAM_SUB?"子":"主", start_wall);
        }
        int speed = pb->speed_num>0?pb->speed_num:1;
        long target = base_ms + (long)(((h.pts - base_pts)/90)/speed);   /* pts 90kHz → ms /倍速 */
        long dt = target - now_ms();
        if(dt>0 && dt<5000) usleep((useconds_t)(dt*1000));
        else if(dt<=-2000){ base_ms=now_ms(); base_pts=h.pts; }          /* 落后太多重定基准 */
        mhal_vdec_send(vdec, data, len, (uint32_t)h.pts);
        pb->cur_wall = (uint32_t)h.wall_time;
    }
    if(vdec) mhal_vdec_close(vdec);
    rsdk_group_play_close(gp);
    if(pb->running){ snprintf(pb->status,sizeof(pb->status),"stopped"); pb->running=0; }  /* 自然放完 */
    NVR_LOGI("pb","chn%d ■回放退出", chn0+1);
    return NULL;
}

/* 进入回放模式:接管全屏窗口并**黑屏**(关 live 解码 + 清黑),不显示 LiveView。须已持锁。 */
static void pb_blackout_locked(nvr_playback_t *pb, int chn0)
{
    if(chn0<0) return;
    if(pb->cfg.pv && !pb->saved_valid){                           /* 首次接管:记住 live 布局供 stop 恢复 */
        if(nvr_preview_get_mode(pb->cfg.pv, &pb->saved_mode, &pb->saved_page)==0) pb->saved_valid=1;
    }
    if(pb->cfg.pv) nvr_preview_fullscreen(pb->cfg.pv, chn0);       /* 单画面布局(切布局会清黑) */
    if(pb->cfg.sm) nvr_stream_set_display(pb->cfg.sm, chn0, -1);   /* 关 live 解码,不再刷 live */
    mhal_vout_clear_black();                                       /* 清黑,抹掉残留 live 帧 */
    pb->chn0 = chn0;
}

/* 停当前回放线程 + 黑屏(留在回放模式的黑底,不回 live;回 live 由 GUI 切 live 布局触发)。须已持锁。 */
static void pb_stop_locked(nvr_playback_t *pb)
{
    if(pb->have_thread){ pb->running=0; pthread_join(pb->th,NULL); pb->have_thread=0; }
    /* ★ 归还 live:恢复进回放前布局(nvr_preview_set_mode 对可见格重开解码出图),否则回放接管的
     * 窗口停在"关解码/黑屏",退出回放后 live 不再出图(实测:playback 后全黑)。 */
    if(pb->cfg.pv && pb->saved_valid){
        nvr_preview_set_mode(pb->cfg.pv, pb->saved_mode, pb->saved_page);
        pb->saved_valid=0;
    } else {
        mhal_vout_clear_black();
    }
    pb->chn0 = -1;
    snprintf(pb->status,sizeof(pb->status),"stopped");
}

/* 进入回放模式黑屏(供 GUI_setPlaybackMode:一进回放就黑,不显示 live)。 */
void nvr_playback_enter(nvr_playback_t *pb, int chn1)
{
    if(!pb) return;
    int chn0 = (chn1>0)? chn1-1 : (pb->chn0>=0?pb->chn0:0);
    pthread_mutex_lock(&pb->lock);
    if(pb->have_thread){ pb->running=0; pthread_join(pb->th,NULL); pb->have_thread=0; }
    pb_blackout_locked(pb, chn0);
    snprintf(pb->status,sizeof(pb->status),"stopped");
    pthread_mutex_unlock(&pb->lock);
}

int nvr_playback_create(const nvr_playback_cfg_t *cfg, nvr_playback_t **out)
{
    if(!cfg||!out) return -1;
    nvr_playback_t *pb = (nvr_playback_t*)calloc(1,sizeof(*pb));
    if(!pb) return -1;
    pb->cfg = *cfg; pb->chn0=-1; pb->want_stream=NVR_STREAM_MAIN; pb->speed_num=1;
    snprintf(pb->status,sizeof(pb->status),"stopped");
    snprintf(pb->speed,sizeof(pb->speed),"1X");
    snprintf(pb->direction,sizeof(pb->direction),"forward");
    pthread_mutex_init(&pb->lock,NULL);
    *out = pb; return 0;
}

void nvr_playback_destroy(nvr_playback_t *pb)
{
    if(!pb) return;
    pthread_mutex_lock(&pb->lock); pb_stop_locked(pb); pthread_mutex_unlock(&pb->lock);
    pthread_mutex_destroy(&pb->lock); free(pb);
}

int nvr_playback_control(nvr_playback_t *pb, const char *action, int chn1,
                         uint32_t start_wall, const char *speed, const char *direction)
{
    if(!pb||!action) return -1;
    pthread_mutex_lock(&pb->lock);
    if(speed&&speed[0])    { snprintf(pb->speed,sizeof(pb->speed),"%s",speed); pb->speed_num=parse_speed(speed); }
    if(direction&&direction[0]){ snprintf(pb->direction,sizeof(pb->direction),"%s",direction);
                                 pb->backward = (strcmp(direction,"backward")==0); }

    int is_pause = (strcmp(action,"pause")==0);
    int is_stop  = (strcmp(action,"stop")==0);
    int is_resume= (strcmp(action,"resume")==0) ||
                   ((strcmp(action,"play")==0||strcmp(action,"start")==0) && pb->have_thread && pb->paused && start_wall==0);

    if(is_pause){
        if(pb->have_thread){ pb->paused=1; snprintf(pb->status,sizeof(pb->status),"paused"); }
    } else if(is_stop){
        pb_stop_locked(pb);
    } else if(is_resume){
        pb->paused=0; snprintf(pb->status,sizeof(pb->status),"playing");
    } else {  /* play / start / seek → (重)起播 */
        int chn0 = (chn1>0)? chn1-1 : pb->chn0;
        uint32_t sw = (start_wall>0)? start_wall : pb->start_wall;
        if(chn0<0 || sw==0){ pthread_mutex_unlock(&pb->lock); return -1; }  /* 无通道/起点 */
        pb_stop_locked(pb);
        pb->start_wall=sw; pb->want_stream=NVR_STREAM_MAIN; pb->paused=0;
        pb->cur_wall=sw;
        pb_blackout_locked(pb, chn0);   /* 先黑屏接管窗口(无录像则保持黑;有录像 feeder 盖上视频) */
        pb->running=1;
        if(pthread_create(&pb->th,NULL,pb_feeder,pb)==0){ pb->have_thread=1; snprintf(pb->status,sizeof(pb->status),"playing"); }
        else { pb->running=0; snprintf(pb->status,sizeof(pb->status),"stopped"); }
    }
    pthread_mutex_unlock(&pb->lock);
    return 0;
}

void nvr_playback_set_mode(nvr_playback_t *pb, int display_mode, const int *channels1, int n)
{
    if(!pb) return;
    pthread_mutex_lock(&pb->lock);
    pb->disp_mode = display_mode;
    if(n>16) n=16; if(n<0) n=0;
    pb->ch_count = n;
    for(int i=0;i<n;i++) pb->ch_list[i] = channels1?channels1[i]:0;
    /* 停当前回放 + 黑屏接管(进回放即黑,不显示 live);主放通道=列表首个 */
    if(pb->have_thread){ pb->running=0; pthread_join(pb->th,NULL); pb->have_thread=0; }
    int chn0 = (n>0 && pb->ch_list[0]>0)? pb->ch_list[0]-1 : (pb->chn0>=0?pb->chn0:0);
    pb_blackout_locked(pb, chn0);
    snprintf(pb->status,sizeof(pb->status),"stopped");
    pthread_mutex_unlock(&pb->lock);
}

int nvr_playback_get_mode(nvr_playback_t *pb, int *channels_out, int cap, int *n)
{
    if(!pb) return 4;
    pthread_mutex_lock(&pb->lock);
    int dm = pb->disp_mode>0?pb->disp_mode:1;
    int cnt = pb->ch_count>0?pb->ch_count:1;
    if(cnt>cap) cnt=cap;
    for(int i=0;i<cnt;i++) channels_out[i] = pb->ch_count>0?pb->ch_list[i]:(i+1);
    if(n) *n=cnt;
    pthread_mutex_unlock(&pb->lock);
    return dm;
}

void nvr_playback_get_status(nvr_playback_t *pb, char *status, char *speed,
                             char *direction, uint32_t *cur_wall)
{
    if(!pb) return;
    pthread_mutex_lock(&pb->lock);
    if(status)    snprintf(status,16,"%s",pb->status);
    if(speed)     snprintf(speed,16,"%s",pb->speed);
    if(direction) snprintf(direction,16,"%s",pb->direction);
    if(cur_wall)  *cur_wall = pb->cur_wall;
    pthread_mutex_unlock(&pb->lock);
}
