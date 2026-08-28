/* Copyright (C) 2025-2026, Nightowl DG. RSDK 设备信息/健康. */
#define _GNU_SOURCE
#include "rsdk_disk.h"
#include "rsdk_storgedev.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <scsi/sg.h>

/* devpath 末段(/dev/sda -> sda), 供 /sys 查询 */
static void base_name(const char *p, char *out, size_t n) {
    const char *s = strrchr(p, '/'); s = s ? s + 1 : p;
    snprintf(out, n, "%s", s);
}

/* /sys/block/<base>/queue/rotational: 0=SSD 1=HDD; 取不到 -1 */
static int sys_rotational(const char *devpath) {
    char base[64], path[160]; base_name(devpath, base, sizeof base);
    snprintf(path, sizeof path, "/sys/block/%s/queue/rotational", base);
    FILE *f = fopen(path, "r"); if (!f) return -1;
    int v = -1; if (fscanf(f, "%d", &v) != 1) v = -1; fclose(f);
    return v < 0 ? -1 : (v == 0 ? 1 : 0);
}

/* /sys/block/<base>/removable: 1=可移动(USB) 0=固定; 取不到 -1 */
static int sys_removable(const char *devpath) {
    char base[64], path[160]; base_name(devpath, base, sizeof base);
    snprintf(path, sizeof path, "/sys/block/%s/removable", base);
    FILE *f = fopen(path, "r"); if (!f) return -1;
    int v = -1; if (fscanf(f, "%d", &v) != 1) v = -1; fclose(f);
    return v;
}

#define RSDK_SMART_REALLOC_MAX 64u   /* 重映射扇区超此值判不健康 */

/* ATA PASS-THROUGH(16) PIO Data-In: 读 1 扇区(512B)到 buf。成功 0。 */
static int ata_pio_in(int fd, uint8_t feat, uint8_t lo, uint8_t mid,
                      uint8_t hi, uint8_t cmd, uint8_t buf[512]) {
    uint8_t cdb[16] = {0}; uint8_t sense[32] = {0};
    cdb[0]=0x85;            /* ATA PASS-THROUGH(16) */
    cdb[1]=(4u<<1);        /* protocol = 4 (PIO Data-In) */
    cdb[2]=0x0E;          /* t_dir=1(in), byt_blok=1, t_length=2(=sector_count) */
    cdb[4]=feat;          /* feature 7:0 */
    cdb[6]=1;             /* sector count = 1 */
    cdb[8]=lo; cdb[10]=mid; cdb[12]=hi;
    cdb[14]=cmd;
    sg_io_hdr_t io; memset(&io, 0, sizeof io);
    io.interface_id='S'; io.dxfer_direction=SG_DXFER_FROM_DEV;
    io.cmd_len=16; io.cmdp=cdb; io.dxferp=buf; io.dxfer_len=512;
    io.sbp=sense; io.mx_sb_len=sizeof sense; io.timeout=5000;
    if (ioctl(fd, SG_IO, &io) < 0) return -1;
    if (io.host_status != 0) return -1;
    return 0;
}

/* ATA IDENTIFY 字符串: 每 16-bit word 内字节对调; words[first..last] 拷出去尾空格 */
static void ata_str(const uint8_t *id, int word_first, int word_last, char *out, size_t n) {
    size_t k = 0;
    for (int w = word_first; w <= word_last && k + 2 < n; w++) {
        out[k++] = (char)id[w*2 + 1];   /* 高字节先 */
        out[k++] = (char)id[w*2 + 0];
    }
    out[k] = '\0';
    while (k > 0 && (out[k-1] == ' ' || out[k-1] == '\0')) out[--k] = '\0';
}

