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
#include "http.h"
#include "base64.h"
#include "http_parse.h"
#include "httpd.h"
#include "http_auth.h"
#include "http_srv.h"
#include "onvif.h"
#include "onvif_cm.h"
#include "linked_list.h"

#ifdef HTTPD

/***************************************************************************************/

#define SERVER_NAME "Happytimesoft"
#define SERVER_URL  "https://www.happytimesoft.com"
#define PROTOCOL    "HTTP/1.1"
#define RFC1123FMT  "%a, %d %b %Y %H:%M:%S GMT"
#define DEFFILE     "main.html"
#define MAX_OUTPUT  (1024*1024)
#define WEBSPATH    "html"

/***************************************************************************************/

mime_handler mime_handlers[] = {
    { "**.htm", "text/html", 1 },
    { "**.html", "text/html", 1 },
    { "**.js", "text/js", 0 },
    { "**.gif", "image/gif", 1 },
    { "**.jpg", "image/jpeg", 1 },
    { "**.jpeg", "image/gif", 1 },
    { "**.png", "image/png", 1 },
    { "**.ico", "image/x-icon", 1 },
    { "**.css", "text/css", 0 },
    { "**.au", "audio/basic", 1 },
    { "**.wav", "audio/wav", 1 },
    { "**.avi", "video/x-msvideo", 1 },
    { "**.mov", "video/quicktime", 1 },
    { "**.mpeg", "video/mpeg", 1 },
    { "**.vrml", "model/vrml", 1 },
    { "**.midi", "audio/midi", 1 },
    { "**.mp3", "audio/mpeg", 1 },
    { "**.ts", "video/mp2t", 1 },
    { "**.m3u8", "application/vnd.apple.mpegurl", 1 },
    { NULL, NULL, 0 }
};

func_handler func_handlers[] = {
    { NULL, NULL }
};

/***************************************************************************************/

void httpd_send_error(HTTPCLN * p_user, int status, const char* title, const char * exhdr, const char* text)
{
    char body[2 * 1024] = {'\0'};
    char buff[4 * 1024] = {'\0'};
    int offset = 0;
    int bodyoff = 0;
    int buflen = sizeof(buff)-1;
    int bodylen = sizeof(body)-1;
    time_t now;
    char timebuf[100];  

    bodyoff += snprintf(body+bodyoff, bodylen-bodyoff, "<HTML>"
                "<HEAD>"
                "<TITLE>%d %s</TITLE>"
                "</HEAD>\n"
                "<BODY BGCOLOR=\"#cc9999\">"
                "<H4>%d %s</H4>\n", status, title, status, title);
    bodyoff += snprintf(body+bodyoff, bodylen-bodyoff, "%s\n", text);
    bodyoff += snprintf(body+bodyoff, bodylen-bodyoff, "<HR>\n"
                "<ADDRESS><A HREF=\"%s\">%s</A></ADDRESS>\n"
                "</BODY>"
                "</HTML>\n", SERVER_URL, SERVER_NAME);

    now = time(NULL);
    strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime(&now));
    
    offset += snprintf(buff+offset, buflen-offset, 
                "%s %d %s\r\n"
                "Server: %s\r\n"
                "Date: %s\r\n"
                "Content-Length: %d\r\n"
                "Content-Type: %s\r\n"
                "Connection: %s\r\n", 
                PROTOCOL, status, title,
                SERVER_NAME,
                timebuf,
                bodyoff,
                "text/html",
                p_user->keep_alive ? "keep-alive" : "close");

    if (exhdr)
    {        
        offset += snprintf(buff+offset, buflen-offset, "%s", exhdr);
    }
    
    offset += snprintf(buff+offset, buflen-offset, "\r\n");
    
    strncpy(buff+offset, body, bodyoff);
    offset += bodyoff;

    log_print(HT_LOG_DBG, "%s, %s\r\n", __FUNCTION__, buff);
    
    http_srv_cln_tx(p_user, buff, offset);
}

