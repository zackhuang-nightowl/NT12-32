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

/***************************************************************************************/
#include "sys_inc.h"


/***************************************************************************************/
HT_API void * sys_os_create_mutex()
{
    void * mutex = NULL;

#ifdef IOS
    static int index = 0;
    char name[32] = {'\0'};
    snprintf(name, sizeof(name), "testmutex%u", (uint32)time(NULL)+index++);

    mutex = sem_open(name, O_CREAT, 0644, 1);
    if (mutex == SEM_FAILED)
    {
        log_print(HT_LOG_ERR, "%s, sem_open failed. name=%s\r\n", __FUNCTION__, name);
        return NULL;
    }
#elif __WINDOWS_OS__
    mutex  = CreateMutex(NULL, FALSE, NULL);
#elif __LINUX_OS__
    mutex = (sem_t *)malloc(sizeof(sem_t));
    if (mutex)
    {
        int ret = sem_init((sem_t *)mutex, 0, 1);
        if (ret != 0)
        {
            free(mutex);
            return NULL;
        }
    }
#endif

    return mutex;
}

HT_API void sys_os_destroy_mutex(void * mutex)
{
    if (NULL == mutex)
    {
        return;
    }

#ifdef IOS
    sem_close((sem_t *)mutex);
#elif __WINDOWS_OS__
    CloseHandle(mutex);
#elif __LINUX_OS__
    sem_destroy((sem_t *)mutex);
    free(mutex);
#endif
}

HT_API void sys_os_mutex_enter(void * mutex)
{
    if (NULL == mutex)
    {
        return;
    }

#if __LINUX_OS__
    sem_wait((sem_t *)mutex);
#elif __WINDOWS_OS__
    WaitForSingleObject(mutex, INFINITE);
#endif
}

HT_API void sys_os_mutex_leave(void * mutex)
{
    if (NULL == mutex)
    {
        return;
    }

#if __LINUX_OS__
    sem_post((sem_t *)mutex);
#elif __WINDOWS_OS__
    ReleaseMutex(mutex);
#endif
}

HT_API void * sys_os_create_sig()
{    
    void * sig = NULL;
    
#ifdef IOS
    static int index = 0;
    char name[32] = {'\0'};
    snprintf(name, sizeof(name), "testsig%u", (uint32)time(NULL)+index++);
    
    sig = sem_open(name, O_CREAT, 0644, 1);
    if (sig == SEM_FAILED)
    {
        log_print(HT_LOG_ERR, "%s, sem_open failed. name=%s\r\n", __FUNCTION__, name);
        return NULL;
    }
#elif __WINDOWS_OS__
    sig = CreateEvent(NULL, FALSE, FALSE, NULL);
#elif __LINUX_OS__
    sig = malloc(sizeof(sem_t));
    if (sig)
    {
        int ret = sem_init((sem_t *)sig, 0, 0);
        if (ret != 0)
        {
            free(sig);
            return NULL;
        }
    }
#endif

    return sig;
}

HT_API void sys_os_destroy_sig(void * sig)
{
    if (NULL == sig)
    {
        return;
    }
    
#ifdef IOS
    sem_close((sem_t *)sig);
#elif __WINDOWS_OS__
    CloseHandle(sig);
#elif __LINUX_OS__
    sem_destroy((sem_t *)sig);
    free(sig);
#endif
}

HT_API void sys_os_sig_wait(void * sig)
{
    if (sig == NULL)
    {
        return;
    }
    
#if __LINUX_OS__
    sem_wait((sem_t *)sig);
#elif __WINDOWS_OS__
    WaitForSingleObject(sig, INFINITE);
#endif
}

