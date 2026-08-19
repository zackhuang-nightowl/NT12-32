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
#include "base64.h"
#include "onvif_security.h"
#include "onvif_utils.h"
#include "onvif_event.h"

#ifdef SECURITY_SUPPORT

BOOL onvif_save_public_key(RSA *rsa_key, KeyList * p_key) 
{
    BUF_MEM * buf_mem;
    BIO *bio = BIO_new(BIO_s_mem());
    if (!bio) 
    {
        log_print(HT_LOG_ERR, "%s, BIO_new failed, %s\r\n", 
            __FUNCTION__, ERR_reason_error_string(ERR_get_error()));
        return FALSE;
    }

    if (!PEM_write_bio_RSA_PUBKEY(bio, rsa_key)) 
    {
        BIO_free(bio);
        log_print(HT_LOG_ERR, "%s, PEM_write_bio_RSA_PUBKEY failed, %s\r\n", 
            __FUNCTION__, ERR_reason_error_string(ERR_get_error()));
        return FALSE;
    }
    
    BIO_get_mem_ptr(bio, &buf_mem);

    p_key->public_key = (char *) malloc(buf_mem->length+1);
    if (NULL == p_key->public_key)
    {
        BIO_free(bio);
        log_print(HT_LOG_ERR, "%s, malloc failed\r\n", __FUNCTION__);
        return FALSE;
    }
    
    memcpy(p_key->public_key, buf_mem->data, buf_mem->length);
    p_key->public_key[buf_mem->length] = '\0';
    p_key->public_key_length = buf_mem->length;
    
    BIO_free(bio);
    
    return TRUE;
}

BOOL onvif_save_private_key(RSA *rsa_key, KeyList * p_key)
{
    BUF_MEM * buf_mem;
    BIO *bio = BIO_new(BIO_s_mem());
    if (!bio) 
    {
        log_print(HT_LOG_ERR, "%s, BIO_new failed, %s\r\n", 
            __FUNCTION__, ERR_reason_error_string(ERR_get_error()));
        return FALSE;
    }

    if (!PEM_write_bio_RSAPrivateKey(bio, rsa_key, NULL, NULL, 0, NULL, NULL)) 
    {
        BIO_free(bio);
        log_print(HT_LOG_ERR, "%s, PEM_write_bio_RSAPrivateKey failed, %s\r\n", 
            __FUNCTION__, ERR_reason_error_string(ERR_get_error()));
        return FALSE;
    }
    
    BIO_get_mem_ptr(bio, &buf_mem);

    p_key->private_key = (char *) malloc(buf_mem->length+1);
    if (NULL == p_key->private_key)
    {
        BIO_free(bio);
        log_print(HT_LOG_ERR, "%s, malloc failed\r\n", __FUNCTION__);
        return FALSE;
    }
    
    memcpy(p_key->private_key, buf_mem->data, buf_mem->length);
    p_key->private_key[buf_mem->length] = '\0';
    p_key->private_key_length = buf_mem->length;
    
    BIO_free(bio);
    
    return TRUE;
}

BOOL onvif_generate_rsa_keypair(KeyList * p_key, int key_length)
{
    RSA * rsa = NULL;
    BIGNUM * bn = BN_new();

    if (!bn) 
    {
        log_print(HT_LOG_ERR, "%s, BN_new failed, %s\r\n", 
            __FUNCTION__, ERR_reason_error_string(ERR_get_error()));
        return FALSE;
    }
    
    if (BN_set_word(bn, RSA_F4) != 1) 
    {
        BN_free(bn);
        log_print(HT_LOG_ERR, "%s, BN_set_word failed, %s\r\n", 
            __FUNCTION__, ERR_reason_error_string(ERR_get_error()));
        return FALSE;
    }

    rsa = RSA_new();
    if (!rsa) 
    {
        BN_free(bn);
        log_print(HT_LOG_ERR, "%s, RSA_new failed, %s\r\n", 
            __FUNCTION__, ERR_reason_error_string(ERR_get_error()));
        return FALSE;
    }
    
    if (RSA_generate_key_ex(rsa, key_length, bn, NULL) != 1) 
    {
        BN_free(bn);
        RSA_free(rsa);
        log_print(HT_LOG_ERR, "%s, RSA_generate_key_ex failed, %s\r\n", 
            __FUNCTION__, ERR_reason_error_string(ERR_get_error()));
        return FALSE;
    }
    
    BN_free(bn);

    if (!onvif_save_public_key(rsa, p_key))
    {
        RSA_free(rsa);
        return FALSE;
    }

    if (!onvif_save_private_key(rsa, p_key))
    {
        RSA_free(rsa);
        return FALSE;
    }

    RSA_free(rsa);

    return TRUE;
}

BOOL onvif_load_pem_rsa_key(const char * pem_rsa_key, EVP_PKEY * evp_pkey, BOOL private_flag)
{
    BIO * bio = NULL;
    RSA * rsa = NULL;

    bio = BIO_new_mem_buf(pem_rsa_key, -1);
    if (NULL == bio) 
    {
        log_print(HT_LOG_ERR, "%s, BIO_new_mem_buf failed\r\n", __FUNCTION__);
        return FALSE;
    }

    if (private_flag)
    {
        rsa = PEM_read_bio_RSAPrivateKey(bio, NULL, NULL, NULL);
    }
    else
    {
        rsa = PEM_read_bio_RSA_PUBKEY(bio, NULL, NULL, NULL);
    }
    
    if (NULL == rsa)
    {
        BIO_free(bio);
        log_print(HT_LOG_ERR, "%s, read key failed, %s\r\n", 
            __FUNCTION__, ERR_reason_error_string(ERR_get_error()));
        return FALSE;
    }

    EVP_PKEY_assign_RSA(evp_pkey, rsa);
    
    BIO_free(bio);

    return TRUE;
}

const EVP_MD * onvif_get_evp_md_from_oid(const char * oid_str) 
{
    const EVP_MD * evp_md;
    ASN1_OBJECT * oid_obj;

    oid_obj = OBJ_txt2obj(oid_str, 1);
    if (NULL == oid_obj) 
    {
        log_print(HT_LOG_ERR, "%s, OBJ_txt2obj, %s\r\n", 
            __FUNCTION__, ERR_reason_error_string(ERR_get_error()));
        return NULL;
    }

    evp_md = EVP_get_digestbyobj(oid_obj);
    if (NULL == evp_md)
    {
        log_print(HT_LOG_ERR, "%s, EVP_get_digestbyobj, %s\r\n", 
            __FUNCTION__, ERR_reason_error_string(ERR_get_error()));
        return NULL;
    }
    
    return evp_md;
}

BOOL onvif_set_subject(onvif_DistinguishedName * p_name, X509_NAME * subject)
{
    if (p_name->sizeCountry)
    {
        if (!X509_NAME_add_entry_by_txt(subject, "C", MBSTRING_ASC, (unsigned char*)p_name->Country[0], -1, -1, 0))
        {
            return FALSE;
        }
    }

    if (p_name->sizeOrganization)
    {
        if (!X509_NAME_add_entry_by_txt(subject, "O", MBSTRING_ASC, (unsigned char*)p_name->Organization[0], -1, -1, 0))
        {
            return FALSE;
        }
    }

    if (p_name->sizeOrganizationalUnit)
    {
        if (!X509_NAME_add_entry_by_txt(subject, "OU", MBSTRING_ASC, (unsigned char*)p_name->OrganizationalUnit[0], -1, -1, 0))
        {
            return FALSE;
        }
    }

    if (p_name->sizeDistinguishedNameQualifier)
    {
        if (!X509_NAME_add_entry_by_txt(subject, "DNQUALIFIER", MBSTRING_ASC, (unsigned char*)p_name->DistinguishedNameQualifier[0], -1, -1, 0))
        {
            return FALSE;
        }
    }

    if (p_name->sizeStateOrProvinceName)
    {
        if (!X509_NAME_add_entry_by_txt(subject, "ST", MBSTRING_ASC, (unsigned char*)p_name->StateOrProvinceName[0], -1, -1, 0))
        {
            return FALSE;
        }
    }

    if (p_name->sizeCommonName)
    {
        if (!X509_NAME_add_entry_by_txt(subject, "CN", MBSTRING_ASC, (unsigned char*)p_name->CommonName[0], -1, -1, 0))
        {
            return FALSE;
        }
    }

    if (p_name->sizeSerialNumber)
    {
        if (!X509_NAME_add_entry_by_txt(subject, "SERIALNUMBER", MBSTRING_ASC, (unsigned char*)p_name->SerialNumber[0], -1, -1, 0))
        {
            return FALSE;
        }
    }

    if (p_name->sizeLocality)
    {
        if (!X509_NAME_add_entry_by_txt(subject, "L", MBSTRING_ASC, (unsigned char*)p_name->Locality[0], -1, -1, 0))
        {
            return FALSE;
        }
    }

    if (p_name->sizeTitle)
    {
        if (!X509_NAME_add_entry_by_txt(subject, "T", MBSTRING_ASC, (unsigned char*)p_name->Title[0], -1, -1, 0))
        {
            return FALSE;
        }
    }

    if (p_name->sizeSurname)
    {
        if (!X509_NAME_add_entry_by_txt(subject, "SN", MBSTRING_ASC, (unsigned char*)p_name->Surname[0], -1, -1, 0))
        {
            return FALSE;
        }
    }

    if (p_name->sizeGivenName)
    {
        if (!X509_NAME_add_entry_by_txt(subject, "G", MBSTRING_ASC, (unsigned char*)p_name->GivenName[0], -1, -1, 0))
        {
            return FALSE;
        }
    }

    if (p_name->sizeInitials)
    {
        if (!X509_NAME_add_entry_by_txt(subject, "I", MBSTRING_ASC, (unsigned char*)p_name->Initials[0], -1, -1, 0))
        {
            return FALSE;
        }
    }

    if (p_name->sizePseudonym)
    {
        if (!X509_NAME_add_entry_by_txt(subject, "PSEUDONYM", MBSTRING_ASC, (unsigned char*)p_name->Pseudonym[0], -1, -1, 0))
        {
            return FALSE;
        }
    }

    if (p_name->sizeGenerationQualifier)
    {
        if (!X509_NAME_add_entry_by_txt(subject, "GENERATIONQUALIFIER", MBSTRING_ASC, (unsigned char*)p_name->GenerationQualifier[0], -1, -1, 0))
        {
            return FALSE;
        }
    }

    if (p_name->anyAttributeFlag)
    {
        if (p_name->anyAttribute.sizeDomainComponent)
        {
            if (!X509_NAME_add_entry_by_txt(subject, "DC", MBSTRING_ASC, (unsigned char*)p_name->anyAttribute.DomainComponent[0], -1, -1, 0))
            {
                return FALSE;
            }
        }
    }

    return TRUE;
}

EVP_PKEY * onvif_get_evp_pkey(const char * public_key, const char * private_key)
{
    EVP_PKEY * evp_pkey = EVP_PKEY_new();
    if (NULL == evp_pkey)
    {
        return NULL;
    }
    
    if (!onvif_load_pem_rsa_key(public_key, evp_pkey, FALSE))
    {
        EVP_PKEY_free(evp_pkey);
        return NULL;
    }

    if (!onvif_load_pem_rsa_key(private_key, evp_pkey, TRUE))
    {
        EVP_PKEY_free(evp_pkey);
        return NULL;
    }

    return evp_pkey;
}

BOOL onvif_get_x509_req_base64_buff(X509_REQ * cert, char ** buff, int * size)
{
    int der_size;
    int base64_size;
    char * base64;
    uint8 * der = NULL;
    
    der_size = i2d_X509_REQ(cert, &der);
    if (der_size < 0)
    {
        log_print(HT_LOG_ERR, "%s, Error converting to DER, %s\r\n",
            __FUNCTION__, ERR_reason_error_string(ERR_get_error()));
        return FALSE;
    }

    base64 = (char *) malloc(2 * der_size);
    if (NULL == base64)
    {
        OPENSSL_free(der);
        log_print(HT_LOG_ERR, "%s, malloc failed\r\n", __FUNCTION__);
        return FALSE;
    }

    base64_size = base64_encode(der, der_size, base64, 2*der_size);
    if (base64_size <= 0)
    {
        OPENSSL_free(der);
        free(base64);
        log_print(HT_LOG_ERR, "%s, base64_encode failed\r\n", __FUNCTION__);
        return FALSE;
    }

    OPENSSL_free(der);

    *buff = base64;
    *size = base64_size;

    return TRUE;
}

BOOL onvif_get_x509_base64_buff(X509 * cert, char ** buff, int * size)
{
    int der_size;
    int base64_size;
    char * base64;
    uint8 * der = NULL;
    
    der_size = i2d_X509(cert, &der);
    if (der_size < 0)
    {
        log_print(HT_LOG_ERR, "%s, Error converting to DER, %s\r\n", 
            __FUNCTION__, ERR_reason_error_string(ERR_get_error()));
        return FALSE;
    }

    base64 = (char *) malloc(2 * der_size);
    if (NULL == base64)
    {
        OPENSSL_free(der);
        log_print(HT_LOG_ERR, "%s, malloc failed\r\n", __FUNCTION__);
        return FALSE;
    }

    base64_size = base64_encode(der, der_size, base64, 2*der_size);
    if (base64_size <= 0)
    {
        OPENSSL_free(der);
        free(base64);
        log_print(HT_LOG_ERR, "%s, base64_encode failed\r\n", __FUNCTION__);
        return FALSE;
    }

    OPENSSL_free(der);

    *buff = base64;
    *size = base64_size;

    return TRUE;
}

