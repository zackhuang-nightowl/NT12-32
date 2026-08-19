/***************************************************************************************
 *  nvr_resetcode.h — Admin 找回密码 ResetCode（对齐 master/AdminPWD/AdminPWD.cpp）。
 ***************************************************************************************/
#ifndef NVR_RESETCODE_H
#define NVR_RESETCODE_H

#ifdef __cplusplus
extern "C" {
#endif

/* 用 UID 前 6 字符 + 日期 yyyy-mm-dd 计算 6 位 ResetCode，写入 out(≥8)。
 * 成功返回 0。 */
int nvr_resetcode_calc(const char *uid, const char *date_yyyy_mm_dd,
                       char *out, int out_cap);

/* 用设备当前本地日期校验 code；uid 取前 6 字符。匹配返回 1。 */
int nvr_resetcode_verify(const char *uid, const char *code);

#ifdef __cplusplus
}
#endif
#endif /* NVR_RESETCODE_H */
