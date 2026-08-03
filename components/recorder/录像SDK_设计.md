# NT12-32 录像系统 SDK 设计（裸盘 + 多盘负载均衡 + AES-256-CTR）

> 目标设备：**NT12-32** / SoC **NV98633 = NA51090**（ARM Cortex-A53×4，Linux 4.19，Buildroot）
> 架构来源：逆向自 ENVR12-16(MOLCHIP) 的 **JFSDK 裸盘存储引擎**，为 NA51090 平台重做一套干净实现。
> 本轮交付：**设计文档为主**；实现按**垂直切片**推进（写入 → 索引 → 回放跑通，单盘先通，再扩多盘/加密）。
> 相关逆向文档：[录像系统深挖_JFSDK.md](录像系统深挖_JFSDK.md)、[录像与存储.md](录像与存储.md)、[裸盘取录像_验证.md](裸盘取录像_验证.md)。

---

## 0. 命名与定位

- SDK 代号：**RSDK**（Recording SDK），C 语言，静态库 `librsdk.a`，对齐同仓 `nop_client` 的 `NOP_API` 导出风格与 CMake 工程（注意：`nop_client` 是**网络协议客户端 SDK**，与本录像引擎无耦合）。
- 导出前缀：`rsdk_`；不透明句柄 `rsdk_*_t`；返回码 `rsdk_err_t`。
- 定位：**设备侧**录像引擎——直接读写裸块设备 `/dev/sda`…`/dev/sdX`，自管分配 / 索引 / 加密 / 多盘均衡，**不经内核文件系统**（对标 `NK_JFSDK_FILESYS_IsNoFs`）。

### 为什么裸盘 no-FS（相对 NT12-32 现有 4×ext4）

| 维度 | ext4 方案（现状） | 裸盘 no-FS（本设计） |
|------|------------------|---------------------|
| 写放大 | 目录/inode/日志元数据反复写 | 预分配定长块，顺序直写，几乎零元数据 I/O |
| 掉电一致性 | 依赖 fsck / journal | 自管理，超级块双写 + 块头自描述，秒级恢复 |
| 循环覆盖 | 删文件→碎片→再分配 | 块级环形覆盖，零碎片 |
| 加密 | 需 dm-crypt/fscrypt 全盘层 | 引擎内**按段** AES-CTR，粒度可控、可随机 seek |
| 多盘均衡 | 上层手工调度 | 引擎内 BALANCE 统一分配 |

---

## 1. 分层架构

对标 JFSDK 的 7 层（见逆向文档 §3），重做为 8 个模块：

```
┌────────────────────────────────────────────────────────────────┐
│  应用（LocalHMI / NVR_16CH / 网络回放 / 导出）                    │
├────────────────────────────────────────────────────────────────┤
│  rsdk_search / rsdk_play     检索 + PTS seek + 逐帧取流           │
│  rsdk_backup                 导出 fMP4（本轮范围外，仅留接口）    │
├────────────────────────────────────────────────────────────────┤
│  rsdk_rec                    录像写入（RecSegment + 帧记录 + 元数据）│
│  rsdk_index                  时间/通道 → 段+位置 索引              │
├────────────────────────────────────────────────────────────────┤
│  rsdk_balance                多盘负载均衡（盘组 / 选盘 / 环形覆盖）  │
│  rsdk_crypto                 AES-256-CTR（/dev/crypto mc-aes + 软实现）│
├────────────────────────────────────────────────────────────────┤
│  rsdk_storgedev              盘注册 / 格式化 / 超级块 / 保留区布局   │
│  rsdk_rawdev                 裸块设备抽象（open/pread/pwrite 扇区）  │
└────────────────────────────────────────────────────────────────┘
```

模块 ↔ JFSDK 符号对照：

| RSDK 模块 | 对应 JFSDK 符号 |
|-----------|-----------------|
| rsdk_rawdev / rsdk_storgedev | `FILESYS IsNoFs/GetDevCapacity/ReadSysTab`、`STORGEDEV_Init/GetFormatProgress/GetSysVer` |
| rsdk_rec | `REC_FILE_init/WriteRefresh/NextRecFile/AppendRecType/Read-WriteRecPrivateData` |
| rsdk_index | `INDEX_WriteRecIndex/GetRecFile/GetEarliestDate/GetOverwriteStatus` |
| rsdk_search / rsdk_play | `SEARCH_QueryRec/PLAY_SeekPts/PeepNextFrameSpecifyChn` |
| rsdk_balance | `JFSMD MGNT_RequestDevById/BALANCE_RequestRecDev/GetDevFsInfo` |
| rsdk_crypto | `FILESYS_GetEncryption` + `/dev/crypto`(mc-aes) |
| rsdk_backup | `BACKUP_RecToFile/fmp4_writer_init_segment` |

---

## 2. 盘上布局（Raw Layout）

每块盘统一布局。所有多字节字段 **小端**，扇区大小 `SEC=512B`。

```
偏移(扇区)         区域                        说明
──────────────────────────────────────────────────────────────────────
0                 保护 MBR + 标签             单分区(type 0xDA 非文件系统)+签名55AA,
                                              防止 OS 误挂载/误格式化
1        ┐
         │  SuperBlock (主, 1 扇区)           magic/版本/盘UUID/盘组/角色/几何/加密描述符
2        │  SuperBlock (备, 1 扇区)           主备双写,掉电原子切换
3..66    │  SysTab (≤4096B ⇒ ≤8 扇区, 主)     盘布局表:各区起始扇区/容量/格式版本/序列号
67..130  │  SysTab (备)                       断言 sizeof(stSYSTABLE)==读回长度(对齐JFSDK)
131..N   │  AllocBitmap (块位图, 主+备)        每 chunk 1 bit:空闲/占用;循环覆盖写指针
N..M     │  IndexRegion (录像索引区)          按通道的 RecSegment 索引条目(见 §6)
──────── ┴  ↑ 以上合称「保留区 RSV」≈ 128MB(对齐JFSDK的sda1 JFSRSV≈2GB可调) ────
DATA_START        DataRegion (数据区)          定长 CHUNK 环形覆盖(chunk 池)
...               chunk[0], chunk[1], ...       — MetaRegion 从本池按参数动态划拨(见下)
盘尾              ← 写指针到盘尾回绕到 DATA_START 覆盖最旧
```