void httpd_send_authenticate(HTTPD_CLASS * httpd, HTTPCLN * p_user)
{
    int offset = 0;
    char header[1000] = {'\0'};
    HD_AUTH_INFO * p_auth = &httpd->auth_info;

    if (p_auth->auth_nonce[0] == '\0')
    {
        srand(sys_os_get_ms());
        
        snprintf(p_auth->auth_nonce, sizeof(p_auth->auth_nonce), "%08X%08X", rand(), rand());    
        strcpy(p_auth->auth_qop, "auth");
        strcpy(p_auth->auth_realm, "happytimesoft");
    }

    // Default use of MD5 algorithm
    offset += snprintf(header+offset, sizeof(header)-offset-1, 
        "WWW-Authenticate: Digest realm=\"%s\", qop=\"%s\", nonce=\"%s\"\r\n", 
        p_auth->auth_realm, p_auth->auth_qop, p_auth->auth_nonce);

    // Using SHA-256 algorithm
    offset += snprintf(header+offset, sizeof(header)-offset-1, 
        "WWW-Authenticate: Digest realm=\"%s\", qop=\"%s\", nonce=\"%s\", algorithm=SHA-256\r\n", 
        p_auth->auth_realm, p_auth->auth_qop, p_auth->auth_nonce);
        
    httpd_send_error(p_user, 401, "Unauthorized", header, "Authorization required.");
}

void httpd_do_file_output(HTTPCLN * p_user, const char * filename, mime_handler * p_handler)
{
    char * buff;
    int offset = 0;
    int filelen;
    int fileoffset = 0;
    int buflen;
    time_t now;
    char timebuf[100];
    char filetime[100];
    FILE * fp;
    struct stat filestat;
    struct tm * filetm;
    
    now = time(NULL);
    strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime(&now));

    fp = fopen(filename, "rb");
    if (!fp)
    {
        httpd_send_error(p_user, 404, "Not Found", NULL, "File not found.");
        log_print(HT_LOG_WARN, "%s, open file (%s) failed\r\n", __FUNCTION__, filename);
        return;
    }

    fseek(fp, 0, SEEK_END);
    
    filelen = ftell(fp);
    if (filelen <= 0)
    {
        httpd_send_error(p_user, 404, "Not Found", NULL, "File not found.");
        fclose(fp);
        return;
    }

    fseek(fp, 0, SEEK_SET);

    fstat(fileno(fp), &filestat);

    filetm = gmtime(&filestat.st_mtime);
    strftime(filetime, sizeof(filetime)-1, RFC1123FMT, filetm);

    if (filelen + 1024 < MAX_OUTPUT)
    {
        buflen = filelen + 1024;
    }
    else
    {
        buflen = MAX_OUTPUT;
    }

    buff = (char *)malloc(buflen);
    if (NULL == buff)
    {
        httpd_send_error(p_user, 500, "Internal service error", NULL, "Internal service error.");
        
        fclose(fp);
        log_print(HT_LOG_ERR, "%s, memory malloc (%d) failed\r\n", __FUNCTION__, buflen);
        return;
    }

    offset += snprintf(buff+offset, buflen-offset, 
                "%s %d %s\r\n"
                "Server: %s\r\n"
                "Date: %s\r\n"
                "Content-Type: %s\r\n"
                "Content-Length: %d\r\n"
                "Last-Modified: %s\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Cache-Control: no-cache\r\n"
                "Connection: %s\r\n"
                "\r\n", 
                PROTOCOL, 200, "OK",
                SERVER_NAME,
                timebuf,
                p_handler->mime_type,
                filelen,
                filetime,
                p_user->keep_alive ? "keep-alive" : "close");

    buff[offset] = '\0';
    log_print(HT_LOG_DBG, "%s, buff = %s\r\n", __FUNCTION__, buff);
    
    while (fileoffset < filelen)
    {
        int rlen = (int)fread(buff+offset, 1, buflen-offset, fp);
        if (rlen <= 0)
        {
            break;
        }
        
        offset += rlen;
        fileoffset += rlen;

        if (http_srv_cln_tx(p_user, buff, offset) <= 0)
        {
            break;
        }

        offset = 0;
    }

    fclose(fp);
    free(buff);
}

int httpd_match_one(const char* pattern, int patternlen, const char* string)
{
    const char* p;

    for (p = pattern; p - pattern < patternlen; ++p, ++string)
    {
        if (*p == '?' && *string != '\0')
        {
            continue;
        }
        
        if (*p == '*')
        {
            int i, pl;
            ++p;
            
            if (*p == '*')
            {
                /* Double-wildcard matches anything. */
                ++p;
                i = (int)strlen(string);
            }
            else
            {
                /* Single-wildcard matches anything but slash. */
                i = (int)strcspn(string, "/");
            }
            
            pl = patternlen - (int)(p - pattern);
            
            for (; i >= 0; --i)
            {
                if (httpd_match_one(p, pl, &(string[i])))
                {
                    return 1;
                }    
            }
            
            return 0;
        }
        
        if (*p != *string)
        {
            return 0;
        }    
    }
    
    if (*string == '\0')
    {
        return 1;
    }
    
    return 0;
}