BOOL onvif_create_pkcs10_der(X509_NAME *subject, EVP_PKEY *key, const EVP_MD *md, char **buff, int *size)
{
    X509_REQ *req = NULL;

    req = X509_REQ_new();
    if (NULL == req) 
    {
        log_print(HT_LOG_ERR, "%s, Error creating X509_REQ object, %s\r\n", 
            __FUNCTION__, ERR_reason_error_string(ERR_get_error()));
        return FALSE;
    }

    if (!X509_REQ_set_version(req, 2)) 
    {
        X509_REQ_free(req);
        log_print(HT_LOG_ERR, "%s, Error setting version, %s\r\n", 
            __FUNCTION__, ERR_reason_error_string(ERR_get_error()));
        return FALSE;
    }
    
    if (!X509_REQ_set_subject_name(req, subject)) 
    {
        X509_REQ_free(req);
        log_print(HT_LOG_ERR, "%s, Error setting subject name, %s\r\n", 
            __FUNCTION__, ERR_reason_error_string(ERR_get_error()));
        return FALSE;
    }

    if (!X509_REQ_set_pubkey(req, key)) 
    {
        X509_REQ_free(req);
        log_print(HT_LOG_ERR, "%s, Error setting public key, %s\r\n", 
            __FUNCTION__, ERR_reason_error_string(ERR_get_error()));
        return FALSE;
    }

    if (!X509_REQ_sign(req, key, md)) 
    {
        X509_REQ_free(req);
        log_print(HT_LOG_ERR, "%s, Error signing request, %s\r\n", 
            __FUNCTION__, ERR_reason_error_string(ERR_get_error()));
        return FALSE;
    }

    if (!onvif_get_x509_req_base64_buff(req, buff, size))
    {
        X509_REQ_free(req);
        log_print(HT_LOG_ERR, "%s, onvif_get_x509_req_base64_buff failed\r\n", __FUNCTION__);
        return FALSE;
    }
    
    X509_REQ_free(req);
    
    return TRUE;
}

ASN1_TIME * onvif_time_t_to_asn1_time(time_t t) 
{
    char buf[32];
    struct tm *tm_info;
    ASN1_TIME *asn1_time = NULL;

    tm_info = gmtime(&t);
    if (tm_info == NULL) 
    {
        return NULL;
    }

    if (strftime(buf, sizeof(buf), "%Y%m%d%H%M%SZ", tm_info) == 0) 
    {
        return NULL;
    }

    asn1_time = ASN1_TIME_new();
    if (asn1_time == NULL) 
    {
        return NULL;
    }

    if (ASN1_TIME_set_string(asn1_time, buf) != 1) 
    {
        ASN1_TIME_free(asn1_time);
        return NULL;
    }

    return asn1_time;
}

BOOL onvif_x509_set_rand_sn(X509 *cert)
{
    BIGNUM * bn;
    ASN1_INTEGER * sn;

    bn = BN_new();
    if (NULL == bn)
    {
        return FALSE;
    }
    
    BN_rand(bn, 160, -1, 0);
    
    sn = ASN1_INTEGER_new();
    
    BN_to_ASN1_INTEGER(bn, sn);
    
    X509_set_serialNumber(cert, sn);
    
    BN_free(bn);
    ASN1_INTEGER_free(sn);

    return TRUE;
}

BOOL onvif_create_self_singed_cert(tas_CreateSelfSignedCertificate_REQ * p_req, X509_NAME *subject, EVP_PKEY *key, const EVP_MD *md, char **buff, int *size)
{
    int version;
    X509 *cert = NULL;
    X509V3_CTX ctx;
    X509_EXTENSION *ex;

    cert = X509_new();
    if (NULL == cert) 
    {
        log_print(HT_LOG_ERR, "%s, Error creating X509 object, %s\r\n", 
            __FUNCTION__, ERR_reason_error_string(ERR_get_error()));
        return FALSE;
    }

    if (p_req->X509VersionFlag)
    {
        version = p_req->X509Version;
    }
    else
    {
        version = 3;
    }
    
    if (X509_set_version(cert, version - 1) != 1) 
    {
        X509_free(cert);
        log_print(HT_LOG_ERR, "%s, Error setting certificate version, %s\r\n", 
            __FUNCTION__, ERR_reason_error_string(ERR_get_error()));
        return FALSE;
    }

    if (!onvif_x509_set_rand_sn(cert))
    {
        X509_free(cert);
        log_print(HT_LOG_ERR, "%s, onvif_x509_set_rand_sn failed\r\n", __FUNCTION__);
        return FALSE;
    }

    if (p_req->notValidBeforeFlag)
    {
        ASN1_TIME * t = onvif_time_t_to_asn1_time(p_req->notValidBefore);
        if (t)
        {
            X509_set1_notBefore(cert, t);
            ASN1_TIME_free(t);
        }
    }
    else
    {
        if (!X509_gmtime_adj(X509_getm_notBefore(cert), 0)) 
        {
            X509_free(cert);
            log_print(HT_LOG_ERR, "%s, Error setting notBefore, %s\r\n", 
                __FUNCTION__, ERR_reason_error_string(ERR_get_error()));
            return FALSE;
        }
    }

    if (p_req->notValidAfterFlag)
    {
        ASN1_TIME * t = onvif_time_t_to_asn1_time(p_req->notValidAfter);
        if (t)
        {
            X509_set1_notAfter(cert, t);
            ASN1_TIME_free(t);
        }
    }
    else
    {
        ASN1_TIME * t = ASN1_TIME_new();
        if (t) 
        {
            if (ASN1_TIME_set_string(t, "99991231235959Z") == 1) 
            {
                X509_set1_notAfter(cert, t);
            }

            ASN1_TIME_free(t);
        }
    }

    if (!X509_set_pubkey(cert, key)) 
    {
        X509_free(cert);
        log_print(HT_LOG_ERR, "%s, Error setting public key, %s\r\n", 
            __FUNCTION__, ERR_reason_error_string(ERR_get_error()));
        return FALSE;
    }

    if (!X509_set_subject_name(cert, subject) || !X509_set_issuer_name(cert, subject)) 
    {
        X509_free(cert);
        log_print(HT_LOG_ERR, "%s, Error setting subject/issuer, %s\r\n", 
            __FUNCTION__, ERR_reason_error_string(ERR_get_error()));
        return FALSE;
    }

    X509V3_set_ctx_nodb(&ctx);
    X509V3_set_ctx(&ctx, cert, cert, NULL, NULL, 0);

    ex = X509V3_EXT_conf_nid(NULL, &ctx, NID_basic_constraints, "critical,CA:TRUE");
    if (!ex || !X509_add_ext(cert, ex, -1)) 
    {
        X509_free(cert);
        log_print(HT_LOG_ERR, "%s, Error adding basic constraints, %s\r\n", 
            __FUNCTION__, ERR_reason_error_string(ERR_get_error()));
        return FALSE;
    }
    
    X509_EXTENSION_free(ex);

    ex = X509V3_EXT_conf_nid(NULL, &ctx, NID_key_usage, "critical,digitalSignature,keyEncipherment");
    if (!ex || !X509_add_ext(cert, ex, -1))
    {
        X509_free(cert);
        log_print(HT_LOG_ERR, "%s, Error adding key usage, %s\r\n", 
            __FUNCTION__, ERR_reason_error_string(ERR_get_error()));
        return FALSE;
    }
    
    X509_EXTENSION_free(ex);

    if (!X509_sign(cert, key, md)) 
    {
        X509_free(cert);
        log_print(HT_LOG_ERR, "%s, Error signing certificate, %s\r\n", 
            __FUNCTION__, ERR_reason_error_string(ERR_get_error()));
        return FALSE;
    }

    if (!onvif_get_x509_base64_buff(cert, buff, size))
    {
        X509_free(cert);
        log_print(HT_LOG_ERR, "%s, onvif_get_x509_der_base64_buff failed\r\n", __FUNCTION__);
        return FALSE;
    }
    
    X509_free(cert);
    
    return TRUE;
}

BOOL onvif_get_pem_public_key(EVP_PKEY * pkey, char ** buffer, int * size)
{
    int buff_size;
    char * buff;
    BIO * bio;

    bio = BIO_new(BIO_s_mem());
    if (!bio) 
    {
        return FALSE;
    }

    if (!PEM_write_bio_PUBKEY(bio, pkey)) 
    {
        BIO_free(bio);
        return FALSE;
    }

    buff_size = BIO_pending(bio);
    if (buff_size <= 0)
    {
        BIO_free(bio);
        return FALSE;
    }
    
    buff = (char *)malloc(buff_size+1);
    if (NULL == buff) 
    {
        BIO_free(bio);
        return FALSE;
    }

    BIO_read(bio, buff, buff_size);
    BIO_free(bio);

    buff[buff_size] = '\0';

    *buffer = buff;
    *size = buff_size;
    
    return TRUE;
}

int onvif_password_callback(char *buf, int size, int rwflag, void *userdata)
{
    const char * password = (const char *) userdata;
    strncpy(buf, password, size);
    buf[size - 1] = '\0';
    return strlen(buf);
}

BOOL onvif_load_certification_path(onvif_CertificationPath * p_certpath)
{
    BOOL ret = FALSE;
    int der_size;
    uint8 * der_cert = NULL;
    const uint8 * p;
    X509 * x509_cert = NULL;
    SSL_CTX * ssl_ctx = NULL;
    EVP_PKEY * evp_pkey = NULL;
    KeyList * p_key;
    CertificateList * p_cert;
    
    p_cert = onvif_find_Certificate(g_onvif_cfg.certificates, p_certpath->CertificateID[0]);
    if (NULL == p_cert)
    {
        return  FALSE;
    }
    
    der_cert = (uint8 *) malloc(p_cert->Certificate.CertificateContent.size);
    if (NULL == der_cert)
    {
        return  FALSE;
    }

    der_size = base64_decode(p_cert->Certificate.CertificateContent.ptr, p_cert->Certificate.CertificateContent.size, 
                             der_cert, p_cert->Certificate.CertificateContent.size);
    if (der_size <= 0)
    {
        goto cleanup;
    }

    p = der_cert;
    x509_cert = d2i_X509(NULL, &p, der_size);
    if (NULL == x509_cert)
    {
        goto cleanup;
    }

    p_key = onvif_find_Key(g_onvif_cfg.keys, p_cert->Certificate.KeyID);
    if (NULL == p_key)
    {
        goto cleanup;
    }

    evp_pkey = onvif_get_evp_pkey(p_key->public_key, p_key->private_key);
    if (NULL == evp_pkey)
    {
        goto cleanup;
    }

    ssl_ctx = SSL_CTX_new(TLS_server_method());
    if (NULL == ssl_ctx)
    {
        goto cleanup;
    }

    SSL_CTX_set_options(ssl_ctx, SSL_OP_ALL);
    SSL_CTX_set_default_verify_paths(ssl_ctx);
    
    if (SSL_CTX_use_certificate(ssl_ctx, x509_cert) <= 0)
    {
        goto cleanup;
    }
    
    if (SSL_CTX_use_PrivateKey(ssl_ctx, evp_pkey) <= 0)
    {
        goto cleanup;
    }
    
    if (!SSL_CTX_check_private_key(ssl_ctx))
    {
        goto cleanup;
    }

    if (NULL == g_onvif_cls.https_srv.ssl_ctx)
    {
        goto cleanup;
    }

    SSL_CTX_free((SSL_CTX *)g_onvif_cls.https_srv.ssl_ctx);
    
    g_onvif_cls.https_srv.ssl_ctx = ssl_ctx;

    ret = TRUE;

cleanup:

    if (x509_cert)
    {
        X509_free(x509_cert);
    }

    if (der_cert)
    {
        free(der_cert);
    }

    if (evp_pkey)
    {
        EVP_PKEY_free(evp_pkey);
    }

    if (!ret && ssl_ctx)
    {
        SSL_CTX_free(ssl_ctx);
    }
    
    return ret;
}

/**
 * @brief
 *  A device that indicates support for key handling via the MaximumNumberOfKeys capability shall provide infor
 *   mation about key status changes through key status events.
 *  A device shall include optional item OldStatus unless NewStatus is generating
 *
 **/
void onvif_KeyStatus_Notify(const char * KeyID, const char * oldstatus, const char * newstatus)
{
    char str[100] = {'\0'};
    NotificationMessageList * p_message;

    onvif_format_datetime_str(time(NULL), 1, "%Y-%m-%dT%H:%M:%SZ", str, sizeof(str));
    
    p_message = onvif_init_NotificationMessage3(
        "tns1:Advancedsecurity/Keystore/KeyStatus", 
        PropertyOperation_Changed, "KeyID", KeyID, NULL, NULL, 
        "OldStatus", oldstatus, "NewStatus", newstatus);
    if (p_message)
    {
        onvif_put_NotificationMessage(p_message);
    }
}

/**
 * @brief
 *  This operation triggers the asynchronous generation of an RSA key pair of a particular keylength (specified as
 *  the number of bits) as specified in RFC 3447, with a suitable key generation mechanism on the device. Keys,
 *  especially RSA key pairs, are uniquely identified using key IDs.
 *
 *  If the device does not have enough storage capacity for storing the key pair to be created, the maximum number
 *  of keys reached fault shall be produced and no key pair shall be generated. Otherwise, the operation generates
 *  a keyID for the new key and associates the generating status to it.
 *
 *  The device also returns a best-effort estimate of how much time it requires to create the key pair. A client
 *  may use this information as an indication how long to wait before querying the device whether key generation
 *  is completed.
 *
 *  After the key has been successfully created, the device shall assign it the ok status. If the key generation fails,
 *  the device shall assign the key the corrupt status.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_MaximumNumberOfKeysReached
 *  ONVIF_ERR_KeyLength
 **/