**MetaRegion 不是固定预留,而是热插拔的逻辑配额**：只有当固件参数 `RSDK_FEAT_METADATA` 开启时,才从 chunk 池划出一段配额(≈盘容量 0.5%,可配)标记为「元数据 chunk」存 Tier-2 逐帧轨迹;参数关闭则归还给数据区。**开/关无需重新格式化**(边界是逻辑的、记在 SuperBlock,不是物理固定分区)。详见 §12 + §13。

> 事件级元数据(Tier-1)另存**嵌入式 DB(SQLite)**,按时间戳索引、与事件绑定,不落数据区 —— 完整方案见 **§12 元数据子系统**。

关键常量（可在格式化时写入 SuperBlock，运行期从盘读取）：

| 常量 | 默认 | 说明 |
|------|------|------|
| `RSDK_SEC` | 512 | 扇区字节 |
| 保留区 RSV | **按盘容量自动算** | 超级块+SysTab+位图+索引；随 chunk_count 线性增长（500GB≈32MiB / 2TB≈120MiB / 18TB≈1GiB），非固定值，公式见 [盘上格式冻结_v1.md](盘上格式冻结_v1.md) §1.1 |
| `RSDK_CHUNK_BYTES` | 8 MiB（可 auto） | 数据区分配单元（预分配、环形覆盖粒度）；超大盘可自动升 16/32MiB |
| `RSDK_FRAME_ALIGN` | 128 | 帧记录头按 128B 对齐（DMA/块写友好，对齐 pteeeld） |
| `RSDK_SB_MAGIC` | `"RSDK01\0\0"` | 超级块魔术 |

### 2.1 SuperBlock（1 扇区）

```c
typedef struct {                 /* 512B, 小端 */
    char     magic[8];           /* "RSDK01\0\0" */
    uint32_t version;            /* 格式版本, 当前 1 */
    uint32_t sb_crc32;           /* 除本字段外全体 CRC32 */
    uint8_t  disk_uuid[16];      /* 本盘唯一ID(格式化时随机) */
    uint8_t  group_uuid[16];     /* 盘组ID(同组多盘负载均衡) */
    uint16_t group_disk_index;   /* 本盘在组内序号 0..N-1 */
    uint16_t group_disk_count;   /* 组内盘总数(冗余,以Storage为准) */
    uint64_t total_sectors;      /* 盘总扇区 */
    uint64_t rsv_start_sec;      /* 保留区起始扇区(=1) */
    uint64_t data_start_sec;     /* 数据区起始扇区 */
    uint64_t chunk_sectors;      /* 每chunk扇区数 = CHUNK_BYTES/SEC */
    uint64_t chunk_count;        /* 数据区chunk总数 */
    uint64_t write_ptr_chunk;    /* 环形写指针(当前分配到的chunk下标) */
    uint64_t seq_epoch;          /* 覆盖轮次(每绕盘一圈+1,用于新旧判定) */
    /* —— 特性状态(格式化时按固件统一config写入, 见§13) —— */
    uint32_t feature_mask;       /* 本盘启用的特性位 FEAT_*(与固件config核对) */
    uint32_t _rsvf;
    /* —— 元数据配额(仅 FEAT_METADATA 开时非0; 从chunk池划拨, 见§12) —— */
    uint64_t meta_start_chunk;   /* MetaRegion 起始chunk下标 */
    uint64_t meta_chunk_count;   /* 元数据配额chunk数; 0=未启用元数据 */
    /* —— 加密描述符 —— */
    uint8_t  enc_algo;           /* 0=none 1=AES-256-CTR */
    uint8_t  enc_flags;          /* bit0: 索引区也加密 */
    uint8_t  _rsv0[6];
    uint8_t  kdf_salt[16];       /* 由 SN 派生 KEK 用的盐 */
    uint8_t  wrapped_dek[48];    /* KEK 包裹的数据密钥(AES-KW或XOR-MAC封装) */
    uint8_t  dek_kcv[8];         /* 密钥校验值(解密自检,不泄露密钥) */
    uint8_t  _pad[/*补足512*/ ];
} rsdk_superblock_t;
```

> 块位图每条目含 **chunk 类型位**（`数据 / 元数据 / 空闲`）；元数据配额即位图里被标为「元数据」的一段 chunk。特性字段在格式化时**恒定存在**（未启用则为 0），故后续变更配置无需改盘上布局版本 —— 详见 §13「能力预留 vs 空间预留」。

### 2.2 SysTab（≤4096B，主备）

对齐 JFSDK 断言 `(4096) > nSysTabSize` 与 `sizeof(stNK_JFSDK_SYSTABLE)==nRet`：**定长、一次读满、≤4096B**。

```c
typedef struct {                 /* 定长, ≤4096B */
    uint32_t magic;              /* 'STAB' */
    uint32_t sys_version;        /* GetSysVer/UpdateSysVerInfo */
    uint32_t systab_size;        /* 本结构体实际大小(自校验) */
    uint32_t crc32;
    uint64_t format_time;        /* 格式化 epoch */
    uint64_t bitmap_start_sec;   /* 块位图起始扇区 */
    uint64_t bitmap_sectors;
    uint64_t index_start_sec;    /* 索引区起始扇区 */
    uint64_t index_sectors;
    uint32_t index_slot_size;    /* 每条索引条目字节 */
    uint32_t index_slot_count;   /* 索引条目容量 */
    /* 每通道当前写位置缓存(加速续写), 16/32路 */
    struct { uint64_t cur_chunk; uint32_t cur_off; uint32_t seg_seq; } chn[32];
    uint8_t  _rsv[/*补足*/];
} rsdk_systab_t;
```

主备双写 + CRC：读时取 CRC 正确且 `sys_version` 较大的一份；写时先写备、fsync、再写主，保证任一时刻至少一份完整（掉电安全）。

---

## 3. 数据区：Chunk / RecSegment / 帧记录

三层容器（对齐逆向所见 "REC 段 + 帧头 + Annex-B"）：

```
DataRegion
 └─ Chunk (8MB 定长, 环形覆盖的最小分配单元, 属于某盘)
     └─ RecSegment (一路连续录像片段, 落在1个或跨多个chunk)
         └─ FrameRecord[]  (每GOP/帧一条, 128B对齐)
             ├─ 帧记录头 (专有, 见下)
             ├─ (可选)PES 包头
             └─ 媒体负载 (H.264/H.265 Annex-B, AES-CTR 加密)
```

### 3.1 帧记录头（Frame Record Header，对齐 `pteeeld`）

