# NOP ↔ ONVIF 映射层 代码约定 (CONVENTIONS)

本目录 (`src/onvif/mapping/`) 实现 NVR 作为 ONVIF 客户端时,把私有 NOP 接口翻译成
ONVIF 客户端调用的映射层。以下约定务必遵守,保证可维护、可审计。

## 1. 分层与依赖方向(严格单向)

```
cap_*.c (业务层)                       —— 前门:is_onvif() 命中则 dispatch()
  → nop_onvif_map.h  (facade,唯一对外接口)
     → onvif_map_table.c   (中央映射表:NOP func 名 → 处理函数)
     → onvif_map_dispatch.c(查表 + 路由)
     → onvif_map_<domain>.c(每域一文件,每 NOP 接口一个 handler)
        → onvif_map_utils.*  (非几何通用转换,共享)
        → onvif_coord.*      (几何坐标转换,纯 C,可独立单测)
        → nop_onvif.h / nop_onvif_ext.h  (C ABI,唯一 ONVIF 边界)
           → onvif_adapter*.cpp (C++,封装 vendored 库)
```

- **纯 C 铁律**:`onvif_map_*.c`、`onvif_coord.*`、`onvif_map_utils.*` 一律纯 C,
  只 include `base/nop_json.h` + C ABI 头,**绝不** include vendored ONVIF 头。
- 所有 ONVIF/C++ 藏在 `nop_onvif.h` / `nop_onvif_ext.h` 之后。
- `onvif_coord.*` 不依赖任何本层其它文件,保证测试零依赖链接。

## 2. 编译门控

- 全部映射 `.c` 始终编入 `nopcore_obj`,但驱动 ONVIF 的函数体包在
  `#if NOP_ONVIF_MAP ... #endif` 内;`NOP_WITH_ONVIF=ON` 时 CMake 定义
  `NOP_ONVIF_MAP=1`。
- `NOP_WITH_ONVIF=OFF` 时:`onvif_map_dispatch.c` 的 `#else` 分支提供 facade 桩
  (`is_onvif()==0`、`dispatch()==NOP_ERR_NOTIMPL`、lifecycle 空实现),SDK 无 ONVIF
  也能链接;其余映射 `.c` 编成空目标。
- `onvif_coord.*` 无门控(纯数学,永远编译)。

## 3. 命名约定

| 层 | 前缀 | 例 |
|----|------|----|
| facade(对外) | `nop_onvif_map_` | `nop_onvif_map_dispatch` |
| 每接口 handler | `onvif_map_<nopFuncName>` | `onvif_map_ptzMove` |
| 会话 | `onvif_session_` | `onvif_session_begin` |
| 通用工具 | `onvif_map_` | `onvif_map_dir_to_velocity` |
| 坐标 | `nop_coord_` | `nop_coord_norm_to_thousandths` |
| 扩展 C ABI | `nop_onvif_<domain>_<verb>` | `nop_onvif_ptz_set_home` |

- **每个 NOP 接口 = 一个 handler 函数**,函数名与 NOP func 名一一对应
  (`onvif_map_<func>`),便于对照 NOPMappingONVIF.md 审计。
- handler 统一签名 `onvif_mapper_fn`:
  `nop_status_t fn(nop_onvif_map_backend_t *be, int ch, const nop_request_t *req, nop_response_t *resp)`。

## 4. 中央映射表

- 唯一权威表在 `onvif_map_table.c` 的 `g_onvif_map_table[]`,按 spec 域分组,
  每行 `{ "nopFuncName", onvif_map_nopFuncName }`。新增接口只加一行 + 一个 handler。
- `dispatch` 仅做:解析 channel → 查表 → 调 handler。不含业务分支。

## 5. 通用转换必须复用

- **不要**在多个 mapper 里重复写方向解析、速度归一化、返回码转换、token 格式化等。
  统一放 `onvif_map_utils.*`(非几何)或 `onvif_coord.*`(几何),共享调用。
- 新增可复用转换时,先查这两个文件是否已有;没有再加,并补注释。

## 6. 错误处理

- ONVIF adapter 约定:返回 `0` 成功,`<0` 失败。用 `onvif_map_rc()` 统一转
  `NOP_OK` / `NOP_ERR_IO`。
- 参数缺失 → `NOP_ERR_PARAM`(400);无对应 ONVIF 映射 → `NOP_ERR_NOTIMPL`(501)。
- 业务级错误放 `resp->content.error`,函数仍返回 `NOP_OK`(与现有 cap 一致)。
- 会话拿不到(通道非 ONVIF/连不上)→ `NOP_ERR_IO`。

## 7. 会话使用

- 每次 ONVIF 访问:`onvif_session_begin(be, ch)`(加锁+惰性建连+解析 profile)
  → 用 `onvif_session_dev/profile/...` → **必须** `onvif_session_end(be)` 解锁。
  任何提前 return 前先 `end()`。
- backend 级串行(vendored 句柄非可重入)。

## 8. 坐标转换(几何)

- 一律走 `onvif_coord.*`,公式来自 NOPMappingONVIF.md §1,禁止在 mapper 内散写。
- 空点集用 `NOP_COORD_EMPTY` 哨兵,mapper 据域译成"禁用规则/全幅默认",
  不得下发退化零面积多边形。