ONVIF_RET onvif_tas_CreateRSAKeyPair(tas_CreateRSAKeyPair_REQ * p_req, tas_CreateRSAKeyPair_RES * p_res)
{
    KeyList * p_key;

    if (p_req->KeyLength != 1024 && p_req->KeyLength != 2048)
    {
        return ONVIF_ERR_KeyLength;
    }
    
    p_key = onvif_add_Key(&g_onvif_cfg.keys);
    if (NULL == p_key)
    {
        return ONVIF_ERR_MaximumNumberOfKeysReached;
    }

    if (p_req->AliasFlag)
    {
        p_key->KeyAttribute.AliasFlag = 1;
        strcpy(p_key->KeyAttribute.Alias, p_req->Alias);
    }

    if (!onvif_generate_rsa_keypair(p_key, p_req->KeyLength))
    {
        strcpy(p_key->KeyAttribute.KeyStatus, "corrupt");
    }
    else
    {
        p_key->KeyAttribute.hasPrivateKey = 1;
        strcpy(p_key->KeyAttribute.KeyStatus, "ok");
    }
    
    strcpy(p_res->KeyID, p_key->KeyAttribute.KeyID);
    p_res->EstimatedCreationTime = 1;

    onvif_KeyStatus_Notify(p_res->KeyID, "generating", p_key->KeyAttribute.KeyStatus);
    
    return ONVIF_OK;
}

/**
 * @brief
 *  Triggers the asynchronous generation of an ECC key pair using a particular elliptic curve as
 *  specified in RFC 8422, with a suitable key generation mechanism on the device. Keys, especially ECC key
 *  pairs, are uniquely identified using key IDs.
 *
 *  If the device does not have enough storage capacity for storing the key pair to be created, the maximum number
 *  of keys reached fault shall be produced and no key pair shall be generated. Otherwise, the operation generates
 *  a keyID for the new key and associates the generating status to it. Immediately after key generation has started,
 *  the device shall return the keyID to the client and continue to generate the key pair. The client may query the
 *  device with the GetKeyStatus operation whether the generation has finished. The client
 *  may also subscribe to Key Status events to be notified about key status changes.
 *
 *  The device also returns a best-effort estimate of how much time it requires to create the key pair. A client
 *  may use this information as an indication how long to wait before querying the device whether key generation
 *  is completed.
 *
 *  After the key has been successfully created, the device shall assign it the ok status. If the key generation fails,
 *  the device shall assign the key the corrupt status.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_MaximumNumberOfKeysReached
 *  ONVIF_ERR_UnsupportedEllipticCurve
 **/
ONVIF_RET onvif_tas_CreateECCKeyPair(tas_CreateECCKeyPair_REQ * p_req, tas_CreateECCKeyPair_RES * p_res)
{
    return ONVIF_ERR_Genenal;
}

/**
 * @brief
 *  Uploads a key pair in a PKCS#8 data structure as specified in [RFC 5958, RFC 5959].
 *
 *  If a passphrase is either directly provided or as ID reference to a previously uploaded passphrase, the device
 *  shall assume that the KeyPair parameter contains an EncryptedPrivateKeyInfo ASN.1 structure that is encrypt
 *  ed with the given passphrase. In case neither a passphrase nor a passphrase ID is provided the device shall
 *  assume that the KeyPair parameter contains a OneAsymmetricKey ASN.1 structure which contains both the
 *  private key and the corresponding public key.
 *
 *  If the supplied key pair cannot be processed by the device, the device shall produce an UnsupportedPublicK
 *  eyAlgorithm fault and shall not store the uploaded key pair in the keystore
 *
 *  Key pairs are uniquely identified using key IDs. If a key pair exists in the keystore with the public key equal to
 *  the public key in the request and this key pair does not contain a private key, the device shall add the supplied
 *  private key to the existing key pair and return the ID of this key pair.
 *
 *  If a key pair exists in the keystore with the public key equal to the public key in the request and this key pair
 *  contains a private key, the device shall leave the key pair unchanged and return the ID of this key pair
 *
 *  If the existing key pair does not have status ok, the device shall produce an InvalidKeyStatus fault and shall
 *  not modify the existing key pair.
 *
 *  If no key pair exists in the keystore with the public key equal to the public key in the request, the device shall
 *  generate a new key pair with the supplied private key and the supplied public key, status ok and the externally
 *  generated attribute set to true. Furthermore, the device shall return the ID of this key pair.
 *
 *  If a new key pair is created, the device shall assign the supplied alias to it. Otherwise, the device shall ignore
 *  an eventually supplied alias.
 *
 *  If decryption of the EncryptedPrivateKeyInfo failed, the device shall produce a DecryptionFailed fault and shall
 *  not store the uploaded key pair in the keystore.
 *
 *  If the device does not have enough storage capacity for storing the key pair that eventually has to be created,
 *  the device shall generate a maximum number of keys reached fault. Furthermore the device shall not generate
 *  a key pair.
 *
 *  If no passphrase exists under the ID specified by EncryptionPassphraseID, the device shall produce an invalid
 *  passphrase ID fault and shall not store the uploaded key pair in the keystore
 *
 *  If the supplied PKCS#8 data structure cannot be processed by the device, the device shall produce a BadP
 *  KCS8File fault and shall not store the uploaded key pair in the keystore.
 *
 *  If the public key in the uploaded key pair does not match the uploaded private key, the device shall produce a
 *  PublicPrivateKeyMismatch fault and shall not store the uploaded key pair in the keystore.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_MaximumNumberOfKeysReached
 *  ONVIF_ERR_PassphraseID
 *  ONVIF_ERR_DecryptionFailed
 *  ONVIF_ERR_UnsupportedPublicKeyAlgorithm
 *  ONVIF_ERR_InvalidKeyStatus
 *  ONVIF_ERR_BadPKCS8File
 *  ONVIF_ERR_PublicPrivateKeyMismatch
 **/
ONVIF_RET onvif_tas_UploadKeyPairInPKCS8(tas_UploadKeyPairInPKCS8_REQ * p_req, tas_UploadKeyPairInPKCS8_RES * p_res)
{
    ONVIF_RET ret = ONVIF_OK;
    int pkcs8_len;
    char password[64] = {'\0'};
    uint8 * pkcs8_data = NULL;
    BIO * bio_pkcs8 = NULL;
    EVP_PKEY * pkey = NULL;
    RSA * rsa = NULL;
    KeyList * p_key = NULL;
    
    if (p_req->EncryptionPassphraseIDFlag)
    {
        PassphraseList * p_passphrase = onvif_find_Passphrase(g_onvif_cfg.passphrases, p_req->EncryptionPassphraseID);
        if (NULL == p_passphrase)
        {
            return ONVIF_ERR_PassphraseID;
        }

        strcpy(password, p_passphrase->Passphrase);
    }
    else if (p_req->EncryptionPassphraseFlag)
    {
        strcpy(password, p_req->EncryptionPassphrase);
    }

    pkcs8_data = (uint8 *) malloc(p_req->KeyPair.size+1);
    if (NULL == pkcs8_data)
    {
        return ONVIF_ERR_MaximumNumberOfKeysReached;
    }

    pkcs8_len = base64_decode(p_req->KeyPair.ptr, p_req->KeyPair.size, pkcs8_data, p_req->KeyPair.size);
    if (pkcs8_len <= 0)
    {
        ret = ONVIF_ERR_BadPKCS8File;
        goto cleanup;
    }

    bio_pkcs8 = BIO_new_mem_buf(pkcs8_data, pkcs8_len);
    if (NULL == bio_pkcs8)
    {
        ret = ONVIF_ERR_BadPKCS8File;
        goto cleanup;
    }

    if (password[0] == '\0')
    {
        PKCS8_PRIV_KEY_INFO * p8info = d2i_PKCS8_PRIV_KEY_INFO_bio(bio_pkcs8, NULL);
        if (p8info)
        {
            pkey = EVP_PKCS82PKEY(p8info);

            PKCS8_PRIV_KEY_INFO_free(p8info);
        }
    }
    else
    {
        pkey = d2i_PKCS8PrivateKey_bio(bio_pkcs8, NULL, NULL, (void *)password);
    }

    if (NULL == pkey)
    {
        ret = ONVIF_ERR_DecryptionFailed;
        log_print(HT_LOG_ERR, "%s, Error create evp_pkey obj, %s\r\n", 
            __FUNCTION__, ERR_reason_error_string(ERR_get_error()));
        goto cleanup;
    }

    rsa = EVP_PKEY_get1_RSA(pkey);
    if (NULL == rsa)
    {
        ret = ONVIF_ERR_BadPKCS8File;
        log_print(HT_LOG_ERR, "%s, Error get rsa obj, %s\r\n", 
            __FUNCTION__, ERR_reason_error_string(ERR_get_error()));
        goto cleanup;
    }

    p_key = onvif_add_Key(&g_onvif_cfg.keys);
    if (NULL == p_key)
    {
        ret = ONVIF_ERR_MaximumNumberOfKeysReached;
        goto cleanup;
    }

    if (!onvif_save_public_key(rsa, p_key))
    {
        ret = ONVIF_ERR_MaximumNumberOfKeysReached;
        goto cleanup;
    }

    if (!onvif_save_private_key(rsa, p_key))
    {
        ret = ONVIF_ERR_MaximumNumberOfKeysReached;
        goto cleanup;
    }

    if (p_req->AliasFlag)
    {
        p_key->KeyAttribute.AliasFlag = 1;
        strcpy(p_key->KeyAttribute.Alias, p_req->Alias);
    }

    p_key->KeyAttribute.hasPrivateKey = 1;
    p_key->KeyAttribute.externallyGenerated = 1;
    strcpy(p_key->KeyAttribute.KeyStatus, "ok");

    strcpy(p_res->KeyID, p_key->KeyAttribute.KeyID);
    
cleanup:

    if (pkey)
    {
        EVP_PKEY_free(pkey);
    }

    if (rsa)
    {
        RSA_free(rsa);
    }

    if (pkcs8_data)
    {
        free(pkcs8_data);
    }

    if (bio_pkcs8)
    {
        BIO_free(bio_pkcs8);
    }

    if (ONVIF_OK != ret && p_key)
    {
        onvif_free_Key(&g_onvif_cfg.keys, p_key);
    }

    return ret;
}

/**
 * @brief
 *  Returns the status of a key.
 *
 *  Keys are uniquely identified using key IDs. If no key is stored under the requested key ID in the keystore, an
 *  InvalidKeyID fault is produced. Otherwise, the status of the key is returned
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_KeyID
 **/
ONVIF_RET onvif_tas_GetKeyStatus(tas_GetKeyStatus_REQ * p_req, tas_GetKeyStatus_RES * p_res)
{
    KeyList * p_key = onvif_find_Key(g_onvif_cfg.keys, p_req->KeyID);
    if (NULL == p_key)
    {
        return ONVIF_ERR_KeyID;
    }
    
    strcpy(p_res->KeyStatus, p_key->KeyAttribute.KeyStatus);

    return ONVIF_OK;
}

/**
 * @brief
 *  Returns whether a key pair contains a private key.
 *
 *  Keys are uniquely identified using key IDs. If no key is stored under the requested key ID in the keystore, an
 *  invalid key ID fault shall be produced. If a key is stored under the requested key ID in the keystore, but this key
 *  is not a key pair, an invalid key type fault shall be produced.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_KeyID
 *  ONVIF_ERR_InvalidKeyType
 **/
ONVIF_RET onvif_tas_GetPrivateKeyStatus(tas_GetPrivateKeyStatus_REQ * p_req, tas_GetPrivateKeyStatus_RES * p_res)
{
    KeyList * p_key = onvif_find_Key(g_onvif_cfg.keys, p_req->KeyID);
    if (NULL == p_key)
    {
        return ONVIF_ERR_KeyID;
    }

    if (p_key->private_key && p_key->private_key_length)
    {
        p_res->hasPrivateKey = TRUE;
    }
    else
    {
        p_res->hasPrivateKey = FALSE;
    }

    return ONVIF_OK;
}

/**
 * @brief
 *  Deletes a key from the device¡¯s keystore.
 *
 *  Keys are uniquely identified using key IDs. If no key is stored under the requested key ID in the keystore, a
 *  device shall produce an InvalidArgVal fault. If a reference exists for the specified key, a device shall produce
 *  the corresponding fault and shall not delete the key. If there is a key under the requested key ID stored in the
 *  keystore and the key could not be deleted, a device shall produce a KeyDeletion fault. If the key has the status
 *  generating, a device shall abort the generation of the key and delete from the keystore all data generated for
 *  this key.
 *
 *  After a key is successfully deleted, the device may assign its former ID to other keys.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_KeyDeletionFailed
 *  ONVIF_ERR_KeyID
 *  ONVIF_ERR_ReferenceExists
 **/
ONVIF_RET onvif_tas_DeleteKey(tas_DeleteKey_REQ * p_req)
{
    CertificateList * p_cert;
    KeyList * p_key = onvif_find_Key(g_onvif_cfg.keys, p_req->KeyID);
    if (NULL == p_key)
    {
        return ONVIF_ERR_KeyID;
    }

    p_cert = g_onvif_cfg.certificates;
    while (p_cert)
    {
        if (strcmp(p_cert->Certificate.KeyID, p_key->KeyAttribute.KeyID) == 0)
        {
            return ONVIF_ERR_ReferenceExists;
        }
        
        p_cert = p_cert->next;
    }

    onvif_free_Key(&g_onvif_cfg.keys, p_key);
    
    return ONVIF_OK;
}