```c
typedef struct {                 /* 64B 头, 之后补0到128B对齐 */
    char     magic[8];           /* "rsdkfrm\0" (对标 pteeeld) */
    uint16_t chn;                /* 通道 */
    uint8_t  stream;             /* 0=主码流 1=子码流 2=音频 */
    uint8_t  codec;              /* 0=H264 1=H265 2=AAC ... */
    uint8_t  frame_type;         /* 0=I/IDR 1=P 2=B 3=audio */
    uint8_t  enc;                /* 0=明文 1=AES-256-CTR */
    uint16_t _rsv0;
    uint32_t payload_len;        /* 加密后负载字节(=明文长, CTR等长) */
    uint32_t seg_id;             /* 所属 RecSegment id */
    uint32_t frame_seq;          /* 段内帧序号(IV派生用) */
    uint64_t pts;                /* 显示时间戳(90kHz或us) */
    uint64_t wall_time;          /* 墙钟 epoch(秒), 掉索引也能自解析 */
    uint8_t  iv_nonce[8];        /* 每帧CTR起始counter高位(见 §5) */
    uint32_t hdr_crc32;          /* 头CRC(不含本字段) */
} rsdk_frame_hdr_t;
```

设计要点（承接逆向结论）：
- **自描述**：即使索引损坏，扫盘按 `magic` + `wall_time` 也能重建（对齐"单块可自解析出时间"）。
- **128B 对齐**：便于块设备/DMA 顺序写。
- **头明文、负载加密**：头保持明文以支持扫盘恢复；负载 AES-CTR。可选 `enc_flags` 连头一起加密（更强，牺牲可恢复性）。

### 3.2 RecSegment 私有元数据（对齐 `RecPrivateMetadata`）

一个段 = 一路一次连续录像。段的私有元数据记录：通道、事件类型（常录/移动/人形/车…）、时间跨度、帧数、总字节、盘内起止位置。它同时是**索引条目**的内容（见 §6）。事件类型编码（对齐逆向 §6）：

```c
enum rsdk_rectype {
    RSDK_REC_CONTINUOUS = 0,  /* 常录 */
    RSDK_REC_MOTION     = 1,
    RSDK_REC_HUMAN      = 2,
    RSDK_REC_FACE       = 3,
    RSDK_REC_VEHICLE    = 4,
    RSDK_REC_LINECROSS  = 5,
    RSDK_REC_INTRUSION  = 6,
    RSDK_REC_ANIMAL     = 7,
    RSDK_REC_PACKAGE    = 8,
    RSDK_REC_DOORBELL   = 9,
};
```

---

## 4. 多盘负载均衡（rsdk_balance，对标 JFSMD_BALANCE）

### 4.1 盘组模型
- 同一 `group_uuid` 的多块盘组成一个**盘组**，对上层呈现为一个连续录像空间。
- 每盘独立布局（各有自己的 SuperBlock/SysTab/位图/索引/数据区）；**索引按盘本地**存储，检索时跨盘归并（段记录带 `disk_index`）。

### 4.2 选盘策略（写入时 `rsdk_balance_request_rec_dev`）
每当一路需要新 chunk 时，从盘组里选一块盘分配，目标是**吞吐均衡 + 磨损均衡**：

```
score(disk) = w1 * norm(inflight_bytes_per_s)      // 当前写带宽越低越优先
            + w2 * norm(pending_alloc)              // 待分配队列越短越优先
            + w3 * norm(overwrite_pressure)         // 越接近回绕(覆盖最旧)越降权
选择 argmin(score) 且 健康(SMART ok / 未只读)。
默认 w1=0.5 w2=0.3 w3=0.2。
```

- **通道亲和**：同一通道的连续段尽量落同盘，减少回放跨盘 seek；仅当该盘压力显著高于组均值（>阈值）才迁移到别盘 —— 兼顾均衡与回放局部性。
- **热插拔**：盘掉线→从组内活动集合移除，写入自动转到其余盘；回放时该盘的段标记为暂不可读。盘重新上线→校验 `group_uuid` 后重新纳入。

### 4.3 环形覆盖（对标 `INDEX_GetOverwriteStatus`）
- 每盘 `write_ptr_chunk` 顺序推进，到 `chunk_count` 回绕到 0，`seq_epoch++`。
- 覆盖前：读旧 chunk 内各段的索引条目 → 从索引区删除/标记失效（避免检索命中已被覆盖的数据）。
- 满盘策略（对齐 `HDDFullAct`）：`OVERWRITE`（覆盖最旧，默认）/ `STOP`（停录）。

---

## 5. 加密方案：AES-256-CTR（rsdk_crypto）

### 5.1 为什么 CTR
- **可随机 seek 解密**：回放要跳到任意段/任意帧，CTR 模式下第 `i` 个 16B 块的密钥流独立可算 —— 直接从目标偏移解密，无需从段头回退（CBC 做不到）。
- **等长**：密文与明文等长，帧记录 `payload_len` 不变，盘布局不受影响。
- **硬件加速**：SoC 有 `mc-aes`（hw_info 中断 271）+ `/dev/crypto`(cryptodev) → 硬件 AES-CTR；无 cryptodev 时软件 fallback（可复用 app 内 wolfSSL）。

### 5.2 密钥层级
```
设备根密钥 KEK  ← KDF(设备SN/工厂区 + SuperBlock.kdf_salt)      // 每设备唯一,不落盘
       │  解包
数据密钥 DEK (AES-256, 每盘随机)  ← SuperBlock.wrapped_dek 解包    // 真正加密录像
       │  校验
       └─ dek_kcv 自检(用DEK加密全0块取前8字节比对)
```
- DEK 随机生成、KEK 包裹后存 `wrapped_dek`；换盘/迁移不泄露明文密钥。
- KEK 绑定设备身份（SN/MAC 在工厂区，见 [身份与工厂区_SN_MAC.md](身份与工厂区_SN_MAC.md)）：**盘离开本机无法解密**。

### 5.3 IV / Counter 派生（关键）
每帧独立 IV，保证同一密钥下 counter 唯一（CTR 安全性硬要求：nonce 不复用）：

```
128-bit CTR block = | seg_id(32) | frame_seq(32) | iv_nonce(64) 的高位 |
                    低位为「帧内 16B 块计数」= byte_offset / 16
```
即：`IV_base = AES_CTR_iv(seg_id, frame_seq)`；解密帧内偏移 `off` 处：`counter = IV_base + off/16`。
- `seg_id`+`frame_seq` 全局单调，杜绝 nonce 复用；`iv_nonce[8]` 存于帧头（明文），回放直接取用。
- 段头/索引默认明文（便于扫盘恢复）；`enc_flags.bit0` 置位时索引区也用同 DEK、以 `index_slot` 号派生 IV 加密。