int httpd_match(const char* pattern, const char* string)
{
    const char * por;
    
    for (;;)
    {
        por = strchr(pattern, '|');
        
        if (por == NULL)
        {
            return httpd_match_one(pattern, (int)strlen(pattern), string);
        }
        
        if (httpd_match_one( pattern, (int)(por - pattern), string))
        {
            return 1;
        }
        
        pattern = por + 1;
    }
}

void httpd_do_func_output(HTTPCLN * p_user, func_handler * p_handler)
{
    char header[1024] = {'\0'};
    char buffer[1024] = {'\0'};
    int offset = 0;
    int buflen = 0;
    time_t now;
    char timebuf[100];  
    
    now = time(NULL);
    strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime(&now));

    if (p_handler->output)
    {
        buflen = p_handler->output(p_user, buffer, sizeof(buffer));
    }

    offset = snprintf(header, sizeof(header), 
                "%s %d %s\r\n"
                "Server: %s\r\n"
                "Date: %s\r\n"
                "Content-Type: %s\r\n"
                "Content-Length: %d\r\n"
                "Connection: %s\r\n"
                "\r\n", 
                PROTOCOL, 200, "OK",
                SERVER_NAME,
                timebuf,
                p_handler->mime_type,
                buflen,
                p_user->keep_alive ? "keep-alive" : "close");
    
    http_srv_cln_tx(p_user, header, offset);

    http_srv_cln_tx(p_user, buffer, buflen);
}

BOOL httpd_auth_handler(HTTPD_CLASS * httpd, HTTPCLN * p_user, HTTPMSG * rx_msg, char * method)
{
    BOOL ret;
    BOOL auth = FALSE;
    BOOL need_auth = FALSE;
    BOOL user_exist = FALSE;
    char password[100] = {'\0'};
    HD_AUTH_INFO auth_info;

    memset(&auth_info, 0, sizeof(auth_info));

    ret = http_get_auth_digest_info(rx_msg, &auth_info);
    
    if (httpd->on_auth)
    {
        httpd->on_auth(auth_info.auth_name, &need_auth, &user_exist, password, httpd->userdata);
    }
    else
    {
        return TRUE;
    }

    if (!need_auth)
    {
        return TRUE;
    }
    else if (!user_exist)
    {
        return FALSE;
    }
    
    // check http digest auth information
    
    if (ret)
    {
        auth = digest_auth_process(&auth_info, &httpd->auth_info, method, password);
    }

    return auth;
}

void httpd_process_request(HTTPD_CLASS * httpd, HTTPCLN * p_user, HTTPMSG * rx_msg)
{
    int  len;
    char method[256] = {'\0'}, path[512] = {'\0'}, protocol[100] = {'\0'};
    char filename[512] = {'\0'}, filepath[1000] = {'\0'}, * query, * file;
    mime_handler *mhandler;
    func_handler *fhandler;
    
    if (sscanf(rx_msg->msg_buf, "%[^ ] %[^ ] %[^ ]", method, path, protocol) != 3) 
    {
        httpd_send_error(p_user, 400, "Bad Request", NULL, "Can't parse request.");
        return;
    }

    if (strcasecmp(method, "get") != 0 && strcasecmp(method, "post") != 0) 
    {
        httpd_send_error(p_user, 501, "Not Implemented", NULL, "That method is not implemented.");
        return;
    }
    
    if (path[0] != '/') 
    {
        httpd_send_error(p_user, 400, "Bad Request", NULL, "Bad filename.");
        return;
    }

    file = &(path[1]);    
    len = (int)strlen(file);

    if (file[0] == '/' || 
        strcmp(file, "..") == 0 || 
        strncmp(file, "../", 3) == 0 || 
        strstr(file, "/../") != NULL) 
    {
        httpd_send_error(p_user, 400, "Bad Request", NULL, "Illegal filename.");
        return;
    }

    if (file[0] == '\0' || file[len-1] == '/')
    {
        snprintf(&file[len], sizeof(path) - len - 1, DEFFILE);
    }
    
    path[sizeof(path) - 1] = '\0';

    if ((query = strstr(file, "?")) != NULL) 
    {
        len = (int)(query - file);

        if (len >= (int)sizeof(filename))
        {
            len = sizeof(filename) - 1;
        }
        
        strncpy(filename, file, len);
    } 
    else
    {
        strncpy(filename, file, sizeof(filename)-1);
    }

    log_print(HT_LOG_DBG, "%s, filename = %s\r\n", __FUNCTION__, filename);

    // do function output
    
    for (fhandler = &func_handlers[0]; fhandler->func_name; fhandler++) 
    {
        if (strcasecmp(fhandler->func_name, filename) == 0) 
        {
            httpd_do_func_output(p_user, fhandler);
            return;
        }
    }

    if (NULL == httpd)
    {
        httpd_send_error(p_user, 500, "Internal error", NULL, "Internal error");
        return;
    }

    // convert vpath to filename
    
    LKNODE * p_node = hlist_lookup_start(httpd->vpath_list);
    while (p_node)
    {
        vpath_file * p_vpath = (vpath_file *) p_node->data;
        
        if (strncasecmp(p_vpath->vpath, filename, strlen(p_vpath->vpath)) == 0)
        {
            strcpy(filename, p_vpath->filename);
            break;
        }
        
        p_node = hlist_lookup_next(httpd->vpath_list, p_node);
    }
    hlist_lookup_end(httpd->vpath_list);

    // do file output
    
    for (mhandler = &mime_handlers[0]; mhandler->pattern; mhandler++) 
    {
        if (httpd_match(mhandler->pattern, filename)) 
        {
            if (mhandler->auth) 
            {
                if (!httpd_auth_handler(httpd, p_user, rx_msg, method))
                {
                    httpd_send_authenticate(httpd, p_user);
                    return;
                }
            }
            
            snprintf(filepath, sizeof(filepath)-1, "%s/%s", httpd->root_path, filename);
            
            httpd_do_file_output(p_user, filepath, mhandler);
            break;
        }
    }

    if (NULL == mhandler->pattern)
    {
        httpd_send_error(p_user, 404, "Not Found", NULL, "File not found.");
    }
}

