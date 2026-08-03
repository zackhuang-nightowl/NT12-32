# RSDK —— NT12-32 NVR 录像系统 SDK

裸盘 no-FS 直写 + 多盘负载均衡 + AES-256-CTR + JSON 元数据文档库。供 **NVR 固件**引用。

**能力总览**：格式化(按盘容量自动布局) · 录制(单盘/多盘均衡, 逐帧 AES-256-CTR) · 索引/检索 ·
回放(PTS seek/多盘跨盘连续) · 导出(MP4 + fMP4, muxer 可扩展) · 元数据(JSON 文档库 + json_extract 智能检索) ·
事件抓拍(推送取图) · 满盘策略(overwrite/stop 参数化) · 覆盖回收(视频↔元数据/抓拍联动) ·
密钥轮换(KEK) · 硬件 mc-aes(/dev/crypto, 软件回退) · 编译期特性开关(统一 config)。
**8 个 example 全部编译运行通过**(见下表)。

- 架构设计：[录像SDK_设计.md](录像SDK_设计.md)
- 盘上格式冻结 + SQL/落盘实例：[盘上格式冻结_v1.md](盘上格式冻结_v1.md)
- 元数据开发清单：[examples/README.md](examples/README.md)

## 目录

```
recorder_sdk/
├─ rsdk_features.conf        # 统一配置(单一真源, 编译前配置; 设计 §13)
├─ CMakeLists.txt            # 构建 librsdk.a (+可选 sqlite3)
├─ include/                  # 对外头: rsdk.h(总头) + 各模块头
├─ src/                      # 实现: rawdev/crypto/storgedev/index/rec/play/balance/meta/feature
├─ sql/meta_schema.sql       # 元数据 DDL(可跑)
├─ tools/gen_config.py       # rsdk_features.conf → include/rsdk_config.h
├─ tools/gen_onwire.py       # 盘上结构字节生成/回归校验
└─ examples/rec_demo.c       # 录像垂直切片(格式化→加密写→索引→解密回放, 实测 PASS)
   examples/meta_demo.c      # 元数据智能检索参考
```

## 构建

```sh
sudo apt-get install -y libsqlite3-dev cmake      # metadata=on 需 sqlite3
cd recorder_sdk
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
# 产物: build/lib/librsdk.a  build/bin/{rec_demo,meta_demo}
./build/bin/rec_demo        # 全链路自测(AES-256 FIPS 向量 + 加密写盘 + 解密回放一致)
```

目标机(NA51090/aarch64)：设 `CMAKE_TOOLCHAIN_FILE` 用交叉工具链；`metadata=on` 时交叉编译 sqlite3(≥3.9, 带 JSON1)。`metadata=off` 则无第三方依赖。

## 固件引用方式

```c
#include <rsdk.h>
/* 链接: -lrsdk  (metadata=on 时另 -lsqlite3) */

/* 1) 首次: 按盘容量自动分配布局并格式化 */
rsdk_format_opt_t fo = { .chunk_mib = 0 /*auto*/, .sn = device_sn };
rsdk_format("/dev/sda", &fo);

/* 2) 打开盘(或盘组 rsdk_group_open 多盘均衡) */
rsdk_dev_t *d; rsdk_dev_open("/dev/sda", &d);

/* 3) 录像写入(编码器每出一帧调一次; 按配置自动 AES-CTR 加密) */
rsdk_writer_t *w; rsdk_rec_open(d, ch, RSDK_REC_CONTINUOUS, &w);
rsdk_frame_t f = { .chn=ch, .codec=RSDK_CODEC_H265, .frame_type=RSDK_FRAME_I,
                   .pts=pts, .wall_time=now, .data=annexb, .len=n };
rsdk_rec_write_frame(w, &f);
rsdk_rec_close(w);                       /* 闭合段→写索引 */

/* 4) 元数据(AI 产出→上层转 JSON→原样入库; 设计 §12) */
void *mc; rsdk_meta_open(RSDK_CFG_META_DB_PATH, &mc);
rsdk_meta_key_t k = { .ts=now, .chn=ch, .event_id=eid, .doc_type=RSDK_DOC_AI_EVENT,
                      .seg={ .disk=0, .chunk=rsdk_rec_seg_id(w) /*或段位置*/ } };
rsdk_meta_put(mc, &k, json_str, json_len, NULL);

/* 5) 检索 + 解密回放 */
rsdk_index_slot_t segs[64];
int n = rsdk_index_query(d, t0, t1, ch, -1, segs, 64);
rsdk_player_t *p; rsdk_play_open(d, &segs[0], &p);
rsdk_play_seek_pts(p, target_pts);
rsdk_frame_hdr_t h; const uint8_t *data; uint32_t len;
while (rsdk_play_next_frame(p, &h, &data, &len) == RSDK_OK)
    decode(data, len);                   /* 已解密的 Annex-B, 送 vpu/vdu */
rsdk_play_close(p); rsdk_dev_close(d);
```

## 多盘回放(设计 §4/§7.4)