### 5.4 crypto 接口
```c
typedef struct rsdk_crypto rsdk_crypto_t;
rsdk_err_t rsdk_crypto_open (rsdk_crypto_t **out, const uint8_t dek[32], int prefer_hw);
/* 就地 XOR 密钥流; off 为该帧负载内字节偏移, seek 回放时可非0 */
rsdk_err_t rsdk_crypto_xcrypt(rsdk_crypto_t *c, uint32_t seg_id, uint32_t frame_seq,
                              uint64_t off, uint8_t *buf, size_t len);
void       rsdk_crypto_close(rsdk_crypto_t *c);
```
硬件路径：`/dev/crypto` → `CIOCGSESSION{ cipher=CRYPTO_AES_CTR, key=DEK }` → `CIOCCRYPT`。失败回退软件 AES-CTR。

### 5.5 加密开关：由固件统一配置控制（§13）

是否加密由**固件的统一配置文件**在**编译前**设定，经**特性控制模块 `rsdk_feature`** 统一读取（见 §13），而非散落的 CMake `-D` 或运行时按盘协商。`rsdk_rec_write_frame` 写每帧前查询该特性开关：
```c
if (rsdk_feature_on(RSDK_FEAT_ENCRYPTION)) {
    rsdk_crypto_xcrypt(c, seg_id, frame_seq, off, buf, len);
    hdr.enc = 1;
} else {
    hdr.enc = 0;                    /* 参数关: 负载直写明文 */
}
```
- **逐帧标记**：帧头 `rsdk_frame_hdr_t.enc` 记录该帧是否加密 → 同盘「明文历史帧 + 加密新帧」共存，回放按帧头逐帧决定是否解密，参数切换即时生效、平滑过渡。
- **盘上状态**：`SuperBlock.enc_algo` 记录该盘**当前**加密态（首次开启加密时写入并生成/包裹 DEK）；参数与盘状态解耦：

| 参数(`FEAT_ENCRYPTION`) | 帧头(`enc`) | 回放行为 |
|-------------------------|-------------|----------|
| 开 | 1 | 解密后输出 |
| 开/关 | 0 | 明文直接输出 |
| 关 | 1(历史帧) | 仍解密（DEK 仍在 SuperBlock），只是新帧不再加密 |

- 密钥仅在 `FEAT_ENCRYPTION` 首次开启时生成；关闭参数不销毁 DEK（否则历史加密帧无法回放），仅停止对新帧加密。若需彻底关闭并抹密钥，走独立的「重置加密」运维操作。

---

## 6. 索引 / 检索（rsdk_index / rsdk_search）

### 6.1 索引条目（RecSegment 记录，扁平二进制，定长）
对齐逆向的 SQL schema（§5）但落成**裸盘定长条目**（无 SQLite 依赖，纯二进制、可 O(1) 追加、可重建）：

```c
typedef struct {                 /* 定长索引槽, 建议 64B */
    uint32_t seg_id;             /* 段ID(单调) */
    uint16_t chn;                /* 通道 */
    uint8_t  rectype;            /* enum rsdk_rectype */
    uint8_t  flags;              /* bit0 有效 bit1 事件 bit2 未闭合(录制中) */
    uint32_t start_time;         /* 起 epoch */
    uint32_t end_time;           /* 止 epoch(未闭合=0xffffffff, 对齐逆向的ff标记) */
    uint32_t frame_count;
    uint64_t total_bytes;
    uint16_t start_disk;         /* 起始盘 index(跨盘均衡) */
    uint16_t end_disk;
    uint64_t start_chunk;        /* 起始 chunk 下标 */
    uint32_t start_off;          /* chunk 内偏移 */
    uint64_t end_chunk;
    uint32_t end_off;
    uint32_t crc32;
} rsdk_index_slot_t;
```

- **组织**：索引区按通道分段的环形槽数组；`INDEX_WriteRecIndex` 追加/更新，`flags` 标记有效性；覆盖旧 chunk 时把对应段 `flags` 清有效。
- **重建**（对齐 `recindex rebuilt.`）：索引损坏时扫数据区 `rsdk_frame_hdr_t.magic`+`wall_time` 重建。
- **最早日期** `rsdk_index_earliest_date`：时间轴左界（对齐 `GetEarliestDate`）。

### 6.2 检索 / 回放流程（对齐逆向 §7 + 垂直切片）
```
1. rsdk_search_query(t0,t1, chn_mask, rectype_mask) → 命中段列表(跨盘归并,按start_time排序)
2. 选中段 → rsdk_play_open(seg) → 定位 (disk, start_chunk, start_off)
3. rsdk_play_seek_pts(pts) → 段内按帧头 pts 二分/线性定位到目标 FrameRecord
4. 逐帧 rsdk_play_next_frame():
     读帧头(明文) → 读负载 → rsdk_crypto_xcrypt 解密 → 得 Annex-B → 回调送解码器
5. 回放输出 VOU/RTSP; 导出走 rsdk_backup(→ fMP4, 本轮范围外)
```

---

## 7. 垂直切片（本轮实现目标）：写入 → 索引 → 回放

**单盘、单/多通道、AES-256-CTR** 端到端跑通，验证盘上格式与解密回放正确。

### 7.1 写入路径
```c
rsdk_dev_t   *dev;  rsdk_storgedev_open("/dev/sda", &dev);      // 未格式化则 rsdk_format()
rsdk_writer_t *w;   rsdk_rec_open(dev, /*chn*/0, RSDK_REC_CONTINUOUS, &w);
for (每帧) {
    rsdk_frame_t f = { .codec=H265, .type=IDR/P, .pts=..., .wall=..., .data=annexb, .len=n };
    rsdk_rec_write_frame(w, &f);        // 内含: 取/换chunk → 填帧头 → CTR加密负载 → 顺序写
}
rsdk_rec_close(w);                      // 闭合段 → INDEX_WriteRecIndex(end_time/frame_count/...)
```
写入内部：`rsdk_balance_request_rec_dev`（单盘退化为固定本盘）→ chunk 分配/续写 → `rsdk_crypto_xcrypt` → `rsdk_rawdev_pwrite`（对齐 128B）→ 周期 `REC_WriteRefresh`（刷 SysTab 通道写位置 + 位图）。

### 7.2 索引路径
- 段起：分配 `seg_id`，写一条 `flags=未闭合, end_time=0xffffffff` 的槽。
- 段止：回填 `end_time/frame_count/total_bytes/end_chunk/end_off`，`flags|=有效`。
- 崩溃恢复：`flags=未闭合` 的段在启动时按数据区最后有效帧头回填闭合（对齐逆向 `ffffffff` 语义）。