/**
 * @brief
 *  Generates a DER-encoded PKCS#10 v1.7 certification request (sometimes also called certificate
 *  signing request or CSR) as specified in RFC 2986 for a public key on the device
 *
 *  The key pair that contains the public key for which a certification request shall be produced is specified by its
 *  key ID. If no key is stored under the requested KeyID or the key specified by the requested KeyID is not an
 *  asymmetric key pair, an invalid key ID fault shall be produced and no CSR shall be generated.
 *
 *  The subject parameter describes the entity that the public key belongs to. Additional attributes can be included
 *  in the attribute parameter.
 *
 *  Distinguished name attribute values shall be supplied either in UTF-8 or in hexadecimal form as specified in
 *  RFC 4514.
 *
 *  If the distinguished name attribute value is supplied in hexadecimal form, the device shall encode the attribute
 *  in the format given in the hexadecimal format.
 *
 *  If the distinguished name attribute value is supplied in UTF-8 and the attribute value has a uniquely defined
 *  encoding (e.g., CountryName is defined as PrintableString), the device shall encode the attribute as the defined
 *  encoding. Otherwise, the device shall encode the attribute value as UTF-8.
 *
 *  The signature algorithm parameter determines which signature algorithm shall be used for signing the certifi
 *  cation request with the public key specified by the key ID parameter. If the specified signature algorithm is not
 *  supported by the device, an UnsupportedSignatureAlgorithm fault shall be produced and no CSR shall be gen
 *  erated. If the public key identified by the requested Key ID is an invalid input to the specified signature algorithm,
 *  a KeySignatureAlgorithmMismatch fault shall be produced and no CSR shall be generated. If the specified
 *  subject is invalid or incomplete, a Subject invalid fault shall be produced and no CSR shall be created. If an
 *  attribute is invalid or incomplete, an Attribute invalid fault shall be produced and no CSR shall be generated.
 *
 *  If the key pair does not have status ok, a device shall produce an InvalidKeyStatus fault and no CSR shall
 *  be generated.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_CSRCreationFailed
 *  ONVIF_ERR_KeyID
 *  ONVIF_ERR_UnsupportedSignatureAlgorithm
 *  ONVIF_ERR_KeySignatureAlgorithmMismatch
 *  ONVIF_ERR_InvalidKeyStatus
 *  ONVIF_ERR_InvalidSubject
 *  ONVIF_ERR_InvalidAttribute
 **/
ONVIF_RET onvif_tas_CreatePKCS10CSR(tas_CreatePKCS10CSR_REQ * p_req, tas_CreatePKCS10CSR_RES * p_res)
{
    KeyList * p_key;
    EVP_PKEY * evp_pkey;
    const EVP_MD * evp_md;
    X509_NAME * subject;

    p_key = onvif_find_Key(g_onvif_cfg.keys, p_req->KeyID);
    if (NULL == p_key)
    {
        return ONVIF_ERR_KeyID;
    }

    if (strcmp(p_key->KeyAttribute.KeyStatus, "ok"))
    {
        return ONVIF_ERR_InvalidKeyStatus;
    }

    evp_pkey = onvif_get_evp_pkey(p_key->public_key, p_key->private_key);
    if (NULL == evp_pkey)
    {
        return ONVIF_ERR_CSRCreationFailed;
    }

    evp_md = onvif_get_evp_md_from_oid(p_req->SignatureAlgorithm.algorithm);
    if (NULL == evp_md)
    {
        EVP_PKEY_free(evp_pkey);
        return ONVIF_ERR_UnsupportedSignatureAlgorithm;
    }

    subject = X509_NAME_new();
    if (NULL == subject) 
    {
        EVP_PKEY_free(evp_pkey);
        log_print(HT_LOG_ERR, "%s, Error creating X509_NAME\r\n", __FUNCTION__);
        return ONVIF_ERR_CSRCreationFailed;
    }

    if (!onvif_set_subject(&p_req->Subject, subject))
    {
        EVP_PKEY_free(evp_pkey);
        X509_NAME_free(subject);
        log_print(HT_LOG_ERR, "%s, onvif_set_subject failed\r\n", __FUNCTION__);
        return ONVIF_ERR_InvalidSubject;
    }

    if (!onvif_create_pkcs10_der(subject, evp_pkey, evp_md, &p_res->PKCS10CSR.ptr, &p_res->PKCS10CSR.size))
    {
        EVP_PKEY_free(evp_pkey);
        X509_NAME_free(subject);
        log_print(HT_LOG_ERR, "%s, onvif_create_pkcs10_der failed\r\n", __FUNCTION__);
        return ONVIF_ERR_CSRCreationFailed;
    }

    EVP_PKEY_free(evp_pkey);
    X509_NAME_free(subject);
    
    return ONVIF_OK;
}

/**
 * @brief
 *  Generates for a public key on the device a self-signed X.509 certificate that complies to RFC 5280.
 *
 *  The X509Version parameter specifies the version of X.509 that the generated certificate shall comply to. A
 *  device that supports this command shall support the generation of X.509v3 certificates as specified in RFC
 *  5280 and may additionally be able to handle other X.509 certificate formats as indicated by the X.509Versions
 *  capability. If no X509Version is specified in the request, the device shall produce an X.509v3 certificate.
 *
 *  The key pair that contains the public key for which a self-signed certificate shall be produced is specified by its
 *  key pair ID. The subject parameter describes the entity that the public key belongs to.
 *
 *  If the key pair does not have status ok, a device shall produce an InvalidKeyStatus fault and no certificate
 *  shall be generated.
 *
 *  If the specified subject is invalid or incomplete, an InvalidSubject fault shall be produced and no certificate
 *  shall be created.
 *
 *  The notValidBefore parameter specifies at which point in time the validity period of the generated certificate
 *  shall begin. If this parameter is not specified in the request, the device shall use its current time or a time
 *  before its current time as starting point of the validity period. The notValidAfter parameter specifies at which
 *  point in time the validity period of the generated certificate shall end. If this parameter is not specified in the
 *  request, the device shall assign the GeneralizedTime value of 99991231235959Z as specified in RFC 5280
 *  to the notValidAfter parameter. If the notValidBefore parameter is invalid, an invalid DateTime fault shall be
 *  produced and no certificate shall be generated. If the notValidAfter parameter is invalid, an invalid DateTime
 *  fault shall be produced and no certificate shall be generated.
 *  
 *  The Extensions parameter specifies potential X509v3 extensions that shall be contained in the certificate. A
 *  device that supports this command shall support the extensions that are defined in RFC5280, Sect. 4.2 as
 *  mandatory for CAs that issue self-signed certificates.
 *
 *  Distinguished name attribute values shall be supplied either in UTF-8 or in hexadecimal form as specified in RFC 4514.
 *
 *  If the distinguished name attribute value is supplied in hexadecimal form, the device shall encode the attribute
 *  in the format given in the hexadecimal format.
 *
 *  If the distinguished name attribute value is supplied in UTF-8 and the attribute value has a uniquely defined
 *  encoding (e.g., CountryName is defined as PrintableString), the device shall encode the attribute as the defined
 *  encoding. Otherwise, the device shall encode the attribute value as UTF-8.
 *
 *  RFC 5280, Sect. 4.1.2.2 mandates that the certificate serial numbers be unique for each certificate issued by a
 *  given issuer (a CA). Since the subject is equal to the issuer in a self-signed certificate, the serial number shall
 *  be unique for each self-signed certificate that the device issues for a given subject.
 *
 *  The generated certificate shall not contain a unique identifier as specified in RFC 5280, Sect. 4.1.2.8. The
 *  device shall not mark the generated certificate as trusted.
 *
 *  Certificates are uniquely identified using certificate IDs. If the command was successful, the device generates
 *  a new ID for the generated certificate and returns this ID.
 *
 *  If the device does not have enough storage capacity for storing the certificate to be created, the maximum
 *  number of certificates reached fault shall be produced and no certificate shall be generated.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_CertificateCreationFailed
 *  ONVIF_ERR_MaximumNumberOfCertificatesReached
 *  ONVIF_ERR_UnsupportedX509Version
 *  ONVIF_ERR_KeyID
 *  ONVIF_ERR_UnsupportedSignatureAlgorithm
 *  ONVIF_ERR_KeySignatureAlgorithmMismatch
 *  ONVIF_ERR_X509VersionExtensionsMismatch
 *  ONVIF_ERR_InvalidKeyStatus
 *  ONVIF_ERR_InvalidSubject
 *  ONVIF_ERR_InvalidDateTime
 **/
ONVIF_RET onvif_tas_CreateSelfSignedCertificate(tas_CreateSelfSignedCertificate_REQ * p_req, tas_CreateSelfSignedCertificate_RES * p_res)
{
    ONVIF_RET ret = ONVIF_OK;
    KeyList * p_key = NULL;
    EVP_PKEY * evp_pkey = NULL;
    const EVP_MD * evp_md = NULL;
    X509_NAME * subject = NULL;
    CertificateList * p_cert = NULL;

    p_key = onvif_find_Key(g_onvif_cfg.keys, p_req->KeyID);
    if (NULL == p_key)
    {
        return ONVIF_ERR_KeyID;
    }

    if (strcmp(p_key->KeyAttribute.KeyStatus, "ok"))
    {
        return ONVIF_ERR_InvalidKeyStatus;
    }

    evp_pkey = onvif_get_evp_pkey(p_key->public_key, p_key->private_key);
    if (NULL == evp_pkey)
    {
        return ONVIF_ERR_CSRCreationFailed;
    }

    evp_md = onvif_get_evp_md_from_oid(p_req->SignatureAlgorithm.algorithm);
    if (NULL == evp_md)
    {
        ret = ONVIF_ERR_CSRCreationFailed;
        goto cleanup;
    }

    subject = X509_NAME_new();
    if (NULL == subject) 
    {
        ret = ONVIF_ERR_CSRCreationFailed;
        log_print(HT_LOG_ERR, "%s, Error creating X509_NAME\r\n", __FUNCTION__);
        goto cleanup;
    }

    if (!onvif_set_subject(&p_req->Subject, subject))
    {
        ret = ONVIF_ERR_InvalidSubject;
        goto cleanup;
    }

    p_cert = onvif_add_Certificate(&g_onvif_cfg.certificates);
    if (NULL == p_cert)
    {
        ret = ONVIF_ERR_MaximumNumberOfCertificatesReached;
        goto cleanup;
    }
    
    if (!onvif_create_self_singed_cert(p_req, subject, evp_pkey, evp_md, 
        &p_cert->Certificate.CertificateContent.ptr, &p_cert->Certificate.CertificateContent.size))
    {
        ret = ONVIF_ERR_CSRCreationFailed;
        goto cleanup;
    }

    strcpy(p_cert->Certificate.KeyID, p_req->KeyID);
    p_cert->Certificate.AliasFlag = p_req->AliasFlag;
    if (p_req->AliasFlag)
    {
        strcpy(p_cert->Certificate.Alias, p_req->Alias);
    }

    strcpy(p_res->CertificateID, p_cert->Certificate.CertificateID);

cleanup:

    if (evp_pkey)
    {
        EVP_PKEY_free(evp_pkey);
    }

    if (subject)
    {
        X509_NAME_free(subject);
    }

    if (ONVIF_OK != ret)
    {
        if (p_cert)
        {
            onvif_free_Certificate(&g_onvif_cfg.certificates, p_cert);
        }
    }
    
    return ret;
}

