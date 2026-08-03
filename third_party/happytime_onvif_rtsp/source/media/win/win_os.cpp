/***************************************************************************************
 *
 *  IMPORTANT: READ BEFORE DOWNLOADING, COPYING, INSTALLING OR USING.
 *
 *  By downloading, copying, installing or using the software you agree to this license.
 *  If you do not agree to this license, do not download, install, 
 *  copy or use the software.
 *
 *  Copyright (C) 2014-2025, Happytimesoft Corporation, all rights reserved.
 *
 *  Redistribution and use in binary forms, with or without modification, are permitted.
 *
 *  Unless required by applicable law or agreed to in writing, software distributed 
 *  under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 *  CONDITIONS OF ANY KIND, either express or implied. See the License for the specific
 *  language governing permissions and limitations under the License.
 *
****************************************************************************************/

#include "sys_inc.h"
#include "win_os.h"


#define WINVER_REG_KEY L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion"

static inline bool win_get_reg_sz(HKEY key, const wchar_t *val, wchar_t *buf, DWORD size)
{
    const LSTATUS status = RegGetValueW(key, NULL, val, RRF_RT_REG_SZ, NULL, buf, &size);

    return status == ERROR_SUCCESS;
}

static inline void win_get_reg_ver(win_version * ver)
{
    HKEY key;
    DWORD size, dw_val;
    LSTATUS status;
    wchar_t str[256];

    status = RegOpenKeyW(HKEY_LOCAL_MACHINE, WINVER_REG_KEY, &key);
    if (status != ERROR_SUCCESS)
    {
        return;
    }
    
    size = sizeof(dw_val);

    status = RegQueryValueExW(key, L"CurrentMajorVersionNumber", NULL, NULL, (LPBYTE)&dw_val, &size);
    if (status == ERROR_SUCCESS)
    {
        ver->major = (int)dw_val;
    }
    
    status = RegQueryValueExW(key, L"CurrentMinorVersionNumber", NULL, NULL, (LPBYTE)&dw_val, &size);
    if (status == ERROR_SUCCESS)
    {
        ver->minor = (int)dw_val;
    }
    
    status = RegQueryValueExW(key, L"UBR", NULL, NULL, (LPBYTE)&dw_val, &size);
    if (status == ERROR_SUCCESS)
    {
        ver->revis = (int)dw_val;
    }
    
    if (win_get_reg_sz(key, L"CurrentBuildNumber", str, sizeof(str))) 
    {
        ver->build = wcstol(str, NULL, 10);
    }

    RegCloseKey(key);
}

static inline void win_get_rtl_ver(win_version *ver)
{
    HMODULE ntdll = GetModuleHandleW(L"ntdll");
    if (!ntdll)
    {
        return;
    }

    typedef NTSTATUS(WINAPI * rtl_get_ver)(RTL_OSVERSIONINFOEXW *);
    
    rtl_get_ver get_ver = (rtl_get_ver) GetProcAddress(ntdll, "RtlGetVersion");
    if (!get_ver) 
    {
        return;
    }

    RTL_OSVERSIONINFOEXW osver = {0};
    osver.dwOSVersionInfoSize = sizeof(osver);
    NTSTATUS s = get_ver(&osver);
    if (s < 0) 
    {
        return;
    }

    ver->major = osver.dwMajorVersion;
    ver->minor = osver.dwMinorVersion;
    ver->build = osver.dwBuildNumber;
    ver->revis = 0;
}

int win_version_compare(const win_version *dst, const win_version *src)
{
    if (dst->major > src->major)
    {
        return 1;
    }
    
    if (dst->major == src->major) 
    {
        if (dst->minor > src->minor)
        {
            return 1;
        }
        
        if (dst->minor == src->minor) 
        {
            if (dst->build > src->build)
            {
                return 1;
            }
            
            if (dst->build == src->build)
            {
                return 0;
            }    
        }
    }
    
    return -1;
}

static inline void win_use_higher_ver(win_version *cur_ver, win_version *new_ver)
{
    if (win_version_compare(cur_ver, new_ver) < 0)
    {
        *cur_ver = *new_ver;
    }    
}

void win_get_version(win_version * info)
{
    static win_version ver = {0};
    static bool got_version = false;

    if (!info)
    {
        return;
    }
    
    if (!got_version) 
    {
        win_version reg_ver = {0};
        win_version rtl_ver = {0};

        win_get_reg_ver(&reg_ver);
        win_get_rtl_ver(&rtl_ver);

        ver = reg_ver;
        win_use_higher_ver(&ver, &rtl_ver);

        got_version = true;
    }

    *info = ver;
}


