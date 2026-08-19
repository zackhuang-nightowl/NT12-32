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

#ifndef LKLIST_H
#define LKLIST_H

/************************************************************************************/
typedef struct LKNODE
{
    struct LKNODE *next;
    struct LKNODE *prev;
    void          *data;
} LKNODE;

/************************************************************************************/
typedef struct LKLIST
{
    LKNODE        *first;
    LKNODE        *last;
    void          *mutex;
} LKLIST;


#ifdef __cplusplus
extern "C" {
#endif

HT_API LKLIST * hlist_create(BOOL need_mutex);
HT_API void     hlist_free_container(LKLIST *list);
HT_API void     hlist_free_all_node(LKLIST *list);

HT_API void     hlist_get_ownership(LKLIST *list);
HT_API void     hlist_giveup_ownership(LKLIST *list);

HT_API BOOL     hlist_remove(LKLIST *list, LKNODE *node);
HT_API BOOL     hlist_remove_data(LKLIST *list, void *data);
HT_API void     hlist_remove_from_front(LKLIST *    list);
HT_API void     hlist_remove_from_back(LKLIST *list);

HT_API BOOL     hlist_add_at_front(LKLIST *list, void *data);
HT_API BOOL     hlist_add_at_back(LKLIST *list, void *data);
HT_API BOOL     hlist_insert(LKLIST *list, LKNODE *prev_node, void *data);
HT_API uint32   hlist_node_count(LKLIST *list);

HT_API LKNODE * hlist_lookup_start(LKLIST *list);
HT_API LKNODE * hlist_lookup_next(LKLIST *list, LKNODE *node);
HT_API void     hlist_lookup_end(LKLIST *list);
HT_API LKNODE * hlist_lookback_start(LKLIST *list);
HT_API LKNODE * hlist_lookback_next(LKLIST *list, LKNODE *node);
HT_API void     hlist_lookback_end(LKLIST *list);

HT_API BOOL     hlist_is_empty(LKLIST *list);
HT_API LKNODE * hlist_get_from_front(LKLIST *list);
HT_API LKNODE * hlist_get_from_back(LKLIST *list);

#ifdef __cplusplus
}
#endif

#endif // LKLIST_H