/**
 * @brief
 *  Uploads an X.509 certificate as specified by RFC 5280 in DER encoding and the public key in
 *  the certificate to a device¡¯s keystore. A device that supports this command shall be able to handle X.509v3
 *  certificates as specified in RFC 5280 and may additionally be able to handle other X.509 certificate formats
 *  as indicated by the X.509Versions capability.
 *
 *  Certificates are uniquely identified using certificate IDs, and key pairs are uniquely identified using key IDs.
 *  The device shall generate a new certificate ID for the uploaded certificate.
 *
 *  Certain certificate usages, e.g. TLS server authentication, require the private key that corresponds to the public
 *  key in the certificate to be present in the keystore. In such cases, the client may indicate that it expects the
 *  device to produce a fault if the matching private key for the uploaded certificate is not present in the keystore
 *  by setting the PrivateKeyRequired argument in the upload request to true.
 *
 *  The uploaded certificate has to be linked to a key pair in the keystore.
 *
 *  If no private key is required for the public key in the certificate and a key pair exists in the keystore with a public
 *  key equal to the public key in the certificate, the uploaded certificate is linked to the key pair identified by the
 *  supplied key ID by adding a reference from the certificate to the key pair.
 *
 *  If no private key is required for the public key in the certificate and no key pair exists with the public key equal to
 *  the public key in the certificate, a new key pair with status ok is created with the public key from the certificate,
 *  and this key pair is linked to the uploaded certificate by adding a reference from the certificate to the key pair.
 *
 *  If a private key is required for the public key in the certificate, and a key pair exists in the keystore with a
 *  private key that matches the public key in the certificate, the uploaded certificate is linked to this key pair by
 *  adding a reference from the certificate to the key pair. If a private key is required for the public key and no such
 *  keypair exists in the keystore, then NoMatchingPrivateKey fault shall be produced and the certificate shall not
 *  be stored in the keystore.
 *  
 *  The device shall assign the supplied Alias to the uploaded certificate.
 *
 *  If a new key pair is generated, the device shall assign the supplied KeyAlias to it. Otherwise, the device shall
 *  ignore an eventually supplied KeyAlias.
 *
 *  If the key pair that the certificate shall be linked to does not have status ok, an InvalidKeyStatus fault is produced,
 *  and the uploaded certificate is not stored in the keystore.
 *
 *  If the signature algorithm that the signature of the supplied certificate is based on is not supported by the device,
 *  the device shall generate an UnsupportedSignatureAlgorithm fault and shall not store the uploaded certificate
 *  nor the contained public key in the keystore.
 *
 *  If the device cannot process the uploaded certificate, a BadCertificate fault is produced and neither the uploaded
 *  certificate nor the public key are stored in the device¡¯s keystore. The BadCertificate fault shall not be produced
 *  based on the mere fact that the device¡¯s current time lies outside the interval defined by the notBefore and
 *  notAfter fields as specified by RFC 5280, Sect. 4.1.
 *
 *  The device shall not mark the uploaded certificate as trusted.
 *
 *  If the device does not have enough storage capacity for storing the certificate to be uploaded, the maximum
 *  number of certificates reached fault shall be produced and no certificate shall be uploaded.
 *
 *  If the device does not have enough storage capacity for storing the key pair that eventually has to be created,
 *  the device shall generate a maximum number of keys reached fault. Furthermore the device shall not generate
 *  a key pair and no certificate shall be stored.
 *
 *  If the command was successful, the device returns the ID of the uploaded certificate and the ID of the key pair
 *  that contains the public key in the certificate.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_MaximumNumberOfCertificatesReached
 *  ONVIF_ERR_MaximumNumberOfKeysReached
 *  ONVIF_ERR_NoMatchingPrivateKey
 *  ONVIF_ERR_BadCertificate
 *  ONVIF_ERR_UnsupportedPublicKeyAlgorithm
 *  ONVIF_ERR_UnsupportedSignatureAlgorithm
 *  ONVIF_ERR_InvalidKeyStatus
 *  ONVIF_ERR_Duplicate
 **/
ONVIF_RET onvif_tas_UploadCertificate(tas_UploadCertificate_REQ * p_req, tas_UploadCertificate_RES * p_res)
{
    BOOL new_key = FALSE;
    ONVIF_RET ret = ONVIF_OK;
    int der_size = 0;
    int pem_pkey_size = 0;
    const uint8 * p;
    uint8 * der_cert = NULL;
    char * pem_pkey = NULL;
    X509 * cert = NULL;
    EVP_PKEY * evp_pkey = NULL;
    KeyList * p_key = NULL;
    CertificateList * p_cert = NULL;
    
    if (!p_req->Certificate.ptr || p_req->Certificate.size <= 0)
    {
        return ONVIF_ERR_BadCertificate;
    }

    der_cert = (uint8 *) malloc(p_req->Certificate.size);
    if (NULL == der_cert)
    {
        return ONVIF_ERR_MaximumNumberOfCertificatesReached;
    }

    der_size = base64_decode(p_req->Certificate.ptr, p_req->Certificate.size, der_cert, p_req->Certificate.size);
    if (der_size <= 0)
    {
        ret = ONVIF_ERR_BadCertificate;
        goto cleanup;
    }

    p = der_cert;
    cert = d2i_X509(NULL, &p, der_size);
    if (NULL == cert)
    {
        ret = ONVIF_ERR_BadCertificate;
        log_print(HT_LOG_ERR, "%s, Error X509 object, %s\r\n", 
            __FUNCTION__, ERR_reason_error_string(ERR_get_error()));
        goto cleanup;
    }

    evp_pkey = X509_get_pubkey(cert);
    if (NULL == evp_pkey)
    {
        ret = ONVIF_ERR_BadCertificate;
        log_print(HT_LOG_ERR, "%s, Error public key, %s\r\n", 
            __FUNCTION__, ERR_reason_error_string(ERR_get_error()));
        goto cleanup;
    }

    if (!onvif_get_pem_public_key(evp_pkey, &pem_pkey, &pem_pkey_size))
    {
        ret = ONVIF_ERR_BadCertificate;
        log_print(HT_LOG_ERR, "%s, Error get pem public key, %s\r\n", 
            __FUNCTION__, ERR_reason_error_string(ERR_get_error()));
        goto cleanup;
    }

    p_key = onvif_find_key_by_pkey(g_onvif_cfg.keys, pem_pkey);

    if (p_req->PrivateKeyRequiredFlag && p_req->PrivateKeyRequired)
    {
        if (NULL == p_key)
        {
            ret = ONVIF_ERR_NoMatchingPrivateKey;
            goto cleanup;
        }
        else if (!p_key->private_key || p_key->private_key_length <= 0)
        {
            ret = ONVIF_ERR_NoMatchingPrivateKey;
            goto cleanup;
        }
        else
        {
            strcpy(p_res->KeyID, p_key->KeyAttribute.KeyID);
        }
    }
    else
    {
        if (NULL == p_key)
        {
            RSA * rsa = NULL;
            
            p_key = onvif_add_Key(&g_onvif_cfg.keys);
            if (NULL == p_key)
            {
                ret = ONVIF_ERR_MaximumNumberOfKeysReached;
                goto cleanup;
            }

            new_key = TRUE;

            if (p_req->KeyAliasFlag)
            {
                p_key->KeyAttribute.AliasFlag = 1;
                strcpy(p_key->KeyAttribute.Alias, p_req->KeyAlias);
            }

            strcpy(p_res->KeyID, p_key->KeyAttribute.KeyID);
            
            rsa = EVP_PKEY_get1_RSA(evp_pkey);
            if (NULL == rsa) 
            {
                ret = ONVIF_ERR_MaximumNumberOfKeysReached;
                goto cleanup;
            }

            if (!onvif_save_public_key(rsa, p_key))
            {
                RSA_free(rsa);
                ret = ONVIF_ERR_MaximumNumberOfKeysReached;
                goto cleanup;
            }

            if (!onvif_save_private_key(rsa, p_key))
            {
                RSA_free(rsa);
                ret = ONVIF_ERR_MaximumNumberOfKeysReached;
                goto cleanup;
            }

            p_key->KeyAttribute.hasPrivateKey = 1;
            p_key->KeyAttribute.externallyGenerated = 1;
            strcpy(p_key->KeyAttribute.KeyStatus, "ok");

            RSA_free(rsa);
        }
        else
        {
            strcpy(p_res->KeyID, p_key->KeyAttribute.KeyID);
        }
    }

    p_cert = onvif_add_Certificate(&g_onvif_cfg.certificates);
    if (NULL == p_cert)
    {
        ret = ONVIF_ERR_MaximumNumberOfCertificatesReached;
        goto cleanup;
    }

    p_cert->Certificate.AliasFlag = p_req->AliasFlag;
    if (p_req->AliasFlag)
    {
        strcpy(p_cert->Certificate.Alias, p_req->Alias);
    }

    if (!onvif_get_x509_base64_buff(cert, &p_cert->Certificate.CertificateContent.ptr, &p_cert->Certificate.CertificateContent.size))
    {
        ret = ONVIF_ERR_MaximumNumberOfCertificatesReached;
        log_print(HT_LOG_ERR, "%s, onvif_get_x509_der_base64_buff failed\r\n", __FUNCTION__);
        goto cleanup;
    }
    
    strcpy(p_cert->Certificate.KeyID, p_res->KeyID);
    strcpy(p_res->CertificateID, p_cert->Certificate.CertificateID);

cleanup:

    if (evp_pkey)
    {
        EVP_PKEY_free(evp_pkey);
    }
    
    if (cert)
    {
        X509_free(cert);
    }

    if (der_cert)
    {
        free(der_cert);
    }

    if (pem_pkey)
    {
        free(pem_pkey);
    }

    if (ONVIF_OK != ret)
    {
        if (p_cert)
        {
            onvif_free_Certificate(&g_onvif_cfg.certificates, p_cert);
        }

        if (new_key && p_key)
        {
            onvif_free_Key(&g_onvif_cfg.keys, p_key);
        }
    }
    
    return ret;
}

/**
 * @brief
 *  Uploads a certification path consisting of X.509 certificates as specified by RFC 5280 in DER
 *  encoding along with a private key to a device¡¯s keystore. Certificates and private key are supplied in the form
 *  of a PKCS#12 file as specified in PKCS#12.
 *
 *  The device shall support PKCS#12 files that contain the following safe bags:
 *  ? one or more instances of CertBag PKCS#12, Sect. 4.2.3
 *  ? either exactly one instance of KeyBag PKCS#12, Sect. 4.3.1 or exactly one instance of PKCS8Shroud
 *  edKeyBag PKCS#12, Sect. 4.2.2.
 *
 *  If the IgnoreAdditionalCertificates parameter has the value true, the device shall behave as if the client had
 *  supplied only the first CertBag in the sequence of CertBag instances.
 *
 *  The device shall support PKCS#12 passphrase integrity mode for integrity protection of the PKCS#12 PFX as
 *  specified in PKCS#12, Sect. 4. The device shall support PKCS8ShroudedKeyBags that are encrypted with the
 *  same passphrase as the CertBag instances.
 *
 *  If a passphrase is supplied, the device shall ignore an eventually supplied integrity passphrase ID and an
 *  eventually supplied encryption passphrase ID, and the device shall use the supplied passphrase to check the
 *  integrity of the PKCS#12 PFX and to decrypt the PKCS8ShroudedKeyBag and the CertBag instances. If a
 *  passphrase is supplied, but a CertBag is not encrypted, the device shall ignore the supplied passphrase when
 *  processing this CertBag. If a passphrase is supplied, but a KeyBag is supplied instead of a PKCS8Shrouded
 *  KeyBag, the device shall ignore the supplied passphrase when processing the KeyBag.
 *
 *  If the command was successful, the device shall return the ID of the created certification path and the ID of
 *  the key pair that contains the public key in the certificate.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_MaximumNumberOfCertificatesReached
 *  ONVIF_ERR_MaximumNumberOfKeysReached
 *  ONVIF_ERR_MaximumNumberOfCertificationPathsReached
 *  ONVIF_ERR_PassphraseID
 *  ONVIF_ERR_DecryptionFailed
 *  ONVIF_ERR_BadCertificate
 *  ONVIF_ERR_UnsupportedPublicKeyAlgorithm
 *  ONVIF_ERR_UnsupportedSignatureAlgorithm
 *  ONVIF_ERR_InvalidKeyStatus
 *  ONVIF_ERR_BadPKCS12File
 *  ONVIF_ERR_PublicPrivateKeyMismatch
 *  ONVIF_ERR_InvalidCertificationPath
 **/
