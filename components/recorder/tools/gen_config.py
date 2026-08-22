#!/usr/bin/env python3
# gen_config.py —— 由 rsdk_features.conf 生成 include/rsdk_config.h (设计 §13)
# 用法: python3 tools/gen_config.py rsdk_features.conf include/rsdk_config.h
import sys, re

def parse(path):
    cfg, sec = {}, None
    for ln in open(path, encoding='utf-8'):
        ln = ln.split('#', 1)[0].strip()
        if not ln: continue
        m = re.match(r'\[(\w+)\]', ln)
        if m: sec = m.group(1); continue
        if '=' in ln and sec:
            k, v = [x.strip() for x in ln.split('=', 1)]
            cfg[f'{sec}.{k}'] = v
    return cfg

def onoff(v): return 1 if str(v).lower() in ('on','true','1','yes') else 0

def main():
    conf = sys.argv[1] if len(sys.argv) > 1 else 'rsdk_features.conf'
    out  = sys.argv[2] if len(sys.argv) > 2 else 'include/rsdk_config.h'
    c = parse(conf)
    chunk = c.get('layout.chunk_mib', 'auto')
    chunk_mib = 0 if str(chunk).lower() == 'auto' else int(chunk)
    ratio10 = int(round(float(c.get('layout.meta_ratio_pct', '0.5')) * 10))
    L = [
      '/* 生成物: 由 rsdk_features.conf 生成, 请勿手改 (tools/gen_config.py) */',
      '#ifndef RSDK_CONFIG_H', '#define RSDK_CONFIG_H',
      f'#define RSDK_CFG_ENCRYPTION        {onoff(c.get("features.encryption","off"))}',
      f'#define RSDK_CFG_ENCRYPTION_HW     {onoff(c.get("features.encryption_hw","on"))}',
      f'#define RSDK_CFG_METADATA          {onoff(c.get("features.metadata","off"))}',
      f'#define RSDK_CFG_MULTIDISK_BALANCE {onoff(c.get("features.multidisk_balance","off"))}',
      f'#define RSDK_CFG_BACKUP_FMP4       {onoff(c.get("features.backup_fmp4","off"))}',
      f'#define RSDK_CFG_CHUNK_MIB         {chunk_mib}',
      f'#define RSDK_CFG_SLOTS_PER_CHUNK   {int(c.get("layout.slots_per_chunk","4"))}',
      f'#define RSDK_CFG_EVT_SLOTS_PER_CHUNK {int(c.get("layout.evt_slots_per_chunk","64"))}',
      f'#define RSDK_CFG_META_RATIO_PCT10  {ratio10}',
      f'#define RSDK_CFG_HDD_FULL          {1 if str(c.get("layout.hdd_full","overwrite")).lower()=="stop" else 0}',
      f'#define RSDK_CFG_META_DB_PATH      "{c.get("metadata.db_path","/config/meta.db")}"',
      '#endif',
    ]
    open(out, 'w', encoding='utf-8').write('\n'.join(L) + '\n')
    print(f'[gen_config] {conf} -> {out} (metadata={onoff(c.get("features.metadata","off"))}, '
          f'encryption={onoff(c.get("features.encryption","off"))})')

if __name__ == '__main__':
    main()
