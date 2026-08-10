/***************************************************************************************
 *  nvr_nop8012_proto.h — NOP 8012 事件中心协议编解码(纯函数,无 socket,host 可单测)。
 *
 *  协议见 nop_doc/camera/8012事件中心.md:
 *    cmdHeader = 40 字节,10×uint32_t,小端(Little-Endian),magic 0x1AA1B22C。
 *    XVR→Camera:LOGIN(cmd=0,payload=64B stMSG_LOGIN)、HEARTBEAT(cmd=1,30s)。
 *    Camera→XVR:ACK_OK(2)/ACK_FAIL(3)/SEND_MSG(4,携 msgType + 可选 JPEG)。
 ***************************************************************************************/
#ifndef NVR_NOP8012_PROTO_H
#define NVR_NOP8012_PROTO_H

#include <stdint.h>
/* 纯字节层:不依赖 detect 词汇。msgType→detect 归类见 app/event/nvr_evt_types.h。 */

#ifdef __cplusplus
extern "C" {
#endif

#define NVR_N8012_MAGIC       0x1AA1B22Cu
#define NVR_N8012_HDR_SIZE    40
#define NVR_N8012_LOGIN_SIZE  64          /* char user[24] + char pass[40] */
#define NVR_N8012_MAX_DATA    (10u * 1024 * 1024)   /* 数据包上限,防错位/溢出 */

enum {
    N8012_CMD_LOGIN     = 0,
    N8012_CMD_HEARTBEAT = 1,
    N8012_CMD_ACK_OK    = 2,
    N8012_CMD_ACK_FAIL  = 3,
    N8012_CMD_SEND_MSG  = 4,
    N8012_CMD_CLOSE     = 5
};

typedef struct {
    uint32_t magic, version, data_size, cmd, timestamp,
             msg_type, extend_flag, rsv0, rsv1, rsv2;
} nvr_n8012_hdr_t;

/* 打包 40B 头到 buf(至少 40B)。返回写入字节数(=40)。 */
int  nvr_n8012_pack_header(uint8_t *buf, uint32_t cmd, uint32_t data_size,
                           uint32_t msg_type, uint32_t extend_flag);
/* 打包登录包(头40 + 64) 到 buf(≥104B)。返回总字节(=104)。 */
int  nvr_n8012_pack_login(uint8_t *buf, const char *user, const char *pass);
/* 打包心跳(=40B)。返回 40。 */
int  nvr_n8012_pack_heartbeat(uint8_t *buf);
/* 解析 40B 头。magic 不符返回 -1;成功返回 0 并填 *out。 */
int  nvr_n8012_parse_header(const uint8_t *buf40, nvr_n8012_hdr_t *out);
/* msgType → detect 类型的映射不在本层:见 app/event/nvr_evt_types.h 的单一事件类型表
 * (nvr_evt_detect_from_msgtype)。本层只解析出裸 msg_type,由事件层统一归类。 */
/* 从 SEND_MSG 负载抽取第一段 JPEG。extend_flag=1: 整段即 JPEG;=2: 找 dataType==1 单元。
   成功置 jpeg/len(指向 payload 内部,借用)返回 0;无图返回 -1。 */
int  nvr_n8012_extract_jpeg(uint32_t extend_flag, const uint8_t *payload,
                            uint32_t payload_len, const uint8_t **jpeg, uint32_t *len);

#ifdef __cplusplus
}
#endif
#endif /* NVR_NOP8012_PROTO_H */