ONVIF_RET onvif_tas_UploadCertificateWithPrivateKeyInPKCS12(tas_UploadCertificateWithPrivateKeyInPKCS12_REQ * p_req, tas_UploadCertificateWithPrivateKeyInPKCS12_RES * p_res)
{
    BOOL new_key = FALSE;
    ONVIF_RET ret = ONVIF_OK;
    int pkcs12_len;
    int pem_pkey_size = 0;
    char password[64] = {'\0'};
    char * pem_pkey = NULL;
    uint8 * pkcs12_data = NULL;    
    BIO * bio = NULL;
    PKCS12 * p12 = NULL;
    EVP_PKEY * pkey = NULL;
    X509 * cert = NULL;
    STACK_OF(X509) * ca_certs = NULL;
    KeyList * p_key = NULL;
    CertificateList * p_cert = NULL;
    CertificationPathList * p_certpath = NULL;

    if (p_req->PassphraseFlag)
    {
        strcpy(password, p_req->Passphrase);
    }
    else if (p_req->EncryptionPassphraseIDFlag)
    {
        PassphraseList * p_pass = onvif_find_Passphrase(g_onvif_cfg.passphrases, p_req->EncryptionPassphraseID);
        if (NULL == p_pass)
        {
            return ONVIF_ERR_PassphraseID;
        }

        strcpy(password, p_pass->Passphrase);
    }
    else if (p_req->IntegrityPassphraseIDFlag)
    {
        PassphraseList * p_pass = onvif_find_Passphrase(g_onvif_cfg.passphrases, p_req->IntegrityPassphraseID);
        if (NULL == p_pass)
        {
            return ONVIF_ERR_PassphraseID;
        }

        strcpy(password, p_pass->Passphrase);
    }
    
    pkcs12_data = (uint8 *) malloc(p_req->CertWithPrivateKey.size+1);
    if (NULL == pkcs12_data)
    {
        return ONVIF_ERR_MaximumNumberOfCertificatesReached;
    }

    pkcs12_len = base64_decode(p_req->CertWithPrivateKey.ptr, p_req->CertWithPrivateKey.size, pkcs12_data, p_req->CertWithPrivateKey.size);
    if (pkcs12_len <= 0)
    {
        ret = ONVIF_ERR_BadPKCS12File;
        goto cleanup;
    }

    bio = BIO_new_mem_buf(pkcs12_data, pkcs12_len);
    if (NULL == bio) 
    {
        ret = ONVIF_ERR_BadPKCS12File;
        goto cleanup;
    }

    p12 = d2i_PKCS12_bio(bio, NULL);
    if (!p12) 
    {
        ret = ONVIF_ERR_BadPKCS12File;
        goto cleanup;
    }

    if (!PKCS12_parse(p12, password, &pkey, &cert, &ca_certs)) 
    {
        ret = ONVIF_ERR_DecryptionFailed;
        goto cleanup;
    }

    if (!onvif_get_pem_public_key(pkey, &pem_pkey, &pem_pkey_size))
    {
        ret = ONVIF_ERR_BadCertificate;
        log_print(HT_LOG_ERR, "%s, Error get pem public key, %s\r\n", 
            __FUNCTION__, ERR_reason_error_string(ERR_get_error()));
        goto cleanup;
    }

    p_key = onvif_find_key_by_pkey(g_onvif_cfg.keys, pem_pkey);
    if (p_key)
    {
        strcpy(p_res->KeyID, p_key->KeyAttribute.KeyID);
    }
    else
    {
        RSA * rsa = NULL;
            
        p_key = onvif_add_Key(&g_onvif_cfg.keys);
        if (NULL == p_key)
        {
            ret = ONVIF_ERR_MaximumNumberOfKeysReached;
            goto cleanup;
        }

        new_key = TRUE;

        if (p_req->KeyAliasFlag)
        {
            p_key->KeyAttribute.AliasFlag = 1;
            strcpy(p_key->KeyAttribute.Alias, p_req->KeyAlias);
        }

        strcpy(p_res->KeyID, p_key->KeyAttribute.KeyID);
        
        rsa = EVP_PKEY_get1_RSA(pkey);
        if (NULL == rsa) 
        {
            ret = ONVIF_ERR_MaximumNumberOfKeysReached;
            goto cleanup;
        }

        if (!onvif_save_public_key(rsa, p_key))
        {
            RSA_free(rsa);
            ret = ONVIF_ERR_MaximumNumberOfKeysReached;
            goto cleanup;
        }

        if (!onvif_save_private_key(rsa, p_key))
        {
            RSA_free(rsa);
            ret = ONVIF_ERR_MaximumNumberOfKeysReached;
            goto cleanup;
        }

        p_key->KeyAttribute.hasPrivateKey = 1;
        p_key->KeyAttribute.externallyGenerated = 1;
        strcpy(p_key->KeyAttribute.KeyStatus, "ok");

        RSA_free(rsa);
    }

    p_cert = onvif_add_Certificate(&g_onvif_cfg.certificates);
    if (NULL == p_cert)
    {
        ret = ONVIF_ERR_MaximumNumberOfCertificatesReached;
        goto cleanup;
    }

    if (!onvif_get_x509_base64_buff(cert, &p_cert->Certificate.CertificateContent.ptr, &p_cert->Certificate.CertificateContent.size))
    {
        ret = ONVIF_ERR_MaximumNumberOfCertificatesReached;
        log_print(HT_LOG_ERR, "%s, onvif_get_x509_der_base64_buff failed\r\n", __FUNCTION__);
        goto cleanup;
    }
    
    strcpy(p_cert->Certificate.KeyID, p_res->KeyID);

    p_certpath = onvif_add_CertificationPath(&g_onvif_cfg.certificatepaths);
    if (NULL == p_certpath)
    {
        ret = ONVIF_ERR_MaximumNumberOfCertificationPathsReached;
        goto cleanup;
    }

    p_certpath->CertificationPath.AliasFlag = p_req->CertificationPathAliasFlag;
    if (p_req->CertificationPathAliasFlag)
    {
        strcpy(p_certpath->CertificationPath.Alias, p_req->CertificationPathAlias);
    }

    p_certpath->CertificationPath.sizeCertificateID = 1;
    strcpy(p_certpath->CertificationPath.CertificateID[0], p_cert->Certificate.CertificateID);

    strcpy(p_res->CertificationPathID, p_certpath->CertificationPathID);

cleanup:

    if (pem_pkey)
    {
        free(pem_pkey);
    }

    if (pkcs12_data)
    {
        free(pkcs12_data);
    }

    if (bio)
    {
        BIO_free(bio);
    }

    if (pkey)
    {
        EVP_PKEY_free(pkey);
    }

    if (cert)
    {
        X509_free(cert);
    }

    if (p12)
    {
        PKCS12_free(p12);
    }

    if (ONVIF_OK != ret)
    {
        if (new_key && p_key)
        {
            onvif_free_Key(&g_onvif_cfg.keys, p_key);
        }

        if (p_cert)
        {
            onvif_free_Certificate(&g_onvif_cfg.certificates, p_cert);
        }

        if (p_certpath)
        {
            onvif_free_CertificationPath(&g_onvif_cfg.certificatepaths, p_certpath);
        }
    }    

    return ret;
}

/**
 * @brief
 *  Returns a specific certificate from the device¡¯s keystore.
 *
 *  Certificates are uniquely identified using certificate IDs. If no certificate is stored under the requested 
 *  certificate ID in the keystore, an InvalidArgVal fault is produced.
 *  
 *  The certificate shall be returned in DER encoding.
 *
 *  It shall be noted that this command does not return the private key that is associated with the public key in
 *  the certificate.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_CertificateID
 **/
ONVIF_RET onvif_tas_GetCertificate(tas_GetCertificate_REQ * p_req, tas_GetCertificate_RES * p_res)
{
    CertificateList * p_cert = onvif_find_Certificate(g_onvif_cfg.certificates, p_req->CertificateID);
    if (NULL == p_cert)
    {
        return ONVIF_ERR_CertificateID;
    }

    memcpy(&p_res->Certificate, &p_cert->Certificate, sizeof(onvif_X509Certificate));

    return ONVIF_OK;
}

/**
 * @brief
 *  Deletes a certificate from the device¡¯s keystore.
 *
 *  The operation shall not delete the public key that is contained in the certificate from the keystore.
 *  
 *   Certificates are uniquely identified using certificate IDs. If no certificate is stored under the requested certificate
 *  ID in the keystore, an InvalidArgVal fault is produced. If there is a certificate under the requested certificate ID
 *  stored in the keystore and the certificate could not be deleted, a CertificateDeletion fault is produced.
 *
 *  If a reference exists for the specified certificate, the certificate shall not be deleted and the corresponding fault
 *  shall be produced.
 *
 *  After a certificate has been successfully deleted, the device may assign its former ID to other certificates.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_CertificateDeletionFailed
 *  ONVIF_ERR_CertificateID
 *  ONVIF_ERR_ReferenceExists
 **/
ONVIF_RET onvif_tas_DeleteCertificate(tas_DeleteCertificate_REQ * p_req)
{
    CertificationPathList * p_certpath;
    CertificateList * p_cert = onvif_find_Certificate(g_onvif_cfg.certificates, p_req->CertificateID);
    if (NULL == p_cert)
    {
        return ONVIF_ERR_CertificateID;
    }

    p_certpath = g_onvif_cfg.certificatepaths;
    while (p_certpath)
    {
        uint32 i;

        for (i = 0; i < p_certpath->CertificationPath.sizeCertificateID; i++)
        {
            if (strcmp(p_certpath->CertificationPath.CertificateID[i], p_cert->Certificate.CertificateID) == 0)
            {
                return ONVIF_ERR_ReferenceExists;
            }
        }
        
        p_certpath = p_certpath->next;
    }

    onvif_free_Certificate(&g_onvif_cfg.certificates, p_cert);

    return ONVIF_OK;
}

/**
 * @brief
 *  Creates a sequence of certificates that may be used, e.g., for certification path validation or for
 *  TLS server authentication.
 *
 *  Certification paths are uniquely identified using certification path IDs. Certificates are uniquely identified using
 *  certificate IDs. A certification path contains a sequence of certificate IDs.
 *
 *  If there is a certificate ID in the sequence of supplied certificate IDs for which no certificate exists in the device¡¯s
 *  keystore, the corresponding fault shall be produced and no certification path shall be created.
 *
 *  The signature of each certificate in the certification path except for the last one shall be verifiable with the public
 *  key contained in the next certificate in the path. If there is a certificate ID in the request other than the last ID
 *  for which the corresponding certificate cannot be verified with the public key in the certificate identified by the
 *  next certificate ID, an InvalidCertificateChain fault shall be produced and no certification path shall be created.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_MaximumNumberOfCertificationPathsReached
 *  ONVIF_ERR_CertificateID
 *  ONVIF_ERR_InvalidCertificationPath
 *  ONVIF_ERR_CertificationPathCreationFailed
 **/
ONVIF_RET onvif_tas_CreateCertificationPath(tas_CreateCertificationPath_REQ * p_req, tas_CreateCertificationPath_RES * p_res)
{
    uint32 i = 0;
    CertificateList * p_cert;
    CertificationPathList * p_certpath;
    
    for (i = 0; i < p_req->CertificateIDs.sizeCertificateID; i++)
    {
        p_cert = onvif_find_Certificate(g_onvif_cfg.certificates, p_req->CertificateIDs.CertificateID[i]);
        if (NULL == p_cert)
        {
            return ONVIF_ERR_CertificateID;
        }
    }

    p_certpath = onvif_add_CertificationPath(&g_onvif_cfg.certificatepaths);
    if (NULL == p_certpath)
    {
        return ONVIF_ERR_MaximumNumberOfCertificationPathsReached;
    }

    p_certpath->CertificationPath.sizeCertificateID = p_req->CertificateIDs.sizeCertificateID;
    memcpy(&p_certpath->CertificationPath.CertificateID, p_req->CertificateIDs.CertificateID, sizeof(p_req->CertificateIDs.CertificateID));

    p_certpath->CertificationPath.AliasFlag = p_req->AliasFlag;
    if (p_req->AliasFlag)
    {
        strcpy(p_certpath->CertificationPath.Alias, p_req->Alias);
    }

    strcpy(p_res->CertificationPathID, p_certpath->CertificationPathID);

    return ONVIF_OK;
}

/**
 * @brief
 *  Returns a specific certification path from the device¡¯s keystore.
 *
 *  Certification paths are uniquely identified using certification path IDs. If no certification path is stored under the
 *  requested ID in the keystore, an InvalidArgVal fault is produced.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_CertificationPathID
 **/
ONVIF_RET onvif_tas_GetCertificationPath(tas_GetCertificationPath_REQ * p_req, tas_GetCertificationPath_RES * p_res)
{
    CertificationPathList * p_certpath = onvif_find_CertificationPath(g_onvif_cfg.certificatepaths, p_req->CertificationPathID);
    if (NULL == p_certpath)
    {
        return ONVIF_ERR_CertificationPathID;
    }

    memcpy(&p_res->CertificationPath, &p_certpath->CertificationPath, sizeof(onvif_CertificationPath));
    
    return ONVIF_OK;
}

/**
 * @brief
 *  Modify a certification path. A device shall support this method if support for SetCertPath
 *  is signaled via its capabilities.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_CertificationPathID
 *  ONVIF_ERR_CertificateID
 *  ONVIF_ERR_InvalidCertificationPath
 **/
ONVIF_RET onvif_tas_SetCertificationPath(tas_SetCertificationPath_REQ * p_req)
{
    uint32 i;
    CertificationPathList * p_certpath = onvif_find_CertificationPath(g_onvif_cfg.certificatepaths, p_req->CertificationPathID);
    if (NULL == p_certpath)
    {
        return ONVIF_ERR_CertificationPathID;
    }

    for (i = 0; i < p_req->CertificationPath.sizeCertificateID; i++)
    {
        CertificateList * p_cert = onvif_find_Certificate(g_onvif_cfg.certificates, p_req->CertificationPath.CertificateID[i]);
        if (NULL == p_cert)
        {
            return ONVIF_ERR_CertificateID;
        }
    }

    memcpy(&p_certpath->CertificationPath, &p_req->CertificationPath, sizeof(onvif_CertificationPath));

    return ONVIF_OK;
}

/**
 * @brief
 *  Deletes a certification path from the device¡¯s keystore.
 *
 *  This operation shall not delete the certificates that are referenced by the certification path.
 *
 *  Certification paths are uniquely identified using certification path IDs. If no certification path is stored under the
 *  requested certification path ID in the keystore, an InvalidArgVal fault is produced. If there is a certification path
 *  under the requested certification path ID stored in the keystore and the certification path could not be deleted,
 *  a CertificationPathDeletion fault is produced.
 *
 *  If a reference exists for the specified certification path, the certification path shall not be deleted and the cor
 *  responding fault shall be produced.
 *
 *  After a certification path is successfully deleted, the device may assign its former ID to other certification paths.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_CertificationPathDeletionFailed
 *  ONVIF_ERR_CertificationPathID
 *  ONVIF_ERR_ReferenceExists
 **/
ONVIF_RET onvif_tas_DeleteCertificationPath(tas_DeleteCertificationPath_REQ * p_req)
{
    uint32 i;
    CertificationPathList * p_certpath = onvif_find_CertificationPath(g_onvif_cfg.certificatepaths, p_req->CertificationPathID);
    if (NULL == p_certpath)
    {
        return ONVIF_ERR_CertificationPathID;
    }

    for (i = 0; i < ARRAY_SIZE(g_onvif_cfg.certpathid); i++)
    {
        if (g_onvif_cfg.certpathid[i][0] != '\0' && strcmp(g_onvif_cfg.certpathid[i], p_req->CertificationPathID) == 0)
        {
            break;
        }
    }
    
    if (i < ARRAY_SIZE(g_onvif_cfg.certpathid))
    {
        return ONVIF_ERR_ReferenceExists;
    }

    onvif_free_CertificationPath(&g_onvif_cfg.certificatepaths, p_certpath);
    
    return ONVIF_OK;
}