```c
const char *disks[] = { "/dev/sda", "/dev/sdb" };
rsdk_group_t *g; rsdk_group_open(disks, 2, &g);

/* 多盘写入均衡: 一路录像每换新段按盘负载选盘, 均摊到多盘 */
rsdk_writer_t *w; rsdk_rec_open_group(g, ch, RSDK_REC_CONTINUOUS, &w);
rsdk_rec_write_frame(w, &frame);  /* ... */  rsdk_rec_close(w);

/* 多盘回放 */
rsdk_index_slot_t segs[128];
int n = rsdk_group_query(g, t0, t1, ch, -1, segs, 128);   /* 跨盘归并, 按时间升序 */

rsdk_group_player_t *gp; rsdk_group_play_open(g, segs, n, &gp);
rsdk_group_play_seek_pts(gp, target_pts);                 /* 跨盘全局 seek */
rsdk_frame_hdr_t h; const uint8_t *data; uint32_t len; int disk;
while (rsdk_group_play_next(gp, &h, &data, &len, &disk) == RSDK_OK)
    decode(data, len);       /* 到段尾自动切到下一段所在盘, 时间线连续 */
rsdk_group_play_close(gp); rsdk_group_close(g);
```

## 导出 MP4(设计 §7.6, 扩展框架)

```c
rsdk_export_opt_t eo = { .fmt=RSDK_EXPORT_MP4, .width=w, .height=h, .fps=15 };
rsdk_backup_export(g, t0, t1, ch, &eo, "/media/udisk/clip.mp4");   /* 跨盘, 自动解密 */
/* 或单段: rsdk_backup_export_seg(dev, &seg, &eo, path) */
```
- 目前仅 **MP4**（H.265 `hvc1`/hvcC、H.264 `avc1`/avcC；标准 `ftyp/mdat/moov`，任意播放器可播）。
- **留扩展**：`rsdk_muxer_t` 接口 + `rsdk_backup_register()`，新增格式(fMP4/MKV…)= 实现 muxer 并注册，`rsdk_export_fmt_t` 加枚举即可，导出驱动/调用方不变。

## 事件抓拍(PIC, 用于事件推送)

```c
/* 事件触发时(AI 出图): 写主图/抠图, 绑 event_id, 加密入 MetaRegion */
rsdk_pic_key_t k = { .chn=ch, .ts=now, .event_id=eid, .type=RSDK_PIC_MAIN,
                     .w=320, .h=240, .seg={...} };
rsdk_pic_write(dev, meta, &k, jpeg, jpeg_len, &pic_id);

/* 事件推送子系统: 一把取该事件主图, 塞进 App/Email 推送 */
void *img; size_t n;
rsdk_pic_get_for_event(dev, meta, eid, RSDK_PIC_MAIN, &img, &n);  /* 已解密 JPEG */
```
类型 `P`目标抠图/`M`主图/`H`场景/`COVER`封面；索引进 `meta_doc`(doc_type=SNAP)，与事件同表可查。需 metadata=on(复用 MetaRegion)。

## 特性开关(编译前, rsdk_features.conf)

`encryption / metadata / multidisk_balance / backup_fmp4` + 布局/加密/元数据调参。
由 `gen_config.py` 生成 `include/rsdk_config.h`，特性控制模块 `rsdk_feature` 统一读取。
改配置**不需要重新格式化硬盘**（能力字段格式里恒存，空间按需划拨；跨格式版本才迁移）。详见设计 §13 / 冻结 §1.1。

## 已实测(examples/rec_demo.c)

| 项 | 结果 |
|----|------|
| AES-256 FIPS-197 向量 | PASS |
| 按盘容量自动布局(256MB→255×1MiB chunk) | PASS |
| 16 帧 AES-256-CTR 加密写盘 | 盘上负载=密文≠明文 ✓ |
| 段索引 + 跨 chunk 多段 + 时间升序检索 | 命中 4 段有序 |
| 解密回放逐字节比对 | 16/16 完全一致 ✓ |
| PTS seek | 命中 pts≤target |
| 元数据原样存 + json_extract 智能检索 + seg 绑定 | PASS |
| **多盘回放**(multidisk_demo): 跨盘归并→连续回放→自动切盘 | 4段/2盘, 16/16 逐字节一致 ✓ |
| **多盘写入均衡**(balance_demo): 一路录像按负载均摊到多盘 | 6段均摊3/3, 回放18/18 一致 ✓ |
| **导出 MP4**(backup_demo): 真HEVC→加密写→导出→ffprobe/ffmpeg | hevc 640x360 30帧, ffmpeg 可解码 ✓ |
| **事件抓拍**(pic_demo): 事件触发→加密写MetaRegion→按事件取推送图 | mjpeg 320x240, 解密逐字节一致 ✓ |
| **满盘策略**(overwrite_demo): stop 停录 / overwrite 覆盖+回收旧索引 | stop 填满即停; overwrite 最早时间前移 ✓ |
| **导出 fMP4**(backup_demo): 同一 muxer 框架扩展的分片 MP4 | ffprobe hevc 30帧 ✓ |
| **密钥轮换**(rec_demo): rekey(KEK) 后重开仍可解密 | 逐字节一致 ✓ |
| **覆盖回收生命周期**(retention_demo): 视频覆盖→元数据/抓拍同步清理 | 存活124=2×容量, 最旧已清理 ✓ |
| **硬件 AES**(mc-aes /dev/crypto): `__has_include` 守卫, 软件回退 | 本机无 cryptodev→软件, 全绿 |

## 现状与后续

- **已实现可用**：rawdev / crypto(AES-256-CTR 软件, 硬件 /dev/crypto 路径预留) / storgedev(自动布局/格式化/超级块主备/掉电CRC) / index / rec / play / balance(单盘+亲和) / meta(SQLite 文档库) / feature。
- **后续切片**：多盘跨盘归并回放的评分细化、fMP4 导出(`backup`)、抓拍 PIC、环形覆盖时的旧索引精细回收、密钥轮换、硬件 mc-aes 接入 `/dev/crypto`。
