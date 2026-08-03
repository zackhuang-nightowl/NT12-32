#!/usr/bin/env python3
# 生成 RSDK 盘上结构的真实字节(struct.pack + 真实CRC32), 打印带注解 hexdump。
import struct, zlib

SEC = 512

def hexdump(b, base=0, ann=None):
    ann = ann or {}
    out = []
    for i in range(0, len(b), 16):
        row = b[i:i+16]
        hexs = ' '.join(f'{x:02x}' for x in row)
        hexs = hexs[:23] + ' ' + hexs[23:] if len(row) > 8 else hexs
        asc = ''.join(chr(x) if 32 <= x < 127 else '.' for x in row)
        line = f'{base+i:06x}  {hexs:<48}  |{asc}|'
        if (base+i) in ann:
            line += '   <- ' + ann[base+i]
        out.append(line)
    return '\n'.join(out)

# ---------------- SuperBlock (512B) ----------------
# 2TB 盘示例
total_sectors   = 3907029168
rsv_start_sec   = 1
chunk_sectors   = 16384                 # 8MiB
data_start_sec  = 262144                # 128MiB 保留区末尾, chunk 对齐
chunk_count     = (total_sectors - data_start_sec) // chunk_sectors
meta_chunk_count= int(chunk_count * 0.005)         # 0.5%
meta_start_chunk= chunk_count - meta_chunk_count    # 尾部划拨
write_ptr_chunk = 12043                  # 已写到某处(示例)
seq_epoch       = 1
feature_mask    = 0x07                    # bit0 enc | bit1 meta | bit2 balance
enc_algo        = 1                       # AES-256-CTR
enc_flags       = 0
disk_uuid       = bytes.fromhex('4e5431322d33320a11223344556677a1')
group_uuid      = bytes.fromhex('67727031202020202020202020202001')
kdf_salt        = bytes.fromhex('a1b2c3d4e5f6071829304152637485a0')
wrapped_dek     = bytes(range(0x40, 0x70))          # 48B 示例包裹密钥
dek_kcv         = bytes.fromhex('9f3c1188aa004277')

def pack_sb(crc):
    b = struct.pack('<8sII16s16sHHQQQQQQQII QQ BB6s16s48s8s'.replace(' ',''),
        b'RSDK01\x00\x00', 1, crc, disk_uuid, group_uuid, 0, 2,
        total_sectors, rsv_start_sec, data_start_sec, chunk_sectors, chunk_count,
        write_ptr_chunk, seq_epoch, feature_mask, 0, meta_start_chunk, meta_chunk_count,
        enc_algo, enc_flags, b'\x00'*6, kdf_salt, wrapped_dek, dek_kcv)
    return b + b'\x00' * (SEC - len(b))

sb0  = pack_sb(0)
crc  = zlib.crc32(sb0) & 0xffffffff
sb   = pack_sb(crc)
assert len(sb) == 512

print('#'*72)
print(f'# SuperBlock  (data_start_sec={data_start_sec}, chunk_count={chunk_count},')
print(f'#              meta_start_chunk={meta_start_chunk}, meta_chunk_count={meta_chunk_count},')
print(f'#              sb_crc32=0x{crc:08x})')
print('#'*72)
ann = {0x00:'magic "RSDK01"',0x08:'version=1',0x0c:'sb_crc32',0x10:'disk_uuid',
       0x20:'group_uuid',0x30:'disk_index=0 count=2',0x34:'total_sectors',
       0x5c:'write_ptr_chunk / seq_epoch',0x6c:'feature_mask=0x07',
       0x74:'meta_start_chunk / meta_chunk_count',0x84:'enc_algo=1 enc_flags=0',
       0x8c:'kdf_salt',0x9c:'wrapped_dek(48)',0xcc:'dek_kcv'}
print(hexdump(sb[:0xE0], 0, ann))
print('   ... 0x0E0..0x200 全 0 (pad)')

# ---------------- FrameRecord Header (64B, 记录对齐128B) ----------------
magic=b'rsdkfrm\x00'; chn=13; stream=0; codec=1; ftype=0; enc=1
payload_len=0x0001A2C0            # 107200B ~ 4K IDR
seg_id=0x105; frame_seq=0
pts=0x0000000000015F90           # 90kHz 示例
wall=0x6a63b9ac                  # 2026-07-24 19:14:52 (与逆向文档同款时间戳)
iv_nonce=bytes.fromhex('11a2035e04017c05')
def pack_fh(crc):
    return struct.pack('<8sHBBBBHIIIQQ8sI8s',
        magic,chn,stream,codec,ftype,enc,0,payload_len,seg_id,frame_seq,
        pts,wall,iv_nonce,crc,b'\x00'*8)
fh0=pack_fh(0); fcrc=zlib.crc32(fh0)&0xffffffff; fh=pack_fh(fcrc)
assert len(fh)==64
print('\n'+'#'*72)
print(f'# FrameRecord Header (IDR, chn=13, H265, enc=1, hdr_crc32=0x{fcrc:08x})')
print(f'#   之后紧跟 {payload_len} 字节 AES-256-CTR 加密负载, 记录补0到128B边界')
print('#'*72)
fann={0x00:'magic "rsdkfrm"',0x08:'chn=13 stream=0 codec=1(H265) ftype=0(IDR) enc=1',
      0x10:'payload_len=0x1A2C0',0x14:'seg_id=0x105 frame_seq=0',
      0x1c:'pts',0x24:'wall_time=0x6a63b9ac (2026-07-24 19:14:52)',
      0x2c:'iv_nonce(8)',0x34:'hdr_crc32'}
