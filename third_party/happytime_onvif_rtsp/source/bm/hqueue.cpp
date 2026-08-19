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
#include "hqueue.h"

/***************************************************************************************/
HT_API HQUEUE * hqueue_create(uint32 unit_num, uint32 unit_size, uint32 queue_mode)
{
    uint32 q_len = unit_num * unit_size + sizeof(HQUEUE);

    HQUEUE *phq = (HQUEUE *)malloc(q_len);
    if (phq == NULL)
    {
        log_print(HT_LOG_ERR, "%s, malloc HQUEUE fail\r\n", __FUNCTION__);
        return NULL;
    }

    phq->queue_mode = queue_mode;
    phq->unit_size = unit_size;
    phq->unit_num = unit_num;
    phq->front = 0;
    phq->rear = 0;
    phq->count_put_full = 0;
    phq->queue_buffer = sizeof(HQUEUE);

    if (queue_mode & HQ_NO_EVENT)
    {
        phq->nnul_event = NULL;
        phq->nful_event = NULL;
        phq->mutex = NULL;
    }
    else
    {
        phq->nnul_event = sys_os_create_cond();
        phq->nful_event = sys_os_create_cond();
        phq->mutex = sys_os_create_cond_mutex();
    }

    return phq;
}

HT_API void hqueue_delete(HQUEUE *phq)
{
    if (phq == NULL)
    {
        return;
    }

    if (phq->queue_mode & HQ_NO_EVENT)
    {
    }
    else
    {
        sys_os_destroy_cond(phq->nnul_event);
        sys_os_destroy_cond(phq->nful_event);
        sys_os_destroy_cond_mutex(phq->mutex);
    }

    free(phq);
}

/***************************************************************************************/
void hqueue_mutex_enter(HQUEUE *phq)
{
    if ((phq->queue_mode & HQ_NO_EVENT) == 0)
    {
        sys_os_cond_mutex_enter(phq->mutex);
    }
}

void hqueue_mutex_leave(HQUEUE *phq)
{
    if ((phq->queue_mode & HQ_NO_EVENT) == 0)
    {
        sys_os_cond_mutex_leave(phq->mutex);
    }
}

HT_API BOOL hqueue_put(HQUEUE *phq, char *buf)
{
    uint32 real_rear, queue_count;
    char *ptr;

    if (phq == NULL || buf == NULL)
    {
        return FALSE;
    }

    hqueue_mutex_enter(phq);

hqueue_put_start:

    queue_count = phq->rear - phq->front;

    if (queue_count == (phq->unit_num - 1))
    {
        if ((phq->queue_mode & HQ_NO_EVENT) == 0)
        {
            if (phq->queue_mode & HQ_PUT_WAIT)
            {
                sys_os_cond_wait(phq->nful_event, phq->mutex);
                goto hqueue_put_start;
            }
            else 
            {
                phq->count_put_full++;
                hqueue_mutex_leave(phq);
                return FALSE;
            }
        }
        else
        {
            hqueue_mutex_leave(phq);
            return FALSE;
        }
    }

    real_rear = phq->rear % phq->unit_num;
    ptr = ((char *)phq) + phq->queue_buffer + real_rear*phq->unit_size;
    memcpy((char *)ptr, buf, phq->unit_size);
    phq->rear++;

    if ((phq->queue_mode & HQ_NO_EVENT) == 0)
    {
        sys_os_cond_signal(phq->nnul_event);
    }

    hqueue_mutex_leave(phq);

    return TRUE;
}

HT_API BOOL hqueue_get(HQUEUE *phq, char *buf)
{
    uint32 real_front;

    if (phq == NULL || buf == NULL)
    {
        return FALSE;
    }

    hqueue_mutex_enter(phq);

hqueue_get_start:

    if (phq->front == phq->rear)
    {
        if ((phq->queue_mode & HQ_NO_EVENT) == 0)
        {
            if (phq->queue_mode & HQ_GET_WAIT)
            {
                sys_os_cond_wait(phq->nnul_event, phq->mutex);
                goto hqueue_get_start;
            }
            else 
            {
                hqueue_mutex_leave(phq);
                return FALSE;
            }
        }
        else
        {
            hqueue_mutex_leave(phq);
            return FALSE;
        }
    }

    real_front = phq->front % phq->unit_num;
    memcpy(buf, ((char *)phq) + phq->queue_buffer + real_front*phq->unit_size, phq->unit_size);
    phq->front++;

    if ((phq->queue_mode & HQ_NO_EVENT) == 0)
    {
        sys_os_cond_signal(phq->nful_event);
    }

    hqueue_mutex_leave(phq);

    return TRUE;
}

HT_API BOOL hqueue_is_empty(HQUEUE *phq)
{
    if (phq == NULL)
    {
        return TRUE;
    }

    hqueue_mutex_enter(phq);

    if (phq->front == phq->rear)
    {
        hqueue_mutex_leave(phq);
        return TRUE;
    }

    hqueue_mutex_leave(phq);

    return FALSE;
}

HT_API BOOL hqueue_is_full(HQUEUE *phq)
{
    uint32 queue_count;

    if (phq == NULL)
    {
        return FALSE;
    }

    hqueue_mutex_enter(phq);

    queue_count = phq->rear - phq->front;

    if (queue_count == (phq->unit_num - 1))
    {
        hqueue_mutex_leave(phq);
        return TRUE;
    }

    hqueue_mutex_leave(phq);

    return FALSE;
}

HT_API BOOL hqueue_peek(HQUEUE *phq, char *buf)
{
    uint32 real_front;

    if (phq == NULL || buf == NULL)
    {
        return FALSE;
    }

    hqueue_mutex_enter(phq);

    if (phq->front == phq->rear)
    {
        hqueue_mutex_leave(phq);
        return FALSE;
    }

    real_front = phq->front % phq->unit_num;
    memcpy(buf, ((char *)phq) + phq->queue_buffer + real_front*phq->unit_size, phq->unit_size);

    hqueue_mutex_leave(phq);

    return TRUE;
}