HT_API int sys_os_sig_wait_timeout(void * sig, uint32 ms)
{
    if (sig == NULL)
    {
        return -1;
    }

#ifdef IOS
    while (ms > 0)
    {
        if (sem_trywait((sem_t *)sig) == 0)
        {
            return 0;
        }
        
        usleep(1000);
        
        ms -= 1;
    }
    
    return -1;
#elif __LINUX_OS__
    {
        int ret;
        struct timespec ts;
        struct timeval tv;

        gettimeofday(&tv, NULL);

        tv.tv_sec = tv.tv_sec + ms / 1000;
        tv.tv_usec = tv.tv_usec + (ms % 1000) * 1000;
        tv.tv_sec += tv.tv_usec / (1000 * 1000);
        tv.tv_usec = tv.tv_usec % (1000 * 1000);

        ts.tv_sec = tv.tv_sec;
        ts.tv_nsec = tv.tv_usec * 1000;

        ret = sem_timedwait((sem_t *)sig, &ts);
        if (ret == -1 && errno == ETIMEDOUT)
        {
            return -1;
        }
    }
#elif __WINDOWS_OS__
    {
        DWORD ret = WaitForSingleObject(sig, ms);
        if (ret == WAIT_FAILED)
        {
            return -1;
        }
        else if (ret == WAIT_TIMEOUT)
        {
            return -1;
        }
    }
#endif

    return 0;
}

HT_API void sys_os_sig_sign(void * sig)
{
    if (sig == NULL)
    {
        return;
    }
    
#if __LINUX_OS__
    sem_post((sem_t *)sig);
#elif __WINDOWS_OS__
    SetEvent(sig);
#endif
}

HT_API void * sys_os_create_cond_mutex()
{
    void * mutex = NULL;

#if __LINUX_OS__
    mutex = malloc(sizeof(pthread_mutex_t));
    if (mutex)
    {
        pthread_mutex_init((pthread_mutex_t *)mutex, NULL);
    }
#elif __WINDOWS_OS__
    mutex = malloc(sizeof(CRITICAL_SECTION));
    if (mutex)
    {
        InitializeCriticalSection((PCRITICAL_SECTION)mutex);
    }
#endif

    return mutex;
}

HT_API void sys_os_destroy_cond_mutex(void * mutex)
{
    if (NULL == mutex)
    {
        return;
    }

#if __LINUX_OS__
    pthread_mutex_destroy((pthread_mutex_t *)mutex);
#elif __WINDOWS_OS__
    DeleteCriticalSection((PCRITICAL_SECTION)mutex);
#endif

    free(mutex);
}

HT_API void sys_os_cond_mutex_enter(void * mutex)
{
    if (NULL == mutex)
    {
        return;
    }

#if __LINUX_OS__
    pthread_mutex_lock((pthread_mutex_t *)mutex);
#elif __WINDOWS_OS__
    EnterCriticalSection((PCRITICAL_SECTION)mutex);
#endif
}

HT_API void sys_os_cond_mutex_leave(void * mutex)
{
    if (NULL == mutex)
    {
        return;
    }

#if __LINUX_OS__
    pthread_mutex_unlock((pthread_mutex_t *)mutex);
#elif __WINDOWS_OS__
    LeaveCriticalSection((PCRITICAL_SECTION)mutex);
#endif
}

HT_API void * sys_os_create_cond()
{
    void * cond = NULL;

#if __LINUX_OS__
    cond = malloc(sizeof(pthread_cond_t));
    if (cond)
    {
        pthread_cond_init((pthread_cond_t *) cond, NULL);
    }
#elif __WINDOWS_OS__
    cond = malloc(sizeof(CONDITION_VARIABLE));
    if (cond)
    {
        InitializeConditionVariable((PCONDITION_VARIABLE) cond);
    }
#endif

    return cond;
}

HT_API void sys_os_destroy_cond(void * cond)
{
    if (NULL == cond)
    {
        return;
    }

#if __LINUX_OS__
    pthread_cond_destroy((pthread_cond_t *)cond);
#elif __WINDOWS_OS__
#endif

    free(cond);
}

HT_API void sys_os_cond_wait(void * cond, void * mutex)
{
    if (NULL == cond || NULL == mutex)
    {
        return;
    }

#if __LINUX_OS__
    pthread_cond_wait((pthread_cond_t *)cond, (pthread_mutex_t *)mutex);
#elif __WINDOWS_OS__
    SleepConditionVariableCS((PCONDITION_VARIABLE)cond, (PCRITICAL_SECTION)mutex, INFINITE);
#endif
}

