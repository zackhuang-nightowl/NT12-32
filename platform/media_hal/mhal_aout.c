/***************************************************************************************
 *  mhal_aout.c — 回放音频:AAC/G711 → hd_audiodec → hd_audioout
 *  参照 hdal samples/media_flow/audio_playback.c。
 *  ⚠️ 仅目标机交叉编译(依赖 hdal + dts 音频内存池)。
 ***************************************************************************************/
#include "mhal_aout.h"
#include "mhal_internal.h"
#include "nvr_log.h"
#include "vendor_common.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MHAL_AOUT_BS_LEN  16384

typedef struct {
    int                  opened;
    int                  mod_inited;
    mhal_acodec_t        codec;
    int                  sample_rate;
    int                  out_dev;          /* 0 DAC / 1 ADDA / 2 HDMI */
    int                  volume;
    HD_PATH_ID           adec_path;
    HD_PATH_ID           aout_path;
    HD_COMMON_MEM_VB_BLK bs_blk;
    char                *bs_va;
    UINTPTR              bs_pa;
    pthread_mutex_t      lk;
} mhal_aout_t;

static mhal_aout_t g_ao = {
    .volume = 50,
    .lk = PTHREAD_MUTEX_INITIALIZER,
    .bs_blk = HD_COMMON_MEM_VB_INVALID_BLK,
};

static HD_AUDIO_CODEC to_hd_codec(mhal_acodec_t c)
{
    switch (c) {
        case MHAL_ACODEC_G711U: return HD_AUDIO_CODEC_ULAW;
        case MHAL_ACODEC_G711A: return HD_AUDIO_CODEC_ALAW;
        case MHAL_ACODEC_PCM:   return HD_AUDIO_CODEC_PCM;
        case MHAL_ACODEC_AAC:
        default:                return HD_AUDIO_CODEC_AAC;
    }
}

static HD_AUDIO_SR to_hd_sr(int hz)
{
    if (hz <= 8000)  return HD_AUDIO_SR_8000;
    if (hz <= 11025) return HD_AUDIO_SR_11025;
    if (hz <= 12000) return HD_AUDIO_SR_12000;
    if (hz <= 16000) return HD_AUDIO_SR_16000;
    if (hz <= 22050) return HD_AUDIO_SR_22050;
    if (hz <= 24000) return HD_AUDIO_SR_24000;
    if (hz <= 32000) return HD_AUDIO_SR_32000;
    if (hz <= 44100) return HD_AUDIO_SR_44100;
    return HD_AUDIO_SR_48000;
}

/* 解析 ADTS 头采样率;非 ADTS 返回 0。 */
static int adts_sample_rate(const uint8_t *d, uint32_t len)
{
    static const int krate[] = {
        96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050,
        16000, 12000, 11025, 8000, 7350
    };
    if (!d || len < 7) return 0;
    if (d[0] != 0xFF || (d[1] & 0xF0) != 0xF0) return 0;
    int idx = (d[2] >> 2) & 0x0F;
    if (idx < 0 || idx > 12) return 0;
    return krate[idx];
}

static int resolve_out_dev(int out_dev)
{
    if (out_dev >= 0) return out_dev;
    const char *e = getenv("MHAL_AOUT_DEV");
    if (e && e[0]) return atoi(e);
    return 2; /* 默认 HDMI(回放跟画面走同一输出)；MHAL_AOUT_DEV=0 可改喇叭 */
}

static int ensure_modules(void)
{
    if (g_ao.mod_inited) return 0;
    HD_RESULT r;
    if ((r = hd_audiodec_init()) != HD_OK) {
        NVR_LOGE("aout", "hd_audiodec_init fail %d", (int)r);
        return -1;
    }
    if ((r = hd_audioout_init()) != HD_OK) {
        NVR_LOGE("aout", "hd_audioout_init fail %d", (int)r);
        hd_audiodec_uninit();
        return -1;
    }
    g_ao.mod_inited = 1;
    return 0;
}

static int alloc_bs_buf(void)
{
    if (g_ao.bs_va) return 0;
    g_ao.bs_blk = hd_common_mem_get_block(HD_COMMON_MEM_USER_BLK, MHAL_AOUT_BS_LEN, DDR_ID0);
    if (g_ao.bs_blk == HD_COMMON_MEM_VB_INVALID_BLK) {
        NVR_LOGE("aout", "mem_get_block fail");
        return -1;
    }
    g_ao.bs_pa = hd_common_mem_blk2pa(g_ao.bs_blk);
    if (!g_ao.bs_pa) {
        hd_common_mem_release_block(g_ao.bs_blk);
        g_ao.bs_blk = HD_COMMON_MEM_VB_INVALID_BLK;
        return -1;
    }
    g_ao.bs_va = (char *)hd_common_mem_mmap(HD_COMMON_MEM_MEM_TYPE_NONCACHE,
                                            g_ao.bs_pa, MHAL_AOUT_BS_LEN);
    if (!g_ao.bs_va) {
        hd_common_mem_release_block(g_ao.bs_blk);
        g_ao.bs_blk = HD_COMMON_MEM_VB_INVALID_BLK;
        return -1;
    }
    return 0;
}