### 7.3 回放路径
- `rsdk_search_query` → `rsdk_play_open` → `rsdk_play_seek_pts` → `rsdk_play_next_frame`（解密）→ 落地成 `.h265` / 送 ffmpeg 验证（对齐 [裸盘取录像_验证.md](裸盘取录像_验证.md) 的验证方法）。
- **验收**：写入 N 秒 H.265 → 断电重挂 → 检索该时段 → 解密回放帧数/PTS 连续、ffmpeg 正常解码首帧与第 3 秒帧、OSD 时间推进。

### 7.4 本轮范围外（后续切片）
多盘均衡的跨盘归并回放、fMP4 导出（`rsdk_backup`）、抓拍图 `PIC_Write`、SMART/健康调度、密钥轮换。

---

## 8. 对外 API 概览（头文件契约草案）

```c
/* ---- 生命周期 / 盘 ---- */
rsdk_err_t rsdk_storgedev_open  (const char *devpath, rsdk_dev_t **out);
rsdk_err_t rsdk_format          (rsdk_dev_t *dev, const rsdk_format_opt_t *opt);
int        rsdk_format_progress (rsdk_dev_t *dev);          /* 0..100 */
rsdk_err_t rsdk_dev_info        (rsdk_dev_t *dev, rsdk_dev_info_t *info);
void       rsdk_storgedev_close (rsdk_dev_t *dev);

/* ---- 盘组 / 均衡 ---- */
rsdk_err_t rsdk_group_open      (const char *const *devpaths, int n, rsdk_group_t **out);
rsdk_err_t rsdk_balance_request_rec_dev(rsdk_group_t *g, int chn, rsdk_dev_t **picked);

/* ---- 录像写入 ---- */
rsdk_err_t rsdk_rec_open        (rsdk_group_t *g, int chn, int rectype, rsdk_writer_t **out);
rsdk_err_t rsdk_rec_write_frame (rsdk_writer_t *w, const rsdk_frame_t *f);
rsdk_err_t rsdk_rec_change_type (rsdk_writer_t *w, int rectype);   /* 常录↔事件 */
rsdk_err_t rsdk_rec_close       (rsdk_writer_t *w);

/* ---- 检索 / 回放 ---- */
rsdk_err_t rsdk_search_query    (rsdk_group_t *g, const rsdk_query_t *q, rsdk_seglist_t **out);
rsdk_err_t rsdk_index_earliest  (rsdk_group_t *g, uint32_t *epoch);
rsdk_err_t rsdk_play_open       (rsdk_group_t *g, const rsdk_segref_t *seg, rsdk_player_t **out);
rsdk_err_t rsdk_play_seek_pts   (rsdk_player_t *p, uint64_t pts);
rsdk_err_t rsdk_play_next_frame (rsdk_player_t *p, rsdk_frame_t *out); /* 已解密 */
void       rsdk_play_close      (rsdk_player_t *p);

/* ---- 元数据: 完整JSON文档存取, SDK不解析内容(见 §12) ---- */
/* key 由调用方给(ts/chn/event_id/doc_type/seg_ref); json 原样存 */
rsdk_err_t rsdk_meta_put   (rsdk_group_t *g, const rsdk_meta_key_t *key,
                            const void *json, size_t len, uint64_t *doc_id);
/* 按时间戳/通道/事件/类型检索; 返回 {key + 完整JSON}, 上层自己 parse */
rsdk_err_t rsdk_meta_query (rsdk_group_t *g, const rsdk_meta_query_t *q, rsdk_metadoc_list_t **out);
rsdk_err_t rsdk_meta_get   (rsdk_group_t *g, uint64_t doc_id, rsdk_metadoc_t *out);
/* 深查(可选): 传 JSON 路径+匹配值, 内部走 SQLite json_extract, 无需上层预解析 */
rsdk_err_t rsdk_meta_query_json(rsdk_group_t *g, const rsdk_meta_query_t *q,
                            const char *json_path, const char *match, rsdk_metadoc_list_t **out);

/* ---- 导出(范围外, 占位) ---- */
rsdk_err_t rsdk_backup_to_fmp4  (rsdk_group_t *g, const rsdk_segref_t *seg, const char *out_path);
```

返回码：`RSDK_OK=0`、`RSDK_E_IO`、`RSDK_E_FORMAT`、`RSDK_E_CRYPTO`、`RSDK_E_NOSPACE`、`RSDK_E_NOTFOUND`、`RSDK_E_CORRUPT`、`RSDK_E_BUSY`、`RSDK_E_PARAM`。

---

## 9. 工程结构（对齐 nop_client）

```
rsdk/
 ├─ CMakeLists.txt          静态库 librsdk.a (C99); aarch64 交叉工具链
 │                          选项 -DRSDK_ENABLE_ENCRYPTION=ON/OFF 控制是否加密(§5.5)
 ├─ include/rsdk.h          对外总头(§8 契约)
 ├─ src/
 │   ├─ rsdk_rawdev.c/.h        裸设备 pread/pwrite(O_DIRECT 对齐)
 │   ├─ rsdk_storgedev.c/.h     格式化 / 超级块 / SysTab / 位图
 │   ├─ rsdk_crypto.c/.h        AES-256-CTR(/dev/crypto + 软fallback)
 │   ├─ rsdk_rec.c/.h           写入器 / 帧记录 / chunk 分配
 │   ├─ rsdk_index.c/.h         索引区读写 / 重建
 │   ├─ rsdk_search.c/.h        检索
 │   ├─ rsdk_play.c/.h          回放 / seek / 逐帧解密
 │   ├─ rsdk_balance.c/.h       多盘均衡(单盘退化)
 │   └─ rsdk_util.c/.h          CRC32 / 小端读写 / 对齐
 ├─ tools/
 │   ├─ rsdk_format_cli.c       格式化裸盘
 │   ├─ rsdk_write_demo.c       喂 .h265 → 写盘(垂直切片写)
 │   └─ rsdk_play_demo.c        检索+解密回放 → 出 .h265(垂直切片读)
 └─ test/                      掉电一致性 / 覆盖 / 解密正确性
```

---

## 10. 与逆向结论的一致性核对

