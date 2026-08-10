/***************************************************************************************
 *  nvr_nop8012_proto.c — 8012 协议编解码实现。见 nvr_nop8012_proto.h。
 ***************************************************************************************/
#include "nvr_nop8012_proto.h"
#include <string.h>

static void wr_le32(uint8_t *p, uint32_t v){
    p[0] = v & 0xFF; p[1] = (v>>8) & 0xFF; p[2] = (v>>16) & 0xFF; p[3] = (v>>24) & 0xFF;
}
static uint32_t rd_le32(const uint8_t *p){
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}

int nvr_n8012_pack_header(uint8_t *buf, uint32_t cmd, uint32_t data_size,
                          uint32_t msg_type, uint32_t extend_flag)
{
    memset(buf, 0, NVR_N8012_HDR_SIZE);
    wr_le32(buf+0,  NVR_N8012_MAGIC);
    wr_le32(buf+4,  1);            /* version=1(支持 extendDataFlag=2) */
    wr_le32(buf+8,  data_size);
    wr_le32(buf+12, cmd);
    /* buf+16 timestamp: 保留 0 */
    wr_le32(buf+20, msg_type);
    wr_le32(buf+24, extend_flag);
    return NVR_N8012_HDR_SIZE;
}

int nvr_n8012_pack_login(uint8_t *buf, const char *user, const char *pass)
{
    nvr_n8012_pack_header(buf, N8012_CMD_LOGIN, NVR_N8012_LOGIN_SIZE, 0, 0);
    uint8_t *p = buf + NVR_N8012_HDR_SIZE;
    memset(p, 0, NVR_N8012_LOGIN_SIZE);
    if (user) strncpy((char *)p,       user, 24 - 1);
    if (pass) strncpy((char *)(p + 24), pass, 40 - 1);
    return NVR_N8012_HDR_SIZE + NVR_N8012_LOGIN_SIZE;
}

int nvr_n8012_pack_heartbeat(uint8_t *buf)
{
    return nvr_n8012_pack_header(buf, N8012_CMD_HEARTBEAT, 0, 0, 0);
}

int nvr_n8012_parse_header(const uint8_t *b, nvr_n8012_hdr_t *o)
{
    if (rd_le32(b) != NVR_N8012_MAGIC) return -1;
    o->magic       = rd_le32(b+0);
    o->version     = rd_le32(b+4);
    o->data_size   = rd_le32(b+8);
    o->cmd         = rd_le32(b+12);
    o->timestamp   = rd_le32(b+16);
    o->msg_type    = rd_le32(b+20);
    o->extend_flag = rd_le32(b+24);
    o->rsv0        = rd_le32(b+28);
    o->rsv1        = rd_le32(b+32);
    o->rsv2        = rd_le32(b+36);
    return 0;
}

int nvr_n8012_extract_jpeg(uint32_t flag, const uint8_t *pl, uint32_t pl_len,
                           const uint8_t **jpeg, uint32_t *len)
{
    if (flag == 1) {
        if (!pl_len) return -1;
        *jpeg = pl; *len = pl_len; return 0;
    }
    if (flag == 2) {
        uint32_t off = 0;
        while (off + 8 <= pl_len) {
            uint32_t dt = rd_le32(pl + off);
            uint32_t ds = rd_le32(pl + off + 4);
            off += 8;
            if (off + ds > pl_len) break;       /* 越界:停止 */
            if (dt == 1) { *jpeg = pl + off; *len = ds; return 0; }
            off += ds;
        }
    }
    return -1;
}