static void free_bs_buf(void)
{
    if (g_ao.bs_va) {
        hd_common_mem_munmap(g_ao.bs_va, MHAL_AOUT_BS_LEN);
        g_ao.bs_va = NULL;
    }
    if (g_ao.bs_blk != HD_COMMON_MEM_VB_INVALID_BLK) {
        hd_common_mem_release_block(g_ao.bs_blk);
        g_ao.bs_blk = HD_COMMON_MEM_VB_INVALID_BLK;
    }
}

static int set_params(void)
{
    HD_RESULT ret;
    HD_AUDIODEC_PATH_CONFIG cfg;
    HD_AUDIODEC_IN adec_in;
    HD_AUDIOOUT_IN aout_in;
    UINT ddr_id = DDR_ID0;

    memset(&cfg, 0, sizeof(cfg));
    cfg.data_pool[0].mode = HD_AUDIODEC_POOL_ENABLE;
    if (vendor_common_get_ddrid(HD_COMMON_MEM_AU_DEC_AU_RENDER_IN_POOL,
                                COMMON_PCIE_CHIP_RC, &ddr_id) != HD_OK) {
        NVR_LOGW("aout", "get_ddrid AU_DEC pool fail, fallback DDR0");
        ddr_id = DDR_ID0;
    }
    cfg.data_pool[0].ddr_id = ddr_id;
    cfg.data_pool[0].frame_sample_size = 1024 * 2;
    cfg.data_pool[0].counts = HD_AUDIODEC_SET_COUNT(10, 0);
    ret = hd_audiodec_set(g_ao.adec_path, HD_AUDIODEC_PARAM_PATH_CONFIG, &cfg);
    if (ret != HD_OK) {
        NVR_LOGE("aout", "PATH_CONFIG fail %d", (int)ret);
        return -1;
    }

    memset(&adec_in, 0, sizeof(adec_in));
    adec_in.codec_type  = to_hd_codec(g_ao.codec);
    adec_in.sample_rate = to_hd_sr(g_ao.sample_rate);
    adec_in.sample_bit  = HD_AUDIO_BIT_WIDTH_16;
    adec_in.mode        = HD_AUDIO_SOUND_MODE_MONO;
    ret = hd_audiodec_set(g_ao.adec_path, HD_AUDIODEC_PARAM_IN, &adec_in);
    if (ret != HD_OK) {
        NVR_LOGE("aout", "AUDIODEC_IN fail %d", (int)ret);
        return -1;
    }

    memset(&aout_in, 0, sizeof(aout_in));
    aout_in.sample_rate = to_hd_sr(g_ao.sample_rate);
    ret = hd_audioout_set(g_ao.aout_path, HD_AUDIOOUT_PARAM_IN, &aout_in);
    if (ret != HD_OK) {
        NVR_LOGE("aout", "AUDIOOUT_IN fail %d", (int)ret);
        return -1;
    }

    HD_AUDIOOUT_VOLUME vol = { .volume = (UINT32)g_ao.volume };
    hd_audioout_set(g_ao.aout_path, HD_AUDIOOUT_PARAM_VOLUME, &vol);
    return 0;
}

int mhal_aout_open(mhal_acodec_t codec, int sample_rate, int out_dev)
{
    pthread_mutex_lock(&g_ao.lk);
    if (g_ao.opened) {
        /* 同参数复用;参数变则重建 */
        if (g_ao.codec == codec &&
            g_ao.sample_rate == (sample_rate > 0 ? sample_rate : g_ao.sample_rate) &&
            g_ao.out_dev == resolve_out_dev(out_dev)) {
            pthread_mutex_unlock(&g_ao.lk);
            return 0;
        }
        /* 重建:先关再开 */
        pthread_mutex_unlock(&g_ao.lk);
        mhal_aout_close();
        pthread_mutex_lock(&g_ao.lk);
    }

    if (ensure_modules() != 0) { pthread_mutex_unlock(&g_ao.lk); return -1; }

    g_ao.codec = codec;
    g_ao.sample_rate = sample_rate > 0 ? sample_rate : 16000;
    g_ao.out_dev = resolve_out_dev(out_dev);

    HD_RESULT ret;
    ret = hd_audiodec_open(HD_AUDIODEC_IN(0, 0), HD_AUDIODEC_OUT(0, 0), &g_ao.adec_path);
    if (ret != HD_OK) {
        NVR_LOGE("aout", "audiodec_open fail %d", (int)ret);
        pthread_mutex_unlock(&g_ao.lk);
        return -1;
    }
    ret = hd_audioout_open(HD_AUDIOOUT_IN(g_ao.out_dev, 0),
                           HD_AUDIOOUT_OUT(g_ao.out_dev, 0), &g_ao.aout_path);
    if (ret != HD_OK) {
        NVR_LOGE("aout", "audioout_open fail %d (dev=%d)", (int)ret, g_ao.out_dev);
        hd_audiodec_close(g_ao.adec_path);
        pthread_mutex_unlock(&g_ao.lk);
        return -1;
    }

    if (set_params() != 0) {
        hd_audioout_close(g_ao.aout_path);
        hd_audiodec_close(g_ao.adec_path);
        pthread_mutex_unlock(&g_ao.lk);
        return -1;
    }

    ret = hd_audiodec_bind(HD_AUDIODEC_OUT(0, 0), HD_AUDIOOUT_IN(g_ao.out_dev, 0));
    if (ret != HD_OK) {
        NVR_LOGE("aout", "bind fail %d", (int)ret);
        hd_audioout_close(g_ao.aout_path);
        hd_audiodec_close(g_ao.adec_path);
        pthread_mutex_unlock(&g_ao.lk);
        return -1;
    }

    if (alloc_bs_buf() != 0) {
        hd_audiodec_unbind(HD_AUDIODEC_OUT(0, 0));
        hd_audioout_close(g_ao.aout_path);
        hd_audiodec_close(g_ao.adec_path);
        pthread_mutex_unlock(&g_ao.lk);
        return -1;
    }

    hd_audiodec_start(g_ao.adec_path);
    hd_audioout_start(g_ao.aout_path);
    g_ao.opened = 1;
    NVR_LOGI("aout", "opened codec=%d sr=%d out_dev=%d vol=%d",
             (int)codec, g_ao.sample_rate, g_ao.out_dev, g_ao.volume);
    pthread_mutex_unlock(&g_ao.lk);
    return 0;
}

