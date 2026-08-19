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
#include "rfc_md5.h"
#include "sha256.h"
#include "http_auth.h"


/***************************************************************************************/

/* calculate H(A1) as per spec */
void md5_digest_calc_ha1
(
const char * algorithm,
const char * username,
const char * realm,
const char * password,
const char * nonce,
const char * cnonce,
char session_key[33]
)
{
    uint8 ha1[16];
    md5_context ctx;

    md5_starts(&ctx);
    md5_update(&ctx, (uint8 *)username, (uint32)strlen(username));
    md5_update(&ctx, (uint8 *)(":"), 1);
    md5_update(&ctx, (uint8 *)realm, (uint32)strlen(realm));
    md5_update(&ctx, (uint8 *)(":"), 1);
    md5_update(&ctx, (uint8 *)password, (uint32)strlen(password));
    md5_finish(&ctx, ha1);

    if (strcasecmp(algorithm, "md5-sess") == 0) 
    {
        md5_starts(&ctx);
        md5_update(&ctx, ha1, 16);
        md5_update(&ctx, (uint8 *)(":"), 1);
        md5_update(&ctx, (uint8 *)nonce, (uint32)strlen(nonce));
        md5_update(&ctx, (uint8 *)(":"), 1);
        md5_update(&ctx, (uint8 *)cnonce, (uint32)strlen(cnonce));
        md5_finish(&ctx, ha1);
    };

    bin_to_hex_str(ha1, 16, session_key, 33);
};

/* calculate request-digest/response-digest as per HTTP Digest spec */
void md5_digest_calc_response_hash
(
char ha1[33],               /* H(A1) */
const char * nonce,         /* nonce from server */
const char * nonce_count,   /* 8 hex digits */
const char * cnonce,        /* client nonce */
const char * qop,           /* qop-value: "", "auth", "auth-int" */
const char * method,        /* method from the request */
const char * uri,           /* requested URL */
char  entity[33],           /* H(entity body) if qop="auth-int" */
uint8 hash[16]              /* request-digest or response-digest */
)
{
    uint8 ha2[16];
    char ha2hex[33];
    md5_context ctx;

    // calculate H(A2)
    md5_starts(&ctx);
    md5_update(&ctx, (uint8 *)method, (uint32)strlen(method));
    md5_update(&ctx, (uint8 *)(":"), 1);
    md5_update(&ctx, (uint8 *)uri, (uint32)strlen(uri));

    if (strcmp(qop, "auth-int") == 0) 
    {
        md5_update(&ctx, (uint8 *)(":"), 1);
        md5_update(&ctx, (uint8 *)(&entity[0]), 33);
    };
    
    md5_finish(&ctx, ha2);
    
    bin_to_hex_str(ha2, 16, ha2hex, 33);

    // calculate response
    md5_starts(&ctx);
    md5_update(&ctx, (uint8 *)(&ha1[0]), 32);
    md5_update(&ctx, (uint8 *)(":"), 1);
    md5_update(&ctx, (uint8 *)nonce, (uint32)strlen(nonce));
    md5_update(&ctx, (uint8 *)(":"), 1);

    if (*qop) 
    {
        md5_update(&ctx, (uint8 *)nonce_count, (uint32)strlen(nonce_count));
        md5_update(&ctx, (uint8 *)(":"), 1);
        md5_update(&ctx, (uint8 *)cnonce, (uint32)strlen(cnonce));
        md5_update(&ctx, (uint8 *)(":"), 1);
        md5_update(&ctx, (uint8 *)qop, (uint32)strlen(qop));
        md5_update(&ctx, (uint8 *)(":"), 1);
    };
    
    md5_update(&ctx, (uint8 *)(&ha2hex[0]), 32);
    md5_finish(&ctx, hash);
}

/* calculate request-digest/response-digest as per HTTP Digest spec */
void md5_digest_calc_response
(
char ha1[33],               /* H(A1) */
const char * nonce,         /* nonce from server */
const char * nonce_count,   /* 8 hex digits */
const char * cnonce,        /* client nonce */
const char * qop,           /* qop-value: "", "auth", "auth-int" */
const char * method,        /* method from the request */
const char * uri,           /* requested URL */
char entity[33],            /* H(entity body) if qop="auth-int" */
char response[33]           /* request-digest or response-digest */
)
{
    uint8 hash[16];
    
    md5_digest_calc_response_hash(ha1, nonce, nonce_count, cnonce, qop, method, uri, entity, hash);

    bin_to_hex_str(hash, 16, response, 33);
}

/* calculate H(A1) as per spec */
void sha256_digest_calc_ha1
(
const char * algorithm,
const char * username,
const char * realm,
const char * password,
const char * nonce,
const char * cnonce,
char session_key[65]
)
{
    uint8 ha1[32];
    sha256_context ctx;
    
    sha256_starts(&ctx);
    sha256_update(&ctx, (uint8 *)username, (uint32)strlen(username));
    sha256_update(&ctx, (uint8 *)(":"), 1);
    sha256_update(&ctx, (uint8 *)realm, (uint32)strlen(realm));
    sha256_update(&ctx, (uint8 *)(":"), 1);
    sha256_update(&ctx, (uint8 *)password, (uint32)strlen(password));
    sha256_finish(&ctx, ha1);

    if (strcasecmp(algorithm, "sha-256-sess") == 0) 
    {
        sha256_starts(&ctx);
        sha256_update(&ctx, ha1, 32);
        sha256_update(&ctx, (uint8 *)(":"), 1);
        sha256_update(&ctx, (uint8 *)nonce, (uint32)strlen(nonce));
        sha256_update(&ctx, (uint8 *)(":"), 1);
        sha256_update(&ctx, (uint8 *)cnonce, (uint32)strlen(cnonce));
        sha256_finish(&ctx, ha1);
    };

    bin_to_hex_str(ha1, 32, session_key, 65);
}