/**
 * @brief
 *  Uploads a certificate revocation list (CRL) as specified in RFC 5280 to the keystore on the device.
 *
 *  If the device does not have enough storage space to store the CRL to be uploaded, the device shall produce
 *  a MaximumNumberOfCRLsReached fault and shall not store the supplied CRL.
 *
 *  If the device is not able to process the supplied CRL, the device shall produce a BadCRL fault and shall not
 *  store the supplied CRL.
 *
 *  If the device does not support the signature algorithm that was used to sign the supplied CRL, the device shall
 *  produce an UnsupportedSignatureAlgorithm fault and shall not store the supplied CRL.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_MaximumNumberOfCRLsReached
 *  ONVIF_ERR_BadCRL
 *  ONVIF_ERR_UnsupportedSignatureAlgorithm
 **/
ONVIF_RET onvif_tas_UploadCRL(tas_UploadCRL_REQ * p_req, tas_UploadCRL_RES * p_res)
{
    return ONVIF_ERR_Genenal;
}

/**
 * @brief
 *  Returns a specific certificate revocation list (CRL) from the keystore on the device.
 *
 *  Certification revocation lists are uniquely identified using CRLIDs. If no CRL is stored under the requested
 *  CRLID, the device shall produce a CRLID fault.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_CRLID
 **/
ONVIF_RET onvif_tas_GetCRL(tas_GetCRL_REQ * p_req, tas_GetCRL_RES * p_res)
{
    return ONVIF_ERR_Genenal;
}

/**
 * @brief
 *  Deletes a certificate revocation list (CRL) from the keystore on the device.
 *
 *  Certification revocation lists are uniquely identified using CRLIDs. If no CRL is stored under the requested
 *  CRLID, the device shall produce a CRLID fault.
 *
 *  If a reference exists for the specified CRL, the device shall produce a ReferenceExists fault and shall not delete
 *  the CRL.
 *
 *  After a CRL has been successfully deleted, a device may assign its former ID to other CRLs.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_CRLID
 *  ONVIF_ERR_ReferenceExists
 **/
ONVIF_RET onvif_tas_DeleteCRL(tas_DeleteCRL_REQ * p_req)
{
    return ONVIF_ERR_Genenal;
}

/**
 * @brief
 *  Creates a certification path validation policy.
 *
 *  Certification path validation policies are uniquely identified using certification path validation policy IDs. The
 *  device shall generate a new certification path validation policy ID for the created certification path validation
 *  policy.
 *
 *  If the device does not have enough storage capacity for storing the certification path validation policy to be
 *  created, the device shall produce a maximum number of certification path validation policies reached fault and
 *  shall not create a certification path validation policy.
 *
 *  If there is at least one trust anchor certificate ID in the request for which there exists no certificate in the device¡¯s
 *  keystore, the device shall produce a CertificateID fault and shall not create a certification path validation policy.
 *
 *  If the device cannot process the supplied certification path validation parameters, the device shall produce a
 *  CertPathValidationParameters fault and shall not create a certification path validation policy.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_MaximumNumberOfCertPathValidationPoliciesReached
 *  ONVIF_ERR_CertificateID
 *  ONVIF_ERR_CertPathValidationParameters
 **/
ONVIF_RET onvif_tas_CreateCertPathValidationPolicy(tas_CreateCertPathValidationPolicy_REQ * p_req, tas_CreateCertPathValidationPolicy_RES * p_res)
{
    return ONVIF_ERR_Genenal;
}

/**
 * @brief
 *  Returns a certification path validation policy from the keystore on the device.
 *
 *  Certification path validation policies are uniquely identified using certification path validation policy IDs. If no
 *  certification path validation policy is stored under the requested certification path validation policy ID, the device
 *  shall produce a CertPathValidationPolicyID fault.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_CertPathValidationPolicyID
 **/
ONVIF_RET onvif_tas_GetCertPathValidationPolicy(tas_GetCertPathValidationPolicy_REQ * p_req, tas_GetCertPathValidationPolicy_RES * p_res)
{
    return ONVIF_ERR_Genenal;
}

/**
 * @brief
 *  Modify a certification path validation policy in the keystore on the device. A device
 *  shall support this method if support for SetCertPath is signaled via its capabilities.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_CertPathValidationPolicyID
 *  ONVIF_ERR_CertificateID
 *  ONVIF_ERR_CertPathValidationParameters
 **/
ONVIF_RET onvif_tas_SetCertPathValidationPolicy(tas_SetCertPathValidationPolicy_REQ * p_req)
{
    return ONVIF_ERR_Genenal;
}

/**
 * @brief
 *  Deletes a certification path validation policy from the keystore on the device.
 *
 *  Certification path validation policies are uniquely identified using certification path validation policy IDs. If no
 *  certification path validation policy is stored under the requested certification path validation policy ID, the device
 *  shall produce an CertPathValidationPolicyID fault.
 *
 *  If a reference exists for the requested certification path validation policy, the device shall produce a Reference
 *  Exists fault and shall not delete the certification path validation policy.
 *
 *  After the certification path validation policy has been deleted, the device may assign its former ID to other
 *  certification path validation policies.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_CertPathValidationPolicyID
 *  ONVIF_ERR_ReferenceExists
 **/
ONVIF_RET onvif_tas_DeleteCertPathValidationPolicy(tas_DeleteCertPathValidationPolicy_REQ * p_req)
{
    return ONVIF_ERR_Genenal;
}

/**
 * @brief
 *  Assigns a key pair and certificate along with a certification path (certificate chain) to the TLS
 *  server on the device. The TLS server shall use this information for key exchange during the TLS handshake,
 *  particularly for constructing server certificate messages as specified in RFC 4346, RFC 2246.
 *
 *  Certification paths are identified by their certification path IDs in the keystore. The first certificate in the certifi
 *  cation path shall be the TLS server certificate.
 *
 *  Since each certificate has exactly one associated key pair, a reference to the key pair that is associated with
 *  the server certificate is not supplied explicitly. Devices shall obtain the private key or results of operations under
 *  the private key by suitable internal interaction with the keystore.
 *
 *  If a device chooses to perform a TLS key exchange based on the supplied certification path, it shall use the
 *  key pair that is associated with the server certificate for key exchange and transmit the certification path to TLS
 *  clients as-is, i.e., the device shall not check conformance of the certification path to RFC 4346, RFC 2246.
 *
 *  In order to use the server certificate during the TLS handshake, the corresponding private key is required.
 *  Therefore, if the key pair that is associated with the server certificate, i.e., the first certificate in the certification
 *  path, does not have an associated private key, the NoPrivateKey fault is produced and the certification path
 *  is not associated with the TLS server.
 *
 *  A TLS server may present different certification paths to different clients during the TLS handshake instead of
 *  presenting the same certification path to all clients. Therefore more than one certification path may be assigned
 *  to the TLS server. If the maximum number of certification paths that may be assigned to the TLS server simul
 *  taneously is reached, the device shall generate a MaximumNumberOfTLSCertificationPathsReached fault and
 *  the requested certification path shall not be assigned to the TLS server.
 *
 *  If the certification path identified by the supplied certification path ID is already assigned to the TLS server,
 *  this command shall have no effect.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_CertificationPathID
 *  ONVIF_ERR_NoPrivateKey
 *  ONVIF_ERR_MaximumNumberOfTLSCertificationPathsReached
 **/
ONVIF_RET onvif_tas_AddServerCertificateAssignment(tas_AddServerCertificateAssignment_REQ * p_req)
{
    uint32 i;
    CertificationPathList * p_certpath = onvif_find_CertificationPath(g_onvif_cfg.certificatepaths, p_req->CertificationPathID);
    if (NULL == p_certpath)
    {
        return ONVIF_ERR_CertificationPathID;
    }

    for (i = 0; i < ARRAY_SIZE(g_onvif_cfg.certpathid); i++)
    {
        if (g_onvif_cfg.certpathid[i][0] == '\0')
        {
            strcpy(g_onvif_cfg.certpathid[i], p_req->CertificationPathID);
            break;
        }
    }

    if (i == ARRAY_SIZE(g_onvif_cfg.certpathid))
    {
        return ONVIF_ERR_MaximumNumberOfTLSCertificationPathsReached;
    }

    if (g_onvif_cfg.curcertpathid[0] != '\0')
    {
        return ONVIF_OK;
    }
    
    if (g_onvif_cls.https_srv.enable)
    {
        return ONVIF_ERR_Action;
    }
    
    if (!onvif_load_certification_path(&p_certpath->CertificationPath))
    {
        return ONVIF_ERR_Action;
    }

    strcpy(g_onvif_cfg.curcertpathid, p_certpath->CertificationPathID);

    return ONVIF_OK;
}

/**
 * @brief
 *  Removes a key pair and certificate assignment (including certification path) to the TLS server on the device.
 *
 *  Certification paths are identified using certification path IDs. If the supplied certification path ID is not associated
 *  with the TLS server, an InvalidArgVal fault is produced.
 *
 *  If the TLS server on the device is enabled, the device shall produce a ReferenceExists fault and shall not
 *  remove the server certificate assignment.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_OldCertificationPathID
 *  ONVIF_ERR_ReferenceExists
 **/
ONVIF_RET onvif_tas_RemoveServerCertificateAssignment(tas_RemoveServerCertificateAssignment_REQ * p_req)
{
    uint32 i;
    CertificationPathList * p_certpath = onvif_find_CertificationPath(g_onvif_cfg.certificatepaths, p_req->CertificationPathID);
    if (NULL == p_certpath)
    {
        return ONVIF_ERR_OldCertificationPathID;
    }

    if (g_onvif_cls.https_srv.enable && strcmp(g_onvif_cfg.curcertpathid, p_certpath->CertificationPathID) == 0)
    {
        return ONVIF_ERR_ReferenceExists;
    }

    for (i = 0; i < ARRAY_SIZE(g_onvif_cfg.certpathid); i++)
    {
        if (g_onvif_cfg.certpathid[i][0] != '\0' && strcmp(g_onvif_cfg.certpathid[i], p_req->CertificationPathID) == 0)
        {
            g_onvif_cfg.certpathid[i][0] = '\0';
            break;
        }
    }
    
    if (i == ARRAY_SIZE(g_onvif_cfg.certpathid))
    {
        return ONVIF_ERR_OldCertificationPathID;
    }

    if (strcmp(g_onvif_cfg.curcertpathid, p_certpath->CertificationPathID) == 0)
    {
        g_onvif_cfg.curcertpathid[0] = '\0';
    }

    return ONVIF_OK;
}

/**
 * @brief
 *  Replaces an existing key pair and certificate assignment to the TLS server on the device by a
 *  new key pair and certificate assignment (including certification paths).
 *
 *  After the replacement, the TLS server shall use the new certificate and certification path exactly in those cases in
 *  which it would have used the old certificate and certification path. Therefore, especially in the case that several
 *  server certificates are assigned to the TLS server, clients that wish to replace an old certificate assignment
 *  by a new assignment should use this operation instead of a combination of the Add TLS Server Certificate
 *  Assignment and the Remove TLS Server Certificate Assignment operations.
 *
 *  Certification paths are identified using certification path IDs. If the supplied old certification path ID is not asso
 *  ciated with the TLS server, or no certification path exists under the new certification path ID, the corresponding
 *  InvalidArgVal faults are produced and the associations are unchanged.
 *
 *  The first certificate in the new certification path shall be the TLS server certificate.
 *
 *  Since each certificate has exactly one associated key pair, a reference to the key pair that is associated with
 *  the new server certificate is not supplied explicitly. Devices shall obtain the private key or results of operations
 *  under the private key by suitable internal interaction with the keystore.
 *
 *  If a device chooses to perform a TLS key exchange based on the new certification path, it shall use the key
 *  pair that is associated with the server certificate for key exchange and transmit the certification path to TLS
 *  clients as-is, i.e., the device shall not check conformance of the certification path to RFC 4346, RFC 2246.
 *
 *  In order to use the server certificate during the TLS handshake, the corresponding private key is required.
 *  Therefore, if the key pair that is associated with the server certificate, i.e., the first certificate in the certification
 *  path, does not have an associated private key, the NoPrivateKey fault is produced and the certification path
 *  is not associated with the TLS server.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_OldCertificationPathID
 *  ONVIF_ERR_NewCertificationPathID
 *  ONVIF_ERR_NoPrivateKey
 **/
ONVIF_RET onvif_tas_ReplaceServerCertificateAssignment(tas_ReplaceServerCertificateAssignment_REQ * p_req)
{
    uint32 i;
    CertificationPathList * p_certpath = onvif_find_CertificationPath(g_onvif_cfg.certificatepaths, p_req->NewCertificationPathID);
    if (NULL == p_certpath)
    {
        return ONVIF_ERR_NewCertificationPathID;
    }

    for (i = 0; i < ARRAY_SIZE(g_onvif_cfg.certpathid); i++)
    {
        if (g_onvif_cfg.certpathid[i][0] != '\0' && strcmp(g_onvif_cfg.certpathid[i], p_req->OldCertificationPathID) == 0)
        {
            strcpy(g_onvif_cfg.certpathid[i], p_req->NewCertificationPathID);
            break;
        }
    }
    
    if (i == ARRAY_SIZE(g_onvif_cfg.certpathid))
    {
        return ONVIF_ERR_OldCertificationPathID;
    }

    if (strcmp(g_onvif_cfg.curcertpathid, p_req->OldCertificationPathID))
    {
        return ONVIF_OK;
    }

    if (!onvif_load_certification_path(&p_certpath->CertificationPath))
    {
        return ONVIF_ERR_Action;
    }

    strcpy(g_onvif_cfg.curcertpathid, p_certpath->CertificationPathID);

    return ONVIF_OK;
}