| 逆向要点 | 本设计落点 |
|----------|-----------|
| `IsNoFs` 裸盘直写 | rsdk_rawdev 直接 pread/pwrite `/dev/sdX` |
| SysTab ≤4096B、定长一次读满 | §2.2 `rsdk_systab_t` ≤4096B + 断言 |
| `nFSysTabStartSec > nRootStartSec` | §2 保留区 SuperBlock 后即 SysTab |
| 双盘 `JFSMD_BALANCE` | §4 盘组 + 选盘评分 + 环形覆盖 |
| REC 段 + 448B 封包 + 假帧头 | §3 FrameRecord 头(128B对齐)+ 负载(去掉假起始码坑) |
| 加密开关可选、mc-aes 硬件 | §5 AES-256-CTR + /dev/crypto,`enc_algo` 开关 |
| INDEX 按通道、可重建、覆盖状态 | §6 索引槽 + `flags` + 扫盘重建 + 环形覆盖 |
| 事件类型写私有元数据 | §3.2 rectype 入索引槽 + 段元数据 |
| 导出 fMP4 | §8 `rsdk_backup_to_fmp4`(后续) |

---

## 11. 下一步（供确认后进入实现）

1. 冻结 §2/§3/§6 的**盘上二进制布局**（一旦有数据写盘，改布局需迁移）。
2. 实现垂直切片三件套：`rsdk_format_cli` / `rsdk_write_demo` / `rsdk_play_demo`，在 `/dev/sda`（或回环 `losetup` 的镜像文件）跑通「写→断电→检索→解密回放」。
3. 用 [裸盘取录像_验证.md](裸盘取录像_验证.md) 同款 ffmpeg 流程验收解密后码流。
4. 再扩：多盘均衡跨盘归并 → fMP4 导出 → 抓拍/事件抠图。

---

## 12. 元数据子系统（Metadata / Event Store）—— 行业实践方案

> 需求：**预留元数据** → 元数据**直接存库、按时间戳**索引 → **与事件绑定**。本节先给行业实践方案（设计），实现随后。

### 12.1 行业实践：为什么「媒体裸盘 + 元数据入库」分层

主流 VMS/NVR（Milestone、Bosch、Avigilon Appearance Search、Axis、ONVIF Profile M）都把**视频码流**与**元数据**分两套存储：

| | 视频码流 | 元数据 / 事件 |
|--|---------|--------------|
| 访问模式 | 顺序大块写、按时间顺序读 | 高频小记录写、**任意维度检索**(时间/类型/目标属性) |
| 存储 | 定长块 / 裸盘（吞吐优先） | **数据库**（可查询优先） |
| 本设计 | §2 DataRegion 裸盘 no-FS | **Tier-1 SQLite + Tier-2 裸盘 blob** |

**结论**：媒体走裸盘（已定），元数据「直接存库」是正解——因为智能检索（forensic/smart search：按人/车/颜色/车牌/区域/时间过滤）需要富查询，裸盘扁平索引扛不动。逆向也印证 app.out 内含 SQLite 建表语句（[录像系统深挖_JFSDK.md](录像系统深挖_JFSDK.md) §5）。

### 12.2 文档存储模型（完整 JSON 结构体，SDK 不解析内容）

**核心约定**：元数据由**设备产出 → 上层转成 JSON 结构体 → 交给 SDK 原样入库**。SDK **不理解、不拆解 JSON 的语义内容**，只把**完整结构体**当作不透明文档存下，并按**调用方给出的少量索引键**（时间戳 + 通道 + 事件绑定 + 文档类型）建索引。这是 **schema-less 文档库** 模型（对标 MongoDB/ES doc、SQLite JSON1），好处：设备元数据格式演进时**无需改库、无需迁移**。

```
设备 → (上层)JSON 结构体 ─┬─ 索引键(调用方显式给, SDK不从JSON里猜): ts / chn / event_id / doc_type / seg_ref
                          └─ 完整JSON文档(SDK原样存, 不解析): {...设备结构体全文...}
存：索引行(小, 可按时间戳/事件检索)  +  完整JSON(可 inline 或落 MetaRegion)
取：按时间戳/事件命中索引 → 取回完整JSON原样返回给上层(上层自己解析)
```

- **写时不理解**：`rsdk_meta_put(key, json, len)` —— `key` 里的 ts/chn/event_id/doc_type 由调用方提供，`json` 整块 verbatim 存储（可选 gzip + AES）。SDK 不 parse。
- **与事件绑定**：`key.event_id` + `key.seg_ref` 把该文档绑到同一事件与视频帧位置。
- **按时间戳**：`ts` 为主索引，驱动时间轴/区间检索。
- **智能检索**（可选、按需）：需要「按车牌/人脸/类别」深查时，**不在写入侧预解析**，而在**读取侧**用 SQLite **JSON1 `json_extract()`** 直接查存下的 JSON；高频字段可加**表达式索引**加速——依然「存时不理解，查时才按需看」。

### 12.3 索引表 Schema（SQLite；payload=完整 JSON）

```sql
-- 元数据文档索引表: 只存"检索键 + 完整JSON", 不做语义列
CREATE TABLE meta_doc (
  id        INTEGER PRIMARY KEY AUTOINCREMENT,
  ts        INTEGER NOT NULL,      -- epoch(秒), 主检索键(按时间戳); 调用方给
  ts_ms     INTEGER DEFAULT 0,     -- 毫秒
  chn       INTEGER,               -- 通道(绑定/过滤); 调用方给
  event_id  INTEGER,               -- 事件绑定(与视频/抓拍同一event); 调用方给
  doc_type  INTEGER,               -- 文档类型标签(event/ai_frame/lpr/...); 调用方给, SDK不据此解析
  -- 视频绑定(可选, 命中后跳回放):
  seg_disk  INTEGER, seg_chunk INTEGER, seg_off INTEGER, seg_pts INTEGER,
  -- 完整结构体存放:
  storage   INTEGER DEFAULT 0,     -- 0=inline(存json列) 1=blobref(存MetaRegion)
  enc       INTEGER DEFAULT 0,     -- 0=明文 1=AES-256-CTR  gz bit: 0x2=gzip
  json_len  INTEGER,               -- 原始JSON字节
  json      BLOB,                  -- storage=0: 完整JSON原样(可gz/enc)
  meta_disk INTEGER, meta_off INTEGER, meta_len INTEGER  -- storage=1: 指向MetaRegion完整JSON
);
CREATE INDEX ix_meta_ts ON meta_doc(ts);              -- 按时间戳
CREATE INDEX ix_meta_ce ON meta_doc(chn, event_id);   -- 按通道/事件绑定
CREATE INDEX ix_meta_et ON meta_doc(event_id);
-- (可选) 高频深查字段的表达式索引, 读取侧 json_extract, 不改写入路径:
-- CREATE INDEX ix_meta_plate ON meta_doc(json_extract(json,'$.objects[0].plate'));
```

