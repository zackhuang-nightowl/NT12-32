-- ===========================================================================
--  meta_schema.sql —— 元数据文档索引库(设计 §12.3 / 冻结 §5)
--  依赖: SQLite ≥3.9 (JSON1 内建); FTS5 可选。
--  用法: sqlite3 meta.db < sql/meta_schema.sql   (或 rsdk_meta_open 内部执行)
--  模型: json 列存"完整结构体原样"; 其余列是调用方给的检索键, 写入侧不解析 JSON。
-- ===========================================================================

PRAGMA journal_mode = WAL;      -- 掉电安全
PRAGMA synchronous  = NORMAL;   -- WAL 下兼顾性能与安全
PRAGMA foreign_keys = ON;

-- ---- 文档索引主表 ---------------------------------------------------------
CREATE TABLE IF NOT EXISTS meta_doc (
  id        INTEGER PRIMARY KEY AUTOINCREMENT,
  ts        INTEGER NOT NULL,      -- epoch 秒(主检索键, 按时间戳)
  ts_ms     INTEGER DEFAULT 0,
  chn       INTEGER,               -- 通道(-1/NULL=无)
  event_id  INTEGER,               -- 事件绑定(与视频/抓拍同一 event)
  doc_type  INTEGER,               -- 文档类型标签(SDK 不据此解析内容)
  seg_disk  INTEGER, seg_chunk INTEGER, seg_off INTEGER, seg_pts INTEGER,  -- 视频绑定
  storage   INTEGER DEFAULT 0,     -- 0=inline(json 列) 1=blobref(MetaRegion)
  enc       INTEGER DEFAULT 0,     -- 0=明文 1=AES-CTR; bit1=gzip
  json_len  INTEGER,               -- 原始 JSON 字节
  json      TEXT,                  -- storage=0: 完整 JSON 原样(需 SQL 深查者放这里)
  meta_disk INTEGER, meta_off INTEGER, meta_len INTEGER  -- storage=1: 指向 MetaRegion
);

-- ---- 基础索引(按时间戳 / 通道-事件) ---------------------------------------
CREATE INDEX IF NOT EXISTS ix_meta_ts ON meta_doc(ts);
CREATE INDEX IF NOT EXISTS ix_meta_ce ON meta_doc(chn, event_id);
CREATE INDEX IF NOT EXISTS ix_meta_et ON meta_doc(event_id);
CREATE INDEX IF NOT EXISTS ix_meta_dt ON meta_doc(doc_type, ts);

-- ---- 智能检索表达式索引(来自 rsdk_features.conf 的 metadata.index_paths) ----
--   写入侧不解析; SQLite 在 INSERT 时自动算 json_extract 存入索引 B-tree。
--   仅对 storage=0(inline)+enc=0(明文) 的行有效(见冻结 §6 注)。
CREATE INDEX IF NOT EXISTS ix_meta_cls   ON meta_doc(json_extract(json,'$.objects[0].cls'))     WHERE storage=0 AND enc=0;
CREATE INDEX IF NOT EXISTS ix_meta_color ON meta_doc(json_extract(json,'$.objects[0].color'))   WHERE storage=0 AND enc=0;
CREATE INDEX IF NOT EXISTS ix_meta_plate ON meta_doc(json_extract(json,'$.objects[0].plate'))   WHERE storage=0 AND enc=0;
CREATE INDEX IF NOT EXISTS ix_meta_face  ON meta_doc(json_extract(json,'$.objects[0].face_id')) WHERE storage=0 AND enc=0;
CREATE INDEX IF NOT EXISTS ix_meta_event ON meta_doc(json_extract(json,'$.event'))              WHERE storage=0 AND enc=0;

-- ---- (可选)FTS5 全文/模糊检索: metadata.fts=on 时创建 --------------------
-- CREATE VIRTUAL TABLE IF NOT EXISTS meta_fts USING fts5(json, content='meta_doc', content_rowid='id');
-- CREATE TRIGGER meta_ai AFTER INSERT ON meta_doc BEGIN
--   INSERT INTO meta_fts(rowid, json) VALUES (new.id, new.json); END;
-- CREATE TRIGGER meta_ad AFTER DELETE ON meta_doc BEGIN
--   INSERT INTO meta_fts(meta_fts, rowid, json) VALUES('delete', old.id, old.json); END;
-- 查询: SELECT id FROM meta_fts WHERE meta_fts MATCH 'person AND red';
