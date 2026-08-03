# RSDK 元数据 —— 开发直接可用清单

本目录是**可直接编译运行**的元数据子系统参考实现与用法（设计 §12 / 冻结 §5-6）。

## 文件

| 文件 | 作用 |
|------|------|
| `../rsdk_features.conf` | 统一配置(含 `[metadata]`：db_path / inline_max_kb / gzip / **index_paths** / fts) |
| `../include/rsdk_meta.h` | 元数据 API + 结构体(rsdk_meta_key_t / query / metadoc / mdoc_hdr 32B) |
| `../sql/meta_schema.sql` | 可跑 DDL：`meta_doc` 表 + 基础索引 + **表达式索引**(index_paths) + 可选 FTS5 |
| `meta_demo.c` | 参考程序：put(原样存JSON) → 时间检索 → json_extract 智能检索 → 索引命中证明 → 取回 → FTS5 → retention |

## 构建 & 运行

```sh
sudo apt-get install -y libsqlite3-dev          # 主机自测依赖
cd recorder_sdk
gcc -O2 -Wall -Iinclude examples/meta_demo.c -lsqlite3 -o meta_demo
./meta_demo                                      # 内存库运行, 无副作用
```
目标机(NA51090/aarch64)：用交叉工具链 + 交叉编译的 sqlite3(≥3.9, 带 JSON1)。

## 实测已验证的结论

- **原样存/取完整 JSON**：写入侧不解析；`SELECT json …` 原样返回给上层。
- **按时间戳/通道/事件/类型** 检索走基础索引。
- **智能检索**：`json_extract` 深查；等值与**范围改写**(前缀)命中表达式索引 `SEARCH USING INDEX`（`EXPLAIN QUERY PLAN` 实证）。
- **注意**：`LIKE '前缀%'` 不吃表达式索引(会 SCAN)——前缀用 `>= AND <` 范围改写；任意子串/全文用 **FTS5**。
- **加密取舍**：需 SQL 深查的文档保持 `storage=0`(inline)+`enc=0`(明文)；MetaRegion/加密文档需先读回解密再由上层解析。

## 接线到真实引擎

`meta_demo.c` 里的 `rsdk_meta_open/put/query` 就是 `src/rsdk_meta.c` 的最小参考实现骨架：
- `db_path` 取自 `rsdk_features.conf` 的 `metadata.db_path`；
- 建库时按 `metadata.index_paths` 生成对应表达式索引；
- `storage=1` 分支把 JSON 落 MetaRegion(`rsdk_mdoc_hdr_t` + 完整 JSON，可 gzip+AES)，索引行只存指针；
- retention 由录像环形覆盖回调触发 `rsdk_meta_purge(before_ts)`。