- **不做规范化列**：没有 `plate/face/cls` 这类语义列——它们都在**完整 JSON 里**，需要才 `json_extract` 取。
- **inline vs blobref**：小文档（每事件级，几百字节~KB）直接 inline 到 `json` 列；大/高频文档（逐帧 AI，25fps×16ch）`storage=1` 存 **MetaRegion**，索引行只留指针 → 避免撑爆 DB。阈值可配（如 >4KB 落 MetaRegion）。

### 12.4 MetaRegion：完整 JSON 文档容器（时间桶）

裸盘 MetaRegion 内按**时间桶**（如每小时一桶）环形排布文档；桶内每条 = **文档头 + 完整 JSON 字节**（可 gzip + AES-CTR）：

```c
typedef struct {                 /* MetaRegion 内单条文档头, 32B, 之后紧跟 json_len 字节 */
    char     magic[4];           /* "MDOC" */
    uint8_t  enc;                /* bit0 AES-CTR  bit1 gzip */
    uint8_t  _rsv0[3];
    uint64_t event_id;           /* 绑定事件(=meta_doc.event_id) */
    uint32_t ts;                 /* epoch, 与索引行一致 */
    uint32_t doc_type;           /* 文档类型标签 */
    uint32_t json_len;           /* 完整JSON字节(压缩前) */
    uint32_t crc32;              /* 文档头+负载 CRC */
    /* 之后: uint8_t json[stored_len]  —— 设备结构体全文, SDK原样存 */
} rsdk_mdoc_hdr_t;
```

- **时间桶** = 「按时间戳」范围顺序读 + **整桶丢弃**（retention O(1) 回收，随视频覆盖，见 §12.6）。
- `meta_doc.storage=1` 时，`meta_disk/meta_off` 指向此文档头；`meta_len = sizeof(hdr)+stored_len`。
- 加密：复用 §5 的 DEK，以 (bucket_id, doc_off) 派生 CTR IV；gzip 在加密前做（JSON 冗余，压缩收益大）。

### 12.5 物理落位与「预留」

| 存储 | 落位（推荐） | 理由 |
|------|-------------|------|
| Tier-1 SQLite (`event.db`+WAL) | 小型元数据 FS 区（flash `/config` 或盘上预留的 f2fs/ext4 小分区，几百 MB） | SQLite 需随机写文件；置于耐磨小 FS，主媒体仍裸盘 |
| Tier-2 MetaRegion blob | **§2 盘上 MetaRegion（配置开启后于格式化时划拨 ~0.5% 盘容量）** | 高频、大体量、与视频同盘同 retention |

> **预留是配置驱动的**：只有固件统一配置里 `metadata=on`（§13）时，格式化才把 MetaRegion 配额从 chunk 池划出并建 Event DB；`off` 则完全不占空间、元数据模块不参与。见 §13。

> **两种预留取向**（供选）：
> **A. 混合（推荐，行业主流）**：SQLite 放小 FS 区 + 逐帧轨迹放裸盘 MetaRegion。工程量小、查询强。
> **B. 纯 no-FS**：连事件表也用自研裸盘定长记录 + 内建时间索引（不引 SQLite），与 §6 风格统一、无 FS 依赖，但要自己实现二级索引与富查询，工程量大。
> 逆向证据（app.out 有 SQLite）+ 行业实践 → **默认 A**。

### 12.6 retention 与视频耦合（关键）

元数据的生命周期**必须跟随视频覆盖**，否则 DB 无限膨胀、且检索命中已被覆盖的录像：

- 视频 chunk 被环形覆盖时 → 该时段 `event.state=purged` 或直接删行；对应 Tier-2 时间桶整桶回收。
- 反向保证：检索只返回 `state!=purged` 且视频仍在盘的事件（seg_ref 仍有效）。
- 可选**分级保留**：事件行（小）保留更久，逐帧轨迹（大）先回收 —— 满足「事件记录还在、但精细轨迹已随视频过期」的行业常见策略。

### 12.7 写入 / 检索时序

```
写入(上层把设备元数据转成JSON后):
  rsdk_meta_put(key={ts,chn,event_id,doc_type,seg_ref}, json, len)
     → 建 meta_doc 索引行 + 完整JSON原样存(inline 或 MetaRegion) → 返回 doc id
  (SDK 全程不 parse json; 索引键全部来自 key, 不从 json 里猜)
检索(App/GUI):
  rsdk_meta_query({t0,t1, chn?, event_id?, doc_type?}) → 命中的 {key + 完整JSON} 列表
  选中 → 上层自己 parse JSON 取需要的字段; seg_ref 跳视频回放(§6.2)
  深查(可选) → SQL 里 json_extract(json,'$.objects[*].plate') 直接查, 写入侧无需预解析
```

与录像写入解耦：元数据由 AI(kflow_ai_net/NPU) 产出、上层转 JSON 后经本 API 落库；文档与视频通过 `key.seg_ref` + 时间戳对齐（同一墙钟/PTS 时钟）。

### 12.8 与逆向/现有结构的一致性

| 逆向所见 | 本方案落点 |
|----------|-----------|
| app.out 含 SQLite 建表/插入 | `meta_doc` 索引表用 SQLite（§12.3） |
| LOG_LIST 事件主日志(定长记录) | 收敛为 `meta_doc` 索引行（按时间戳可查） |
| `.IMG` 抓拍(P抠图/H/M场景) | 抓拍位置写在完整 JSON 内 / 或 `doc_type=snap` 另一文档 |
| 事件标记录像段 + 4/TIME_LINE | `meta_doc.seg_*`（disk/chunk/off/pts）绑视频 |
| 事件类型 RecType/私有元数据 | `doc_type` 标签 + JSON 内容（SDK 不解析） |

### 12.9 下一步（元数据切片，接在 §11 之后）

1. 冻结 §12.3 `meta_doc` schema 与 §12.4 `rsdk_mdoc_hdr_t` 文档头格式。
2. 决定 §12.5 落位取向（默认 A 混合）+ inline↔blobref 阈值。
3. 垂直切片扩展：`rsdk_meta_put(完整JSON)` → 按时间戳/事件检索取回原样 JSON → `json_extract` 深查一例 → seg_ref 跳回放，端到端验证「原样存取 + 事件/视频绑定」。