rsdk_err_t rsdk_disk_identify(const char *devpath, rsdk_disk_info_t *out) {
    if (!devpath || !out) return RSDK_E_PARAM;
    memset(out, 0, sizeof *out);
    out->logical_sec = 512; out->physical_sec = 512; out->is_ssd = -1;
    out->is_removable = sys_removable(devpath);

    int fd = open(devpath, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return RSDK_E_IO;
    struct stat st;
    if (fstat(fd, &st) < 0) { close(fd); return RSDK_E_IO; }

    if (S_ISBLK(st.st_mode)) {
        uint64_t bytes = 0; int ls = 0, ps = 0;
        if (ioctl(fd, BLKGETSIZE64, &bytes) == 0) out->capacity_bytes = bytes;
        if (ioctl(fd, BLKSSZGET,  &ls) == 0 && ls > 0) out->logical_sec  = (uint32_t)ls;
        if (ioctl(fd, BLKPBSZGET, &ps) == 0 && ps > 0) out->physical_sec = (uint32_t)ps;
        int rot = sys_rotational(devpath); if (rot >= 0) out->is_ssd = rot;
        /* ATA IDENTIFY: 填 model/serial/firmware; word217 覆盖 is_ssd */
        uint8_t id[512];
        if (ata_pio_in(fd, 0x00, 0x00, 0x00, 0x00, 0xEC, id) == 0) {
            ata_str(id, 27, 46, out->model,    sizeof out->model);    /* words 27-46 */
            ata_str(id, 10, 19, out->serial,   sizeof out->serial);   /* words 10-19 */
            ata_str(id, 23, 26, out->firmware, sizeof out->firmware); /* words 23-26 */
            uint16_t w217 = (uint16_t)(id[217*2] | id[217*2+1]<<8);   /* 转速; 1=SSD */
            if (w217 == 1) out->is_ssd = 1;
            else if (w217 >= 0x0401 && w217 <= 0xFFFE) out->is_ssd = 0;
        }
    } else {
        out->capacity_bytes = (uint64_t)st.st_size;   /* 普通文件(自测) */
    }
    /* 分类:mmcblk→SD卡; 可移动→USB; 否则按转速 SSD/HDD */
    { char cbase[64]; base_name(devpath, cbase, sizeof cbase);
      if      (strncmp(cbase, "mmcblk", 6) == 0) out->dclass = RSDK_DISKCLASS_SDCARD;
      else if (out->is_removable == 1)           out->dclass = RSDK_DISKCLASS_USB;
      else if (out->is_ssd == 1)                 out->dclass = RSDK_DISKCLASS_SSD;
      else                                       out->dclass = RSDK_DISKCLASS_HDD; }
    close(fd);
    return RSDK_OK;
}

const char *rsdk_disk_class_str(rsdk_disk_class_t c) {
    switch (c) {
        case RSDK_DISKCLASS_SDCARD: return "sdcard";
        case RSDK_DISKCLASS_USB:    return "usb";
        case RSDK_DISKCLASS_HDD:
        case RSDK_DISKCLASS_SSD:    return "hdd";   /* SSD 也归 hdd 名(接口无 ssd 类别) */
        default:                    return "hdd";
    }
}

void rsdk_disk_unified_name(const char *devpath, int seq, char *out, size_t n) {
    if (!out || n == 0) return;
    out[0] = 0;
    if (!devpath) return;
    char base[64]; base_name(devpath, base, sizeof base);
    if (strncmp(base, "mmcblk", 6) == 0) {          /* SD 卡 */
        snprintf(out, n, "sdcard");
        return;
    }
    const char *kind = (sys_removable(devpath) == 1) ? "usb" : "hdd";  /* 可移动=USB, 否则固定盘 */
    if (seq <= 0) snprintf(out, n, "%s", kind);      /* 首个: hdd / usb */
    else          snprintf(out, n, "%s%d", kind, seq);  /* 次个: hdd1 / usb1 .. */
}

/* SMART RETURN STATUS(non-data + CK_COND): *fail=1 表示阈值超标(盘将坏)。成功 0。 */
static int ata_smart_status(int fd, int *fail) {
    uint8_t cdb[16] = {0}; uint8_t sense[32] = {0};
    cdb[0]=0x85; cdb[1]=(3u<<1); /* non-data */ cdb[2]=0x20; /* CK_COND */
    cdb[4]=0xDA; cdb[10]=0x4F; cdb[12]=0xC2; cdb[14]=0xB0;
    sg_io_hdr_t io; memset(&io, 0, sizeof io);
    io.interface_id='S'; io.dxfer_direction=SG_DXFER_NONE;
    io.cmd_len=16; io.cmdp=cdb; io.sbp=sense; io.mx_sb_len=sizeof sense; io.timeout=5000;
    if (ioctl(fd, SG_IO, &io) < 0) return -1;
    /* descriptor-format sense(0x72/0x73): 找 ATA Return descriptor(0x09) */
    if ((sense[0]&0x7f)==0x72 || (sense[0]&0x7f)==0x73) {
        int len = sense[7], i = 8;
        while (i + 1 < 8 + len && i + 13 < (int)sizeof sense) {
            if (sense[i] == 0x09) {                 /* [9]=lba_mid, [11]=lba_high */
                uint8_t lba_mid = sense[i+9], lba_high = sense[i+11];
                *fail = (lba_mid==0xF4 && lba_high==0x2C);
                return 0;
            }
            i += sense[i+1] + 2;
        }
    }
    return -1;
}

rsdk_err_t rsdk_smart_read(const char *devpath, rsdk_smart_t *out) {
    if (!devpath || !out) return RSDK_E_PARAM;
    memset(out, 0, sizeof *out);
    out->healthy = -1; out->temp_c = -1;

    int fd = open(devpath, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return RSDK_E_IO;

    /* 总体健康 */
    int fail = 0;
    if (ata_smart_status(fd, &fail) == 0) out->healthy = fail ? 0 : 1;

    /* 属性表 */
    uint8_t data[512];
    if (ata_pio_in(fd, 0xD0, 0x00, 0x4F, 0xC2, 0xB0, data) == 0) {
        for (int i = 0; i < 30; i++) {
            const uint8_t *a = data + 2 + i*12;
            int id = a[0]; if (!id) continue;
            uint64_t raw = (uint64_t)a[5] | (uint64_t)a[6]<<8 | (uint64_t)a[7]<<16
                         | (uint64_t)a[8]<<24 | (uint64_t)a[9]<<32 | (uint64_t)a[10]<<40;
            switch (id) {
                case 5:   out->reallocated    = raw & 0xFFFFFFFFu; break;
                case 9:   out->power_on_hours = raw & 0xFFFFFFFFu; break;
                case 190: if (out->temp_c < 0) out->temp_c = (int)(raw & 0xFF); break;
                case 194: out->temp_c         = (int)(raw & 0xFF); break;
                case 197: out->pending        = raw & 0xFFFFFFFFu; break;
                case 198: out->offline_uncorr = raw & 0xFFFFFFFFu; break;
                case 199: out->crc_errors     = raw & 0xFFFFFFFFu; break;
            }
        }
        /* 若总体健康取不到, 用属性兜底判定 */
        if (out->healthy < 0 && (out->pending || out->reallocated >= RSDK_SMART_REALLOC_MAX
                                 || out->offline_uncorr))
            out->healthy = 0;
    }
    close(fd);
    return RSDK_OK;   /* 取不到 = 未知, 非错误 */
}

int rsdk_smart_ok(const rsdk_smart_t *s) {
    if (!s) return 0;
    if (s->healthy == 0) return 0;                        /* 显式 FAIL */
    if (s->pending > 0) return 0;
    if (s->reallocated >= RSDK_SMART_REALLOC_MAX) return 0;
    return 1;                                             /* healthy==1 或 -1(未知放行) */
}

int rsdk_smart_read_attrs(const char *devpath, rsdk_smart_attr_t *out, int cap) {
    if (!devpath || !out || cap <= 0) return -1;
    int fd = open(devpath, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;

    uint8_t data[512], thr[512];
    int have_thr = (ata_pio_in(fd, 0xD1, 0x00, 0x4F, 0xC2, 0xB0, thr) == 0);  /* thresholds 页 */
    if (ata_pio_in(fd, 0xD0, 0x00, 0x4F, 0xC2, 0xB0, data) != 0) { close(fd); return -1; }

    int n = 0;
    for (int i = 0; i < RSDK_SMART_ATTR_MAX && n < cap; i++) {
        const uint8_t *a = data + 2 + i * 12;     /* 每条 12 字节, 前 2 字节为版本 */
        int id = a[0]; if (!id) continue;
        rsdk_smart_attr_t *o = &out[n++];
        o->id    = (uint8_t)id;
        o->flags = (uint16_t)(a[1] | a[2] << 8);
        o->value = a[3];
        o->worst = a[4];
        o->raw   = (uint64_t)a[5] | (uint64_t)a[6]<<8 | (uint64_t)a[7]<<16
                 | (uint64_t)a[8]<<24 | (uint64_t)a[9]<<32 | (uint64_t)a[10]<<40;
        o->thresh = 0;
        if (have_thr) {                            /* thresholds 页同结构: [0]=id [1]=thresh */
            for (int j = 0; j < RSDK_SMART_ATTR_MAX; j++) {
                const uint8_t *t = thr + 2 + j * 12;
                if (t[0] == id) { o->thresh = t[1]; break; }
            }
        }
    }
    close(fd);
    return n;
}

const char *rsdk_smart_attr_name(uint8_t id) {
    switch (id) {
        case 1:   return "Raw Read Error Rate";
        case 2:   return "Throughput Performance";
        case 3:   return "Spin-Up Time";
        case 4:   return "Start/Stop Count";
        case 5:   return "Reallocated Sectors Count";
        case 7:   return "Seek Error Rate";
        case 8:   return "Seek Time Performance";
        case 9:   return "Power-On Hours";
        case 10:  return "Spin Retry Count";
        case 12:  return "Power Cycle Count";
        case 173: return "Wear Leveling Count";
        case 174: return "Unexpected Power Loss";
        case 177: return "Wear Range Delta";
        case 183: return "SATA Downshift Error Count";
        case 184: return "End-to-End Error";
        case 187: return "Reported Uncorrectable Errors";
        case 188: return "Command Timeout";
        case 190: return "Airflow Temperature";
        case 191: return "G-Sense Error Rate";
        case 192: return "Power-Off Retract Count";
        case 193: return "Load/Unload Cycle Count";
        case 194: return "Temperature";
        case 195: return "Hardware ECC Recovered";
        case 196: return "Reallocation Event Count";
        case 197: return "Current Pending Sector Count";
        case 198: return "Offline Uncorrectable Sector Count";
        case 199: return "UDMA CRC Error Count";
        case 200: return "Write Error Rate";
        case 240: return "Head Flying Hours";
        case 241: return "Total LBAs Written";
        case 242: return "Total LBAs Read";
        default:  return "Unknown";
    }
}

rsdk_err_t rsdk_disk_probe(const char *devpath, rsdk_disk_status_t *out) {
    if (!devpath || !out) return RSDK_E_PARAM;
    memset(out, 0, sizeof *out);
    rsdk_superblock_t sb;
    rsdk_err_t rc = rsdk_peek_superblock(devpath, &sb);
    if (rc == RSDK_E_FORMAT) { out->formatted = 0; return RSDK_OK; } /* 未格式化: 合法结果 */
    if (rc) return rc;
    out->formatted = 1;
    uint64_t chunk_bytes = sb.chunk_sectors * RSDK_SEC;
    uint64_t data_chunks = sb.chunk_count - sb.meta_chunk_count;
    out->data_bytes  = data_chunks * chunk_bytes;
    out->wrapped     = sb.seq_epoch > 1;
    out->used_bytes  = out->wrapped ? out->data_bytes
                                    : sb.write_ptr_chunk * chunk_bytes;
    out->free_bytes  = out->data_bytes - out->used_bytes;   /* 空闲(已绕盘=0) */
    out->feature_mask = sb.feature_mask;
    out->enc_algo     = sb.enc_algo;
    return RSDK_OK;
}