print(hexdump(fh,0,fann))

# ---------------- Index Slot (64B) ----------------
i_seg=0x105; i_chn=13; i_rectype=2; i_flags=0x03   # bit0 valid | bit1 event(human)
i_start=0x6a63b9ac; i_end=0x6a63ba14                # 104s
i_fcount=1560; i_bytes=0x000000009A32C1
i_sdisk=0; i_edisk=0; i_schunk=12040; i_soff=0x180; i_echunk=12053; i_eoff=0x1C4000
def pack_ix(crc):
    return struct.pack('<IHBBIIIQHHQIQII',
        i_seg,i_chn,i_rectype,i_flags,i_start,i_end,i_fcount,i_bytes,
        i_sdisk,i_edisk,i_schunk,i_soff,i_echunk,i_eoff,crc)+b'\x00'*4
ix0=pack_ix(0); icrc=zlib.crc32(ix0)&0xffffffff; ix=pack_ix(icrc)
assert len(ix)==64
print('\n'+'#'*72)
print(f'# Index Slot (seg 0x105, chn13, HUMAN 事件, 有效, crc=0x{icrc:08x})')
print('#'*72)
iann={0x00:'seg_id=0x105',0x04:'chn13 rectype=2(HUMAN) flags=0x03',
      0x08:'start=0x6a63b9ac end=0x6a63ba14 (104s)',0x10:'frame_count=1560',
      0x14:'total_bytes',0x1c:'start_disk=0 end_disk=0',
      0x20:'start_chunk=12040 start_off=0x180',0x2c:'end_chunk=12053 end_off',
      0x38:'crc32'}
print(hexdump(ix,0,iann))

# ---------------- MetaRegion 文档: rsdk_mdoc_hdr_t(32B) + 完整JSON(原样存) ----------------
event_id=0x0000000000002A11; mdoc_ts=0x6a63b9ac; doc_type=1   # 1=ai_event
# 上层从设备取来转成的完整JSON结构体, SDK原样存(此处 enc=0 明文, 便于 json_extract 深查)
mjson=(b'{"ts":1784486092,"chn":13,"event":"human","rule":5,'
       b'"objects":[{"id":7,"cls":"person","conf":0.92,'
       b'"bbox":[0.12,0.08,0.26,0.64],"color":"red","plate":null}],'
       b'"seg":{"chunk":12040,"off":384,"pts":90000}}')
def pack_mdoc(crc):
    return struct.pack('<4sB3sQIIII', b'MDOC',0,b'\x00'*3,event_id,mdoc_ts,doc_type,len(mjson),crc)
mh0=pack_mdoc(0); mcrc=zlib.crc32(mh0+mjson)&0xffffffff; mh=pack_mdoc(mcrc)
assert len(mh)==32
blob=mh+mjson
print('\n'+'#'*72)
print(f'# MetaRegion 文档 (MDOC, event 0x2A11, json_len={len(mjson)}, 明文, crc=0x{mcrc:08x})')
print('#   SDK 原样存下面这段完整JSON, 写时不解析; 深查时读取侧 json_extract')
print('#'*72)
mann={0x00:'magic "MDOC" | enc=0',0x08:'event_id=0x2A11',0x10:'ts=0x6a63b9ac',
      0x14:'doc_type=1(ai_event)',0x18:'json_len',0x1c:'crc32',0x20:'—— 完整JSON原样(设备结构体) ——'}
print(hexdump(blob,0,mann))

# ---------------- 分区按盘容量自动分配 (冻结 §1.1) ----------------
def layout(total_bytes, chunk_mib=8, slots_per_chunk=4, meta_pct=0.5):
    chunk = chunk_mib * 1024 * 1024
    cc = total_bytes // chunk
    for _ in range(3):                                  # rsv<->cc 循环依赖, 迭代收敛
        bitmap = ((cc * 2 + 7) // 8) * 2                # 2bit/chunk, 主备
        index  = cc * slots_per_chunk * 64 * 2          # 64B/slot, 主备
        rsv = 2*SEC + 2*8*SEC + bitmap + index          # SB主备 + SysTab主备 + 位图 + 索引
        rsv = ((rsv + chunk - 1) // chunk) * chunk      # 对齐 chunk
        cc = (total_bytes - rsv) // chunk
    return chunk, cc, rsv, bitmap, index, round(cc * meta_pct / 100)

def _fmt(b):
    for u in ['B','KiB','MiB','GiB','TiB']:
        if b < 1024: return f'{b:.1f}{u}'
        b /= 1024
    return f'{b:.1f}PiB'

print('\n' + '#'*72)
print('# 分区按盘容量自动分配 (chunk=8MiB, slots_per_chunk=4, meta=0.5%)')
print('#'*72)
print(f"{'盘容量':>7} {'chunk_count':>12} {'RSV':>9} {'位图':>9} {'索引':>9} {'Meta':>9} {'录像可用':>10}")
for tb, lb in [(500*10**9,'500GB'),(2*10**12,'2TB'),(4*10**12,'4TB'),(8*10**12,'8TB'),(18*10**12,'18TB')]:
    ch,cc,rsv,bm,ix,mc = layout(tb)
    print(f"{lb:>7} {cc:>12,} {_fmt(rsv):>9} {_fmt(bm):>9} {_fmt(ix):>9} {_fmt(mc*ch):>9} {_fmt((cc-mc)*ch):>10}")