/* calculate request-digest/response-digest as per HTTP Digest spec */
void sha256_digest_calc_response_hash
(
char ha1[65],               /* H(A1) */
const char * nonce,         /* nonce from server */
const char * nonce_count,   /* 8 hex digits */
const char * cnonce,        /* client nonce */
const char * qop,           /* qop-value: "", "auth", "auth-int" */
const char * method,        /* method from the request */
const char * uri,           /* requested URL */
char  entity[65],           /* H(entity body) if qop="auth-int" */
uint8 hash[32]              /* request-digest or response-digest */
)
{
    uint8 ha2[32];
    char ha2hex[65];
    sha256_context ctx;
    
    // calculate H(A2)
    sha256_starts(&ctx);
    sha256_update(&ctx, (uint8 *)method, (uint32)strlen(method));
    sha256_update(&ctx, (uint8 *)(":"), 1);
    sha256_update(&ctx, (uint8 *)uri, (uint32)strlen(uri));

    if (strcmp(qop, "auth-int") == 0)
    {
        sha256_update(&ctx, (uint8 *)(":"), 1);
        sha256_update(&ctx, (uint8 *)(&entity[0]), 64);
    };

    sha256_finish(&ctx, ha2);
    
    bin_to_hex_str(ha2, 32, ha2hex, 65);

    // calculate response
    sha256_starts(&ctx);
    sha256_update(&ctx, (uint8 *)(&ha1[0]), 64);
    sha256_update(&ctx, (uint8 *)(":"), 1);
    sha256_update(&ctx, (uint8 *)nonce, (uint32)strlen(nonce));
    sha256_update(&ctx, (uint8 *)(":"), 1);

    if (*qop)
    {
        sha256_update(&ctx, (uint8 *)nonce_count, (uint32)strlen(nonce_count));
        sha256_update(&ctx, (uint8 *)(":"), 1);
        sha256_update(&ctx, (uint8 *)cnonce, (uint32)strlen(cnonce));
        sha256_update(&ctx, (uint8 *)(":"), 1);
        sha256_update(&ctx, (uint8 *)qop, (uint32)strlen(qop));
        sha256_update(&ctx, (uint8 *)(":"), 1);
    };

    sha256_update(&ctx, (uint8 *)(&ha2hex[0]), 64);
    sha256_finish(&ctx, hash);
}

/* calculate request-digest/response-digest as per HTTP Digest spec */
void sha256_digest_calc_response
(
char ha1[65],               /* H(A1) */
const char * nonce,         /* nonce from server */
const char * nonce_count,   /* 8 hex digits */
const char * cnonce,        /* client nonce */
const char * qop,           /* qop-value: "", "auth", "auth-int" */
const char * method,        /* method from the request */
const char * uri,           /* requested URL */
char entity[65],            /* H(entity body) if qop="auth-int" */
char response[65]           /* request-digest or response-digest */
)
{
    uint8 hash[32];

    sha256_digest_calc_response_hash(ha1, nonce, nonce_count, cnonce, qop, method, uri, entity, hash);
    
    bin_to_hex_str(hash, 32, response, 65);
};

BOOL digest_auth_process
(
HD_AUTH_INFO * p_auth, 
HD_AUTH_INFO * p_lauth, 
const char * method, 
const char * password
)
{
    if (p_auth->auth_algorithm[0] == '\0' || 
        strncasecmp(p_auth->auth_algorithm, "MD5", 3) == 0)
    {
        char ha1[33] = {'\0'};
        char ha2[33] = {'\0'};
        char response[33] = {'\0'};
        
        md5_digest_calc_ha1(p_auth->auth_algorithm, p_auth->auth_name, 
            p_lauth->auth_realm, password, p_auth->auth_nonce, 
            p_auth->auth_cnonce, ha1);
        
        md5_digest_calc_response(ha1, p_lauth->auth_nonce, 
            p_auth->auth_ncstr, p_auth->auth_cnonce, 
            p_auth->auth_qop, method, p_auth->auth_uri, 
            ha2, response);
            
        if (strcasecmp(response, p_auth->auth_response) == 0)
        {
            return TRUE;
        }
    }
    else if (strncasecmp(p_auth->auth_algorithm, "SHA-256", 7) == 0)
    {
        char ha1[65] = {'\0'};
        char ha2[65] = {'\0'};
        char response[65] = {'\0'};
        
        sha256_digest_calc_ha1(p_auth->auth_algorithm, p_auth->auth_name, 
            p_lauth->auth_realm, password, p_auth->auth_nonce, 
            p_auth->auth_cnonce, ha1);
        
        sha256_digest_calc_response(ha1, p_lauth->auth_nonce, 
            p_auth->auth_ncstr, p_auth->auth_cnonce, 
            p_auth->auth_qop, method, p_auth->auth_uri, 
            ha2, response);
            
        if (strcasecmp(response, p_auth->auth_response) == 0)
        {
            return TRUE;
        }
    }
    
    return FALSE;
}