---

## 13. 特性控制模块 + 统一配置文件（Feature Control）

> 固件使用本 SDK；**编译前**在**一个统一的配置文件**里配好各特性，据此编译固件。固件内由**特性控制模块 `rsdk_feature`** 统一读取该配置，决定每个功能是否启用（加密、元数据、多盘均衡、导出…）。不是散落的 CMake `-D`，而是**单一配置源 + 单一控制模块**。

### 13.1 统一配置文件（单一真源）

一个文件 `rsdk_features.conf`（KV/INI 风格，人可读、可进版本管理），描述整机录像特性与调参：

```ini
# ---- rsdk_features.conf  (固件编译前配置, 单一真源) ----
[features]
encryption      = on          # 录像负载 AES-256-CTR(§5); off=明文
encryption_hw   = on          # 优先 /dev/crypto mc-aes, 失败软件AES
metadata        = on          # 元数据子系统(§12); off=不建库/不预留MetaRegion
multidisk_balance = on        # 多盘负载均衡(§4); off=单盘
backup_fmp4     = off         # fMP4 导出(§8, 后续)

[layout]
chunk_mib       = auto        # 数据区 chunk 大小; auto=按盘容量(≤4TB→8, ≤16TB→16, 更大→32)
slots_per_chunk = 4           # 索引密度因子(决定索引区大小)
rsv_mib         = 0           # 保留区下限兜底; 0=全自动(按盘容量算, 见 冻结v1 §1.1)
meta_ratio_pct  = 0.5         # metadata=on 时 MetaRegion 占盘容量比例
hdd_full        = overwrite   # overwrite | stop

[crypto]
cipher          = aes-256-ctr # 目前仅此一种
kek_source      = device_sn   # KEK 派生源(工厂区SN)

[metadata]
store           = hybrid      # hybrid(§12.5-A, 默认) | nofs(§12.5-B)
obj_sample_hz   = 10          # 逐帧轨迹采样率上限
```

**构建流程**：
```
rsdk_features.conf ──[codegen 脚本]──> include/rsdk_config.h ──> 编译进 librsdk.a / 固件
```
`rsdk_config.h` 是**生成物**（禁止手改），把配置固化为编译期常量表：
```c
/* 由 rsdk_features.conf 生成, 勿手改 */
#define RSDK_CFG_ENCRYPTION        1
#define RSDK_CFG_ENCRYPTION_HW     1
#define RSDK_CFG_METADATA          1
#define RSDK_CFG_MULTIDISK_BALANCE 1
#define RSDK_CFG_BACKUP_FMP4       0
#define RSDK_CFG_CHUNK_MIB         8
#define RSDK_CFG_META_RATIO_PCT    5   /* x10 定点 */
/* ... */
```

### 13.2 特性控制模块 `rsdk_feature`

**唯一**读取配置、对全 SDK 提供统一开关查询的模块。所有可选功能只认它，不各自读配置：

```c
typedef enum {
    RSDK_FEAT_ENCRYPTION = 0,
    RSDK_FEAT_METADATA,
    RSDK_FEAT_MULTIDISK_BALANCE,
    RSDK_FEAT_BACKUP_FMP4,
    RSDK_FEAT__MAX
} rsdk_feature_id_t;

/* 编译期常量, 关闭的特性可被死代码消除(零开销/不链入) */
int         rsdk_feature_on   (rsdk_feature_id_t f);        /* 1/0 */
int         rsdk_cfg_int      (const char *key);            /* 取整型调参 */
const char* rsdk_cfg_str      (const char *key);            /* 取字符串调参 */
uint32_t    rsdk_feature_mask (void);                       /* 打包成 FEAT_* 位掩码 */
```
- 由于 `rsdk_feature_on()` 展开为 `RSDK_CFG_*` 常量，**关闭的特性其代码路径被编译器裁掉**（既满足「统一控制」又零运行时开销）。
- 同时可运行期查询（日志/上报 App「本机启用了哪些特性」）：`rsdk_feature_mask()` → 写入 `SuperBlock.feature_mask`。

### 13.3 「能力预留」vs「空间预留」（回答：改配置要不要重新格式化）

| | 何时发生 | 内容 | 换配置是否需重格式化 |
|--|---------|------|---------------------|
| **能力预留** | 盘上格式**天生具备**（格式版本恒定带这些字段） | SuperBlock `feature_mask`/`meta_*` 字段、块位图 chunk 类型位 | 不需要（字段一直在，未启用则为 0） |
| **空间预留** | **格式化时**按当时配置划拨 | MetaRegion chunk 配额、Event DB | 见下 |

- **配置 → 盘状态**：格式化时 `rsdk_feature_mask()` 写入 `SuperBlock.feature_mask`；`metadata=on` 则划 MetaRegion 配额、建 Event DB，`off` 则 `meta_chunk_count=0`。
- **换固件配置后**（同一盘上格式版本）：
  - `metadata` off→on：**在线**从 chunk 池划配额 + 建库 + 更新 SuperBlock，**不需重新格式化**（盘满则回收最旧录像换空间，视频保留时长略减）。
  - `metadata` on→off：归还配额、停用库，不需重新格式化。
  - `encryption` off→on：SuperBlock 生成/包裹 DEK，新帧起加密（帧头 `enc` 位逐帧标记），历史明文帧照常回放，**不需重新格式化**。
- **唯一需迁移的情形**：盘由**旧格式版本**（无这些字段）格式化 → 一次性升级/重排（跨 `version` 才发生）。

> 与 §5.5、§2、§12.5 一致：**能力**在格式里恒定预留，**空间**按统一配置在启用时划拨；同格式版本内切配置免格式化。

### 13.4 配置一致性校验（开机）

开机 `rsdk_storgedev_open` 时，比对**固件配置** `rsdk_feature_mask()` 与**盘上** `SuperBlock.feature_mask`：

| 固件 config | 盘 feature_mask | 处理 |
|-------------|-----------------|------|
| 一致 | 一致 | 正常 |
| metadata 固件 on / 盘 off | — | 在线划拨 MetaRegion + 建库（免格式化） |
| metadata 固件 off / 盘 on | — | 停用元数据写入；旧库只读保留或回收 |
| encryption 固件 on / 盘 off | — | 新帧起加密；历史明文帧共存 |
| encryption 固件 off / 盘 on | — | 新帧不加密；历史加密帧仍用盘上 DEK 解密回放 |

即固件配置为「目标态」，开机将盘平滑收敛到该态，**除跨格式版本外都不需要重新格式化**。
