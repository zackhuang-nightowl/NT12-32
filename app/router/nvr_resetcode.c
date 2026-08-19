/***************************************************************************************
 *  nvr_resetcode.c — 对齐 master/AdminPWD/AdminPWD.cpp::GenerateSimplePassword。
 ***************************************************************************************/
#include "nvr_resetcode.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

/* 与 AdminPWD.cpp constBase64 一致 */
static const char *const_base64 =
    "VEhJUyBTT0ZUV0FSRSBJUyBQUk9WSURFRCBCWSBUSEUgT3BlblNTTCBQUk9KRUNUIGBgQVMgSVMnJyBBTkQgQU5ZRVhQUkVTU0VEIE9SIElNUExJRUQgV0FSUkFOVElFUywgSU5DTFVESU5HLCBCVVQgTk9UIExJTUlURUQgVE8sIFRIRVBVUlBPU0UgQVJFIERJU0NMQUlNRUQuICBJTiBOTyBFVkVOVCBTSEFMTCBUSEUgT3BlblNTTCBQUk9KRUNUIE9SSVRTIENPTlRSSUJVVE9SUyBCRSBMSUFCTEUgRk9SIEFOWSBESVJFQ1QsIElORElSRUNULCBJTkNJREVOVEFMLFNQRUNJQUwsIEVYRU1QTEFSWSwgT1IgQ09OU0VRVUVOVElBTCBEQU1BR0VTIChJTkNMVURJTkcsIEJVVE5PVCBMSU1JVEVEIFRPLCBQUk9DVVJFTUVOVCBPRiBTVUJTVElUVVRFIEdPT0RTIE9SIFNFUlZJQ0VTO0xPU1MgT0YgVVNFLCBEQVRBLCBPUiBQUk9GSVRTOyBPUiBCVVNJTkVTUyBJTlRFUlJVUFRJT04pSE9XRVZFUiBDQVVTRUQgQU5EIE9OIEFOWSBUSEVPUlkgT0YgTElBQklMSVRZLCBXSEVUSEVSIElOIENPTlRSQUNULFNUUklDVCA=0";

static int is_leap(int y)
{
    if (y % 4 != 0) return 0;
    if (y % 100 != 0) return 1;
    return (y % 400 == 0);
}

int nvr_resetcode_calc(const char *uid, const char *date_yyyy_mm_dd,
                       char *out, int out_cap)
{
    if (!uid || !date_yyyy_mm_dd || !out || out_cap < 8) return -1;
    out[0] = 0;
    if ((int)strlen(date_yyyy_mm_dd) != 10 || date_yyyy_mm_dd[4] != '-' || date_yyyy_mm_dd[7] != '-')
        return -1;

    int year = 0, month = 0, day = 0;
    if (sscanf(date_yyyy_mm_dd, "%d-%d-%d", &year, &month, &day) != 3) return -1;
    if (month < 1 || month > 12) return -1;
    int maxd = 31;
    if (month == 2) maxd = is_leap(year) ? 29 : 28;
    else if (month == 4 || month == 6 || month == 9 || month == 11) maxd = 30;
    if (day < 1 || day > maxd) return -1;

    char uid6[8];
    snprintf(uid6, sizeof(uid6), "%.6s", uid);
    if ((int)strlen(uid6) < 6) return -1;

    int b64len = (int)strlen(const_base64);
    int i = (year - 2000) + month * day;
    if (i < 0) i = 0;
    if (i + 10 > b64len) i = b64len > 10 ? b64len - 10 : 0;

    char hash10[16];
    memcpy(hash10, const_base64 + i, 10);
    hash10[10] = 0;

    char source[128];
    snprintf(source, sizeof(source), "NO%s%s%s", date_yyyy_mm_dd, uid6, hash10);

    /* 与原实现一致用 long double 累加（网站算法依赖浮点行为） */
    long double password = 1.0L;
    password += (long double)(year - 2000) * 1024.0L * 1024.0L;

    int source_size = (int)strlen(source);
    int j = source_size / 4;
    for (int k = 0; k < j; k++) {
        unsigned char *s = (unsigned char *)source;
        long double tmp = (long double)s[k * 4]
            + (long double)s[k * 4 + 1] * 256.0L
            + (long double)s[k * 4 + 2] * 256.0L * 256.0L
            + (long double)s[k * 4 + 3] * 256.0L * 256.0L * 256.0L;
        password += tmp;
    }

    char lastpwd[64];
    snprintf(lastpwd, sizeof(lastpwd), "%.0Lf", password);
    /* 去掉小数点（若有） */
    char *dot = strchr(lastpwd, '.');
    if (dot) *dot = 0;
    int len = (int)strlen(lastpwd);
    if (len < 6) return -1;

    /* 取末 6 位反序 */
    char code[8];
    for (int n = 0; n < 6; n++)
        code[n] = lastpwd[len - 1 - n];
    code[6] = 0;
    snprintf(out, (size_t)out_cap, "%s", code);
    return 0;
}

int nvr_resetcode_verify(const char *uid, const char *code)
{
    if (!uid || !code || !code[0]) return 0;
    time_t now = time(NULL);
    struct tm tm_local;
#if defined(_WIN32)
    if (localtime_s(&tm_local, &now) != 0) return 0;
#else
    if (!localtime_r(&now, &tm_local)) return 0;
#endif
    char date[16];
    snprintf(date, sizeof(date), "%04d-%02d-%02d",
             tm_local.tm_year + 1900, tm_local.tm_mon + 1, tm_local.tm_mday);
    char expect[16];
    if (nvr_resetcode_calc(uid, date, expect, (int)sizeof(expect)) != 0) return 0;
    return strcmp(expect, code) == 0;
}
