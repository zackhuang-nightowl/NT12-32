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
#include "linked_list.h"


/************************************************************************************/
HT_API LKLIST * hlist_create(BOOL need_mutex)
{
    LKLIST *list;

    list = (LKLIST*) malloc(sizeof(LKLIST));
    if (list == NULL)
    {
        return NULL;
    }
        
    list->first = NULL;
    list->last = NULL;

    if (need_mutex)
    {
        list->mutex = sys_os_create_mutex();
    }    
    else
    {
        list->mutex = NULL;
    }
    
    return list;
}

/************************************************************************************/
HT_API void hlist_get_ownership(LKLIST *list)
{
    if (list->mutex)
    {
        sys_os_mutex_enter(list->mutex);
    }
}

HT_API void hlist_giveup_ownership(LKLIST *list)
{
    if (list->mutex)
    {
        sys_os_mutex_leave(list->mutex);
    }
}

/************************************************************************************/
HT_API void hlist_free_container(LKLIST *list)
{
    LKNODE *node;
    LKNODE *next_node;

    if (list == NULL)
    {
        return;
    }
    
    hlist_get_ownership(list);

    node = list->first;

    while (node != NULL) 
    {
        void * p_free;

        next_node = node->next;
        
        p_free = node->data;

        if (p_free != NULL)
        {
            free(p_free);
        }
        
        free (node);

        node = next_node;        
    }

    hlist_giveup_ownership(list);

    if (list->mutex)
    {
        sys_os_destroy_mutex(list->mutex);
    }

    free (list);
}

/************************************************************************************/
HT_API void hlist_free_all_node(LKLIST *list)
{
    LKNODE *node;
    LKNODE *next_node;

    if (list == NULL)
    {
        return;
    }
    
    hlist_get_ownership(list);

    node = list->first;

    while (node != NULL) 
    {
        next_node = node->next;
        
        if (node->data != NULL)
        {
            free(node->data);
        }
        
        free (node);

        node = next_node;        
    }
    
    list->first = NULL;
    list->last = NULL;

    hlist_giveup_ownership(list);
}

/************************************************************************************/
HT_API BOOL hlist_add_at_front(LKLIST *list, void *data)
{
    LKNODE *node;
    LKNODE *next_node;

    if (list == NULL || data == NULL)
    {
        return FALSE;
    }
    
    node = (LKNODE*) malloc(sizeof(LKNODE));
    if (node == NULL)
    {
        return FALSE;
    }
    
    node->next = NULL;
    node->prev = NULL;
    node->data = data;

    hlist_get_ownership(list);

    if (list->first == NULL)
    {
        list->first = node;
        list->last = node;

        node->prev = NULL;
        node->next = NULL;
    }
    else
    {
        next_node = list->first;

        node->next = next_node;
        node->prev = NULL;

        next_node->prev = node;
        list->first = node;
    }

    hlist_giveup_ownership(list);
    
    return TRUE;
}

/************************************************************************************/
HT_API void hlist_remove_from_front(LKLIST *list)
{
    LKNODE *node_to_remove;

    if (list == NULL)
    {
        return;
    }
    
    hlist_get_ownership(list);

    node_to_remove = list->first;

    if (node_to_remove == NULL) 
    {
        hlist_giveup_ownership(list);
        return;
    }

    if (list->first == list->last)
    {
        list->first = NULL;
        list->last = NULL;
    }
    else
    {
        list->first = node_to_remove->next;
        list->first->prev = NULL;
    }

    free(node_to_remove);
    
    hlist_giveup_ownership(list);
}

/************************************************************************************/
HT_API BOOL hlist_add_at_back(LKLIST *list, void *data)
{
    LKNODE *node;
    LKNODE *prev_node;

    if (list == NULL || data == NULL)
    {
        return FALSE;
    }
    
    node = (LKNODE*) malloc(sizeof(LKNODE));
    if (node == NULL)
    {
        return FALSE;
    }
    
    node->next = NULL;
    node->prev = NULL;
    node->data = data;

    hlist_get_ownership(list);

    if (list->last == NULL)
    {
        list->last = node;
        list->first = node;
    
        node->next = NULL;
        node->prev = NULL;
    }
    else
    {
        prev_node = list->last;

        node->next = NULL;
        node->prev = prev_node;

        prev_node->next = node;

        list->last = node;
    }

    hlist_giveup_ownership(list);

    return TRUE;
}

/************************************************************************************/
HT_API void hlist_remove_from_back(LKLIST *list)
{
    LKNODE *node_to_remove;

    if (list == NULL)
    {
        return;
    }
    
    hlist_get_ownership(list);

    if (list->last == NULL)    
    {
        hlist_giveup_ownership(list);
        return;
    }
        
    if (list->first == list->last)
    {
        node_to_remove = list->first;

        list->first = NULL;
        list->last = NULL;

        free(node_to_remove);

        hlist_giveup_ownership(list);
        return;
    }
        
    node_to_remove = list->last;
    
    list->last = node_to_remove->prev;

    list->last->next = NULL;
    
    free(node_to_remove);

    node_to_remove = NULL;
    
    hlist_giveup_ownership(list);
}