HTTPD_CLASS * httpd_init()
{
    HTTPD_CLASS * httpd = (HTTPD_CLASS *) malloc(sizeof(HTTPD_CLASS));
    if (NULL == httpd)
    {
        log_print(HT_LOG_ERR, "%s, malloc failed\r\n", __FUNCTION__);
        return NULL;
    }

    memset(httpd, 0, sizeof(HTTPD_CLASS));
    
    httpd->vpath_list = hlist_create(TRUE);
    if (NULL == httpd->vpath_list)
    {
        free(httpd);
        log_print(HT_LOG_ERR, "%s, h_list_create failed\r\n", __FUNCTION__);
        return FALSE;
    }

    // Set default httpd root path
    strcpy(httpd->root_path, WEBSPATH);

    return httpd;
}

void httpd_deinit(HTTPD_CLASS * httpd)
{
    if (httpd)
    {
        if (httpd->vpath_list)
        {
            hlist_free_container(httpd->vpath_list);
            httpd->vpath_list = NULL;
        }

        free(httpd);
    }
}

BOOL httpd_add_vpath_file(HTTPD_CLASS * httpd, const char * vpath, const char * filename)
{
    if (NULL == httpd || NULL == httpd->vpath_list)
    {
        return FALSE;
    }

    if (NULL == vpath || NULL == filename)
    {
        return FALSE;
    }

    vpath_file * p_node = (vpath_file *) malloc(sizeof(vpath_file));
    if (NULL == p_node)
    {
        log_print(HT_LOG_ERR, "%s, malloc failed\r\n", __FUNCTION__);
        return FALSE;
    }
    
    memset(p_node, 0, sizeof(vpath_file));

    snprintf(p_node->vpath, sizeof(p_node->vpath)-1, "%s", vpath);
    snprintf(p_node->filename, sizeof(p_node->filename)-1, "%s", filename);

    return hlist_add_at_back(httpd->vpath_list, (void *) p_node);
}

void httpd_remove_vpath_file(HTTPD_CLASS * httpd, const char * vpath)
{
    if (NULL == httpd || NULL == httpd->vpath_list)
    {
        return;
    }

    if (NULL == vpath)
    {
        return;
    }

    LKNODE * p_node = hlist_lookup_start(httpd->vpath_list);
    while (p_node)
    {
        vpath_file * p_vpath = (vpath_file *) p_node->data;
        
        if (strcasecmp(p_vpath->vpath, vpath) == 0)
        {
            free(p_node->data);
            
            hlist_remove(httpd->vpath_list, p_node);
            break;
        }
        
        p_node = hlist_lookup_next(httpd->vpath_list, p_node);
    }
    hlist_lookup_end(httpd->vpath_list);
}

void httpd_set_root_path(HTTPD_CLASS * httpd, const char * path)
{
    if (httpd)
    {
        strncpy(httpd->root_path, path, sizeof(httpd->root_path)-1);
    }
}

void httpd_set_on_auth(HTTPD_CLASS * httpd, httpd_on_auth on_auth, void * userdata)
{
    if (httpd)
    {
        httpd->on_auth = on_auth;
        httpd->userdata = userdata;
    }
}

#endif