HT_API void sys_os_cond_signal(void * cond)
{
    if (NULL == cond)
    {
        return;
    }

#if __LINUX_OS__
    pthread_cond_signal((pthread_cond_t *)cond);
#elif __WINDOWS_OS__
    WakeConditionVariable((PCONDITION_VARIABLE)cond);
#endif
}

HT_API void sys_os_cond_broadcast(void * cond)
{
    if (NULL == cond)
    {
        return;
    }

#if __LINUX_OS__
    pthread_cond_broadcast((pthread_cond_t *)cond);
#elif __WINDOWS_OS__
    WakeAllConditionVariable((PCONDITION_VARIABLE)cond);
#endif
}

HT_API pthread_t sys_os_create_thread(void * thread_func, void * argv)
{
    pthread_t tid = 0;

#if __LINUX_OS__
    int ret = pthread_create(&tid, NULL, (void* (*)(void*))thread_func, argv);
    if (ret != 0)
    {
        log_print(HT_LOG_ERR, "%s, pthread_create failed, ret = %d\r\n", __FUNCTION__, ret);
    }
#elif __WINDOWS_OS__
    tid = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)thread_func, argv, 0, NULL);
    if (tid == NULL)
    {
        log_print(HT_LOG_ERR, "%s, CreateThread failed, tid=%u, err=%u\r\n", __FUNCTION__, tid, GetLastError());
    }
#endif

    return tid;
}

HT_API void sys_os_wait_thread(pthread_t * tid)
{
    if (NULL == tid || 0 == *tid)
    {
        return;
    }
    
#if __LINUX_OS__
    pthread_join(*tid, NULL);
#elif __WINDOWS_OS__
    WaitForSingleObject(*tid, INFINITE);
    CloseHandle(*tid);
#endif

    *tid = 0;
}

HT_API void sys_os_detach_thread(pthread_t tid)
{
    if (0 == tid)
    {
        return;
    }
    
#if __LINUX_OS__
    pthread_detach(tid);
#elif __WINDOWS_OS__
    CloseHandle(tid);
#endif
}

HT_API uint32 sys_os_get_ms()
{
    uint32 ms = 0;

#if __LINUX_OS__
    struct timeval tv;
    gettimeofday(&tv, NULL);
    
    ms = tv.tv_sec * 1000 + tv.tv_usec/1000;
#elif __WINDOWS_OS__
    ms = GetTickCount();
#endif

    return ms;
}

HT_API uint32 sys_os_get_uptime()
{
    uint32 upt = 0;

#ifdef ANDROID

    upt = (uint32)time(NULL);
    
#elif __LINUX_OS__

    int rlen;
    char bufs[512];
    char * ptr;    
    FILE * file;

    file = fopen("/proc/uptime", "rb");
    if (NULL == file)
    {
        return (uint32)time(NULL);
    }
    
    rlen = fread(bufs, 1, sizeof(bufs)-1, file);
    fclose(file);

    if (rlen <= 0)
    {
        return (uint32)time(NULL);
    }
    
    bufs[rlen] = '\0';
    ptr = bufs;
    
    while (*ptr != '\0' && *ptr != ' ' && *ptr != '\r' && *ptr != '\n')
    {
        ptr++;
    }
    
    if (*ptr == ' ')
    {
        *ptr = '\0';
    }    
    else
    {
        return (uint32)time(NULL);
    }
    
    upt = (uint32)strtod(bufs, NULL);

#elif __WINDOWS_OS__

    LARGE_INTEGER freq;
    LARGE_INTEGER cur;
    
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&cur);
    
    upt = (uint32) (cur.QuadPart / freq.QuadPart);

#endif

    return upt;
}

HT_API char * sys_os_get_socket_error()
{
    char * p_estr = NULL;
    
#if __LINUX_OS__

    p_estr = strerror(errno);
    
#elif __WINDOWS_OS__

    int err = WSAGetLastError();
    static char err_buf[24];
    snprintf(err_buf, sizeof(err_buf), "WSAE-%d", err);
    p_estr = err_buf;
    
#endif

    return p_estr;
}

HT_API int sys_os_get_socket_error_num()
{
    int sockerr;
    
#if __LINUX_OS__
    sockerr = errno;
#elif __WINDOWS_OS__
    sockerr = WSAGetLastError();
#endif

    return sockerr;
}