HT_API BOOL hlist_remove(LKLIST *list, LKNODE *node)
{
    LKNODE *prev_node;
    LKNODE *next_node;

    if (list == NULL || node == NULL)
    {
        return FALSE;
    }

    prev_node = node->prev;
    
    next_node = node->next;
    
    if (prev_node != NULL)
    {
        prev_node->next = next_node;
    }    
    else
    {
        list->first = next_node;
    }

    if (next_node != NULL)
    {
        next_node->prev = prev_node;
    }    
    else
    {
        list->last = prev_node;
    }
    
    free (node);

    return TRUE;
}

HT_API BOOL hlist_remove_data(LKLIST *list, void *data)
{
    LKNODE *prev_node;
    LKNODE *next_node;
    LKNODE *node;

    if (list == NULL || data == NULL)
    {
        return FALSE;
    }
    
    hlist_get_ownership(list);

    node = list->first;

    while (node != NULL)
    {
        if (data == node->data)
        {
            break;
        }
        
        node = node->next;
    }

    if (node == NULL)
    {
        hlist_giveup_ownership(list);
        return FALSE;
    }

    prev_node = node->prev;
    
    next_node = node->next;
    
    if (prev_node != NULL)
    {
        prev_node->next = next_node;
    }    
    else
    {
        list->first = next_node;
    }

    if (next_node != NULL)
    {
        next_node->prev = prev_node;
    }    
    else
    {
        list->last = prev_node;
    }
    
    free(node);

    hlist_giveup_ownership(list);

    return TRUE;
}

/************************************************************************************/
HT_API uint32 hlist_node_count(LKLIST *list)
{
    uint32 numbers;
    LKNODE *node;

    if (NULL == list)
    {
        return 0;
    }
    
    hlist_get_ownership(list);

    node = list->first;

    numbers = 0;
    
    while (node !=  NULL)
    {
        ++numbers;
        node = node->next;
    }

    hlist_giveup_ownership(list);

    return numbers;
}

/************************************************************************************/
HT_API BOOL hlist_insert(LKLIST *list, LKNODE *prev_node, void *data)
{
    if (list == NULL || data == NULL)
    {
        return FALSE;
    }
    
    if (prev_node == NULL)
    {
        hlist_add_at_front(list, data);
    }    
    else
    {
        if (prev_node->next == NULL)
        {
            hlist_add_at_back(list, data);
        }
        else
        {
            LKNODE *node = (LKNODE *)malloc(sizeof(LKNODE));
            if (NULL == node)
            {
                return FALSE;
            }
        
            hlist_get_ownership(list);

            node->data = data;
            node->next = prev_node->next;
            node->prev = prev_node;
            prev_node->next->prev = node;
            prev_node->next = node;
        
            hlist_giveup_ownership(list);
        }
    }

    return TRUE;
}

/***********************************************************************/
HT_API LKNODE * hlist_lookup_start(LKLIST *list)
{
    if (list == NULL)
    {
        return NULL;
    }
    
    hlist_get_ownership(list);

    if (list->first)
    {
        return list->first;
    }

    return NULL;
}

HT_API LKNODE * hlist_lookup_next(LKLIST *list, LKNODE *node)
{
    if (node == NULL)
    {
        return NULL;
    }
    
    return node->next;
}

HT_API void hlist_lookup_end(LKLIST *list)
{
    if (list == NULL)
    {
        return;
    }
    
    hlist_giveup_ownership(list);
}

HT_API LKNODE * hlist_lookback_start(LKLIST *list)
{
    if (list == NULL)
    {
        return NULL;
    }
    
    hlist_get_ownership(list);

    if (list->last)
    {
        return list->last;
    }

    return NULL;
}

HT_API LKNODE * hlist_lookback_next(LKLIST *list, LKNODE *node)
{
    if (node == NULL)
    {
        return NULL;
    }
    
    return node->prev;
}

HT_API void hlist_lookback_end(LKLIST *list)
{
    if (list == NULL)
    {
        return;
    }
    
    hlist_giveup_ownership(list);
}

HT_API LKNODE * hlist_get_from_front(LKLIST *list)
{
    if (list == NULL)
    {
        return NULL;
    }
    
    return list->first;
}

HT_API LKNODE * hlist_get_from_back(LKLIST *list)
{
    if (list == NULL)
    {
        return NULL;
    }
    
    return list->last;
}

HT_API BOOL hlist_is_empty(LKLIST *list)
{
    if (list == NULL)
    {
        return TRUE;
    }
    
    return (list->first == NULL);
}