void mhal_aout_close(void)
{
    pthread_mutex_lock(&g_ao.lk);
    if (!g_ao.opened) { pthread_mutex_unlock(&g_ao.lk); return; }
    hd_audiodec_stop(g_ao.adec_path);
    hd_audioout_stop(g_ao.aout_path);
    hd_audiodec_unbind(HD_AUDIODEC_OUT(0, 0));
    hd_audioout_close(g_ao.aout_path);
    hd_audiodec_close(g_ao.adec_path);
    free_bs_buf();
    g_ao.adec_path = 0;
    g_ao.aout_path = 0;
    g_ao.opened = 0;
    pthread_mutex_unlock(&g_ao.lk);
    NVR_LOGI("aout", "closed");
}

int mhal_aout_send(mhal_acodec_t codec, int sample_rate,
                   const uint8_t *data, uint32_t len, uint64_t ts_us)
{
    if (!data || len == 0 || len > MHAL_AOUT_BS_LEN) return -1;

    /* ADTS 可纠正采样率 */
    if (codec == MHAL_ACODEC_AAC) {
        int sr = adts_sample_rate(data, len);
        if (sr > 0) sample_rate = sr;
    }
    if (sample_rate <= 0) sample_rate = 16000;

    if (!g_ao.opened) {
        if (mhal_aout_open(codec, sample_rate, -1) != 0) return -1;
    } else if (g_ao.codec != codec ||
               (sample_rate > 0 && abs(g_ao.sample_rate - sample_rate) > 100)) {
        /* 格式变化 → 重建通路 */
        mhal_aout_close();
        if (mhal_aout_open(codec, sample_rate, -1) != 0) return -1;
    }

    pthread_mutex_lock(&g_ao.lk);
    if (!g_ao.opened || !g_ao.bs_va) { pthread_mutex_unlock(&g_ao.lk); return -1; }

    memcpy(g_ao.bs_va, data, len);

    HD_AUDIODEC_SEND_LIST sl;
    memset(&sl, 0, sizeof(sl));
    sl.path_id = g_ao.adec_path;
    sl.user_bs.sign = MAKEFOURCC('A', 'S', 'T', 'M');
    sl.user_bs.acodec_format = to_hd_codec(codec);
    sl.user_bs.timestamp = ts_us;
    sl.user_bs.p_user_buf = g_ao.bs_va;
    sl.user_bs.user_buf_size = len;

    HD_RESULT ret = hd_audiodec_send_list(&sl, 1, 50);
    pthread_mutex_unlock(&g_ao.lk);
    if (ret < 0) {
        /* 偶发忙不刷屏 */
        return -1;
    }
    return 0;
}

int mhal_aout_set_volume(int vol)
{
    if (vol < 0) vol = 0;
    if (vol > 100) vol = 100;
    pthread_mutex_lock(&g_ao.lk);
    g_ao.volume = vol;
    if (g_ao.opened) {
        HD_AUDIOOUT_VOLUME v = { .volume = (UINT32)vol };
        hd_audioout_set(g_ao.aout_path, HD_AUDIOOUT_PARAM_VOLUME, &v);
    }
    pthread_mutex_unlock(&g_ao.lk);
    return 0;
}

int mhal_aout_get_volume(void)
{
    return g_ao.volume;
}

int mhal_aout_is_open(void)
{
    return g_ao.opened;
}