/**
 * @brief
 *  Returns the IDs of all certification paths that are assigned to the TLS server on the device.
 *
 *  This operation may be used, e.g., if a client lost track of the certification path assignments on the device.
 *
 *  If no certification path is assigned to the TLS server, an empty list is returned.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 **/
ONVIF_RET onvif_tas_GetAssignedServerCertificates(tas_GetAssignedServerCertificates_RES * p_res)
{
    uint32 i;
    
    for (i = 0; i < ARRAY_SIZE(g_onvif_cfg.certpathid); i++)
    {
        if (g_onvif_cfg.certpathid[i][0] != '\0')
        {
            uint32 idx = p_res->sizeCertificationPathID;
            
            strcpy(p_res->CertificationPathID[idx], g_onvif_cfg.certpathid[i]);
            p_res->sizeCertificationPathID++;
        }
    }
    
    return ONVIF_OK;
}

/**
 * @brief
 *  Activates or deactivates TLS client authentication for the TLS server on the device.
 *
 *  The TLS server on the device shall require client authentication if and only if clientAuthenticationRequired is
 *  set to true.
 *
 *  If TLS client authentication is requested to be enabled and no certification path validation policy is assigned to
 *  the TLS server, the device shall return an EnablingClientAuthenticationFailed fault and shall not enable TLS
 *  client authentication.
 *
 *  The device shall execute this command regardless of the TLS enabled/disabled state configured in the ONVIF
 *  Device Management Service.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_EnablingClientAuthenticationFailed
 **/
ONVIF_RET onvif_tas_SetClientAuthenticationRequired(tas_SetClientAuthenticationRequired_REQ * p_req)
{
    return ONVIF_ERR_Genenal;
}

/**
 * @brief
 *  Returns whether TLS client authentication is active.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 **/
ONVIF_RET onvif_tas_GetClientAuthenticationRequired(tas_GetClientAuthenticationRequired_RES * p_res)
{
    return ONVIF_ERR_Genenal;
}

/**
 * @brief
 *  Enables or disables mapping of the Common Name present in the TLS client certificate to an
 *  existing user name in the device.
 *
 *  The TLS server on the device shall perform mapping if parameter clientAuthenticationRequired is set to true.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_CnMapsToUserFailed
 **/
ONVIF_RET onvif_tas_SetCnMapsToUser(tas_SetCnMapsToUser_REQ * p_req)
{
    return ONVIF_ERR_Genenal;
}

/**
 * @brief
 *  Returns whether the Common Name Mapping to User is enabled.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 **/
ONVIF_RET onvif_tas_GetCnMapsToUser(tas_GetCnMapsToUser_RES * p_res)
{
    return ONVIF_ERR_Genenal;
}

/**
 * @brief
 *  Assigns a certification path validation policy to the TLS server on the device. The TLS server
 *  shall enforce the policy when authenticating TLS clients and consider a client authentic if Certificaton Path
 *  Validation according to section 6 of RFC 5280 succeeds.
 *
 *  If no certification path validation policy is stored under the requested CertPathValidationPolicyID, the device
 *  shall produce a CertPathValidationPolicyID fault.
 *
 *  A TLS server may use different certification path validation policies to authenticate clients. Therefore more
 *  than one certification path validation policy may be assigned to the TLS server. If the maximum number of
 *  certification path validation policies that may be assigned to the TLS server simultaneously is reached, the
 *  device shall produce a MaximumNumberOfTLSCertPathValidationPoliciesReached fault and shall not assign
 *  the requested certification path validation policy to the TLS server.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_CertPathValidationPolicyID
 *  ONVIF_ERR_MaximumNumberOfTLSCertPathValidationPoliciesReached
 **/
ONVIF_RET onvif_tas_AddCertPathValidationPolicyAssignment(tas_AddCertPathValidationPolicyAssignment_REQ * p_req)
{
    return ONVIF_ERR_Genenal;
}

/**
 * @brief
 *  Removes a certification path validation policy assignment from the TLS server on the device.
 *
 *  If the certification path validation policy identified by the requested CertPathValidationPolicyID is not associated
 *  to the TLS server, the device shall produce a CertPathValidationPolicyID fault.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_CertPathValidationPolicyID
 **/
ONVIF_RET onvif_tas_RemoveCertPathValidationPolicyAssignment(tas_RemoveCertPathValidationPolicyAssignment_REQ * p_req)
{
    return ONVIF_ERR_Genenal;
}

/**
 * @brief
 *  Replaces a certification path validation policy assignment to the TLS server on the device with
 *  another certification path validation policy assignment.
 *
 *  If the certification path validation policy identified by the requested OldCertPathValidationPolicyID is not asso
 *  ciated to the TLS server, the device shall produce an OldCertPathValidationPolicyID fault and shall not asso
 *  ciate the certification path validation policy identified by the NewCertPathValidationPolicyID to the TLS server.
 *
 *  If no certification path validation policy exists under the requested NewCertPathValidationPolicyID in the de
 *  vice¡¯s keystore, the device shall produce a NewCertPathValidationPolicyID fault and shall not remove the as
 *  sociation of the old certification path validation policy to the TLS server.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_OldCertPathValidationPolicyID
 *  ONVIF_ERR_NewCertPathValidationPolicyID
 **/
ONVIF_RET onvif_tas_ReplaceCertPathValidationPolicyAssignment(tas_ReplaceCertPathValidationPolicyAssignment_REQ * p_req)
{
    return ONVIF_ERR_Genenal;
}

/**
 * @brief
 *  Returns the IDs of all certification path validation policies that are assigned to the TLS server on the device.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 **/
ONVIF_RET onvif_tas_GetAssignedCertPathValidationPolicies(tas_GetAssignedCertPathValidationPolicies_RES * p_res)
{
    return ONVIF_ERR_Genenal;
}

/**
 * @brief
 *  Sets the version(s) of TLS which the device shall use. Valid values are taken from the TLSServerSupported capability.
 *
 *  A client initiates a TLS session by sending a ClientHello with the highest TLS version it supports. This suggests
 *  to the server that the client can accept any TLS version up to and including that version.
 *
 *  The server then chooses the TLS version to use. This is generally the highest TLS version the server supports
 *  that is within the range of the client. For example, if a ClientHello indicates TLS version 1.1, the server can
 *  proceed with TLS 1.0 or TLS 1.1.
 *
 *  In the event that an ONVIF installation wishes to disable certain version(s) of TLS, it may do so with this
 *  operation. For example, to disable TLS 1.0 on a device signaling support for TLS versions 1.0, 1.1, and 1.2, the
 *  enabled version list may be set to "1.1 1.2", omitting 1.0. If a client then attempts to connect with a ClientHello
 *  containing TLS 1.0, the server shall send a "protocol_version" alert message and close the connection. This
 *  handshake indicates to the client that TLS 1.0 is not supported by the server. The client must try again with
 *  a higher TLS version suggestion.
 *
 *  An empty version list is not permitted. Disabling all versions of TLS is not the intent of this operation. See
 *  AddServerCertificateAssignment and RemoveServerCertificateAssignment.
 *
 *  A device signalling support for TLS version enabling with the EnabledVersionsSupported capability shall support
 *  this command.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_EmptyList
 *  ONVIF_ERR_TLSVersion
 **/
ONVIF_RET onvif_tas_SetEnabledTLSVersions(tas_SetEnabledTLSVersions_REQ * p_req)
{
    int opt;
    uint32 len, idx = 0;
    char * p;
    char ver[8];
    
    if (p_req->Versions[0] == '\0')
    {
        return ONVIF_ERR_EmptyList;
    }

    if (!g_onvif_cfg.Capabilities.security.TLSServerCapabilities.TLSServerSupportedFlag)
    {
        return ONVIF_ERR_TLSVersion;
    }

    len = 0;
    p = p_req->Versions;
    
    while (1)
    {
        if (*p == ' ' || *p == '\0')
        {
            ver[len] = '\0';

            if (len == 0)
            {
                return ONVIF_ERR_TLSVersion;
            }
            else if (!strstr(g_onvif_cfg.Capabilities.security.TLSServerCapabilities.TLSServerSupported, ver))
            {
                return ONVIF_ERR_TLSVersion;
            }

            len = 0;
            strcpy(g_onvif_cfg.tlsversions[idx++], ver);

            if (*p == '\0')
            {
                break;
            }
        }
        else if (len < sizeof(ver)-1)
        {
            ver[len++] = *p;
        }

        p++;
    }

    for (; idx < ARRAY_SIZE(g_onvif_cfg.tlsversions); idx++)
    {
        g_onvif_cfg.tlsversions[idx][0] = '\0';
    }

    opt = SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1 | SSL_OP_NO_TLSv1_2 | SSL_OP_NO_TLSv1_3;

    for (idx = 0; idx < ARRAY_SIZE(g_onvif_cfg.tlsversions); idx++)
    {
        if (g_onvif_cfg.tlsversions[idx][0] == '\0')
        {
            break;
        }

        if (strcasecmp(g_onvif_cfg.tlsversions[idx], "1.0") == 0)
        {
            opt &= ~SSL_OP_NO_TLSv1;
        }
        else if (strcasecmp(g_onvif_cfg.tlsversions[idx], "1.1") == 0)
        {
            opt &= ~SSL_OP_NO_TLSv1_1;
        }
        else if (strcasecmp(g_onvif_cfg.tlsversions[idx], "1.2") == 0)
        {
            opt &= ~SSL_OP_NO_TLSv1_2;
        }
        else if (strcasecmp(g_onvif_cfg.tlsversions[idx], "1.3") == 0)
        {
            opt &= ~SSL_OP_NO_TLSv1_3;
        }
    }

    if (g_onvif_cls.https_srv.ssl_ctx)
    {
        SSL_CTX_clear_options((SSL_CTX *) g_onvif_cls.https_srv.ssl_ctx, SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1 | SSL_OP_NO_TLSv1_2 | SSL_OP_NO_TLSv1_3);
        SSL_CTX_set_options((SSL_CTX *) g_onvif_cls.https_srv.ssl_ctx, opt);
    }

    return ONVIF_OK;
}

/**
 * @brief
 *  Retrieves the version(s) of TLS which are currently enabled on the device. A device signalling
 *  support for TLS version enabling with the EnabledVersionsSupported capability shall support this command.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 **/
ONVIF_RET onvif_tas_GetEnabledTLSVersions(tas_GetEnabledTLSVersions_RES * p_res)
{
    uint32 i;

    for (i = 0; i < ARRAY_SIZE(g_onvif_cfg.tlsversions); i++)
    {
        if (g_onvif_cfg.tlsversions[i][0] == '\0')
        {
            break;
        }
        
        if (i == 0)
        {
            strcpy(p_res->Versions, g_onvif_cfg.tlsversions[i]);
        }
        else
        {
            int len = strlen(p_res->Versions);

            p_res->Versions[len] = ' ';
            strcat(p_res->Versions, g_onvif_cfg.tlsversions[i]);
        }
    }
    
    return ONVIF_OK;
}

/**
 * @brief
 *  Uploads a passphrase to the keystore of the device.
 *
 *  Passphrases are uniquely identified using passphrase IDs. The device shall generate a new passphrase ID
 *  for the uploaded passphrase.
 *
 *  If the command was successful, the device shall return the ID of the uploaded passphrase.
 *
 *  If the device does not have enough storage capacity for storing the passphrase to be uploaded, the device
 *  shall produce a maximum number of passphrases reached fault and shall not upload the supplied passphrase.
 *
 *  If the device cannot process the passphrase to be uploaded, the device shall produce a BadPassphrase fault
 *  and shall not upload a passphrase.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_MaximumNumberOfPassphrasesReached
 *  ONVIF_ERR_BadPassphrase
 **/
ONVIF_RET onvif_tas_UploadPassphrase(tas_UploadPassphrase_REQ * p_req, tas_UploadPassphrase_RES * p_res)
{
    PassphraseList * p_Passphrase = onvif_add_Passphrase(&g_onvif_cfg.passphrases);
    if (NULL == p_Passphrase)
    {
        return ONVIF_ERR_MaximumNumberOfPassphrasesReached;
    }
    
    strcpy(p_Passphrase->Passphrase, p_req->Passphrase);

    if (p_req->PassphraseAliasFlag)
    {
        p_Passphrase->PassphraseAttribute.AliasFlag = 1;
        strcpy(p_Passphrase->PassphraseAttribute.Alias, p_req->PassphraseAlias);
    }

    strcpy(p_res->PassphraseID, p_Passphrase->PassphraseAttribute.PassphraseID);

    return ONVIF_OK;
}

/**
 * @brief
 *  Deletes a passphrase from the keystore of the device.
 *
 *  Passphrases are uniquely identified using passphrase IDs. If no passphrase is stored under the requested
 *  passphrase ID in the keystore, a device shall produce an invalid passphrase ID fault. If there is a passphrase
 *  under the requested passphrase ID stored in the keystore and the passphrase could not be deleted, a device
 *  shall produce a passphrase deletion failed fault.
 *
 *  After a passphrase is successfully deleted, the device may assign its former ID to other passphrases.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_PassphraseDeletionFailed
 *  ONVIF_ERR_PassphraseID
 **/
ONVIF_RET onvif_tas_DeletePassphrase(tas_DeletePassphrase_REQ * p_req)
{
    PassphraseList * p_Passphrase = onvif_find_Passphrase(g_onvif_cfg.passphrases, p_req->PassphraseID);
    if (NULL == p_Passphrase)
    {
        return ONVIF_ERR_PassphraseID;
    }

    onvif_free_Passphrase(&g_onvif_cfg.passphrases, p_Passphrase);

    return ONVIF_OK;
}

#endif



