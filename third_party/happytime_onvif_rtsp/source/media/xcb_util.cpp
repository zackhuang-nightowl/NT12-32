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
#include "xcb_util.h"

xcb_screen_t * xcb_get_screen(const xcb_setup_t *setup, int screen_num)
{
    xcb_screen_iterator_t it = xcb_setup_roots_iterator(setup);
    xcb_screen_t *screen     = NULL;

    for (; it.rem > 0; xcb_screen_next(&it)) 
    {
        if (!screen_num) 
        {
            screen = it.data;
            break;
        }

        screen_num--;
    }

    return screen;
}

xcb_atom_t xcb_get_atom(xcb_connection_t * c, const char * atom_name)
{
    xcb_atom_t atom;
    xcb_intern_atom_cookie_t cookie;
    xcb_intern_atom_reply_t *reply;
    
    cookie = xcb_intern_atom(c, false, strlen(atom_name), atom_name);
    reply = xcb_intern_atom_reply(c, cookie, NULL);
    if (NULL != reply)
    {
        atom = reply->atom;
        free(reply);
        return atom;
    }
    
    return 0;
}


