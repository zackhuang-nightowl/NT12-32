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

#ifndef ONVIF_SECURITY_H
#define ONVIF_SECURITY_H

#include "sys_inc.h"
#include "onvif.h"
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/evp.h>
#include <openssl/pkcs12.h>
#include <openssl/ssl.h>

/***************************************************************************************/

typedef struct
{
    uint32  AliasFlag : 1;                              // Indicates whether the field Alias is valid
    uint32  Reserved  : 31;
    
    uint32  KeyLength;                                  // required, The length of the key to be created
    char    Alias[32];                                  // optional, The client-defined alias of the key
} tas_CreateRSAKeyPair_REQ;

typedef struct
{
    char    KeyID[64];                                  // required, The key ID of the key pair being generated
    uint32  EstimatedCreationTime;                      // required, Best-effort estimate of how long the key generation will take, unit is second
} tas_CreateRSAKeyPair_RES;

typedef struct
{
    uint32  AliasFlag : 1;                              // Indicates whether the field Alias is valid
    uint32  Reserved  : 31;
    
    char    EllipticCurve[32];                          // required, The name of the elliptic curve to be used for generating the ECC keypair. 
                                                        //  Supported curve names can be found in the IANA TLS Supported Groups section, under the Description field
    char    Alias[32];                                  // optional, The client-defined alias of the key
} tas_CreateECCKeyPair_REQ;

typedef struct
{
    char    KeyID[64];                                  // required, The key ID of the key pair being generated
    uint32  EstimatedCreationTime;                      // required, Best-effort estimate of how long the key generation will take, unit is second
} tas_CreateECCKeyPair_RES;

typedef struct
{
    uint32  AliasFlag : 1;                              // Indicates whether the field Alias is valid
    uint32  EncryptionPassphraseIDFlag : 1;             // Indicates whether the field EncryptionPassphraseID is valid
    uint32  EncryptionPassphraseFlag : 1;               // Indicates whether the field EncryptionPassphrase is valid
    uint32  Reserved  : 29;
    
    onvif_base64Binary KeyPair;                         // required, The key pair to be uploaded in a PKCS#8 data structure
    char    Alias[32];                                  // optional, The client-defined alias of the key pair
    char    EncryptionPassphraseID[64];                 // optional, The ID of the passphrase to use for decrypting the uploaded key pair
    char    EncryptionPassphrase[64];                   // optional, The passphrase to use for decrypting the uploaded key pair
} tas_UploadKeyPairInPKCS8_REQ;

typedef struct
{
    char    KeyID[64];                                  // required, The key ID of the uploaded key pair
} tas_UploadKeyPairInPKCS8_RES;

typedef struct
{
    char    KeyID[64];                                  // required, The ID of the key for which to return the status
} tas_GetKeyStatus_REQ;

typedef struct
{
    char    KeyStatus[16];                              // required, Status of the requested key
                                                        //  ok : Key is ready for use
                                                        //  generating : Key is being generated
                                                        //  corrupt : Key has not been successfully generated and cannot be used
} tas_GetKeyStatus_RES;

typedef struct
{
    char    KeyID[64];                                  // required, The ID of the key pair for which to return whether it contains a private key
} tas_GetPrivateKeyStatus_REQ;

typedef struct
{
    BOOL    hasPrivateKey;                              // required, True if and only if the key pair contains a private key
} tas_GetPrivateKeyStatus_RES;

typedef struct
{
    char    KeyID[64];                                  // required, The ID of the key that is to be deleted from the keystore
} tas_DeleteKey_REQ;

typedef struct
{
    char    KeyID[64];                                  // required, The ID of the key for which the CSR shall be created
    onvif_DistinguishedName     Subject;                // required, The subject to be included in the CSR
    onvif_AlgorithmIdentifier   SignatureAlgorithm;     // required, The signature algorithm to be used to sign the CSR
} tas_CreatePKCS10CSR_REQ;

typedef struct
{
    onvif_base64Binary PKCS10CSR;                       // required, The DER encoded PKCS#10 certification request
} tas_CreatePKCS10CSR_RES;

typedef struct
{
    uint32  X509VersionFlag : 1;                        // Indicates whether the field X509Version is valid
    uint32  AliasFlag : 1;                              // Indicates whether the field Alias is valid
    uint32  notValidBeforeFlag : 1;                     // Indicates whether the field notValidBefore is valid
    uint32  notValidAfterFlag : 1;                      // Indicates whether the field notValidBefore is valid
    uint32  Reserved : 28;
    
    uint32  X509Version;                                // optional, The X.509 version that the generated certificate shall comply to
    onvif_DistinguishedName Subject;                    // required, Distinguished name of the entity that the certificate shall belong to
    char    KeyID[64];                                  // required, The ID of the key for which the certificate shall be created
    char    Alias[64];                                  // optional, The client-defined alias of the certificate to be created
    time_t  notValidBefore;                             // optional, The X.509 not valid before information to be included in the certificate. 
                                                        //  Defaults to the device's current time or a time before the device's current time
    time_t  notValidAfter;                              // optional, The X.509 not valid after information to be included in the certificate. 
                                                        //  Defaults to the time 99991231235959Z as specified in RFC 5280
    onvif_AlgorithmIdentifier SignatureAlgorithm;       // required, The signature algorithm to be used for signing the certificate
    uint32  sizeExtension;
    onvif_X509v3Extension Extension[4];                 // optional, An X.509v3 extension to be included in the certificate
} tas_CreateSelfSignedCertificate_REQ;

typedef struct
{
    char    CertificateID[64];                          // required, The ID of the generated certificate
} tas_CreateSelfSignedCertificate_RES;

typedef struct
{
    uint32  AliasFlag : 1;                              // Indicates whether the field Alias is valid
    uint32  KeyAliasFlag : 1;                           // Indicates whether the field KeyAlias is valid
    uint32  PrivateKeyRequiredFlag : 1;                 // Indicates whether the field PrivateKeyRequired is valid
    uint32  Reserved : 29;
    
    onvif_base64Binary Certificate;                     // required, The base64-encoded DER representation of the X.509 certificate to be uploaded
    char    Alias[64];                                  // optional, The client-defined alias of the certificate
    char    KeyAlias[64];                               // optional, The client-defined alias of the key pair
    BOOL    PrivateKeyRequired;                         // optional, Indicates if the device shall verify that a matching key pair with a private key exists in the keystore
} tas_UploadCertificate_REQ;

typedef struct
{
    char    CertificateID[64];                          // required, The ID of the uploaded certificate
    char    KeyID[64];                                  // required, The ID of the key that the uploaded certificate certifies
} tas_UploadCertificate_RES;

typedef struct
{
    uint32  CertificationPathAliasFlag : 1;             // Indicates whether the field CertificationPathAlias is valid
    uint32  KeyAliasFlag : 1;                           // Indicates whether the field KeyAlias is valid
    uint32  IgnoreAdditionalCertificatesFlag : 1;       // Indicates whether the field IgnoreAdditionalCertificates is valid
    uint32  IntegrityPassphraseIDFlag : 1;              // Indicates whether the field IntegrityPassphraseID is valid
    uint32  EncryptionPassphraseIDFlag : 1;             // Indicates whether the field EncryptionPassphraseID is valid
    uint32  PassphraseFlag : 1;                         // Indicates whether the field Passphrase is valid
    uint32  Reserved : 26;
    
    onvif_base64Binary CertWithPrivateKey;              // required, The certificates and key pair to be uploaded in a PKCS#12 data structure
    char    CertificationPathAlias[64];                 // optional, The client-defined alias of the certification path
    char    KeyAlias[64];                               // optional, The client-defined alias of the key pair
    BOOL    IgnoreAdditionalCertificates;               // optional, True if and only if the device shall behave as if the client had only supplied the first certificate in the sequence of certificates
    char    IntegrityPassphraseID[64];                  // optional, The ID of the passphrase to use for integrity checking of the uploaded PKCS#12 data structure
    char    EncryptionPassphraseID[64];                 // optional, The ID of the passphrase to use for decrypting the uploaded PKCS#12 data structure
    char    Passphrase[64];                             // optional, The passphrase to use for integrity checking and decrypting the uploaded PKCS#12 data structure
} tas_UploadCertificateWithPrivateKeyInPKCS12_REQ;

typedef struct 
{
    char    CertificationPathID[64];                    // required, The certification path ID of the uploaded certification path
    char    KeyID[64];                                  // required, The key ID of the uploaded key pair
} tas_UploadCertificateWithPrivateKeyInPKCS12_RES;

typedef struct
{
    char    CertificateID[64];                          // required, The ID of the certificate to retrieve
} tas_GetCertificate_REQ;

typedef struct
{
    onvif_X509Certificate Certificate;                  // required, The DER representation of the certificate
} tas_GetCertificate_RES;

typedef struct
{
    char    CertificateID[64];                          // required, The ID of the certificate to delete
} tas_DeleteCertificate_REQ;

typedef struct
{
    uint32  AliasFlag : 1;                              // Indicates whether the field Alias is valid
    uint32  Reserved  : 31;
    
    onvif_CertificateIDs CertificateIDs;                // required, The IDs of the certificates to include in the certification path, 
                                                        //  where each certificate signature except for the last one in the path must be
                                                        //  fiable with the public key certified by the next certificate in the path
    char    Alias[64];                                  // optional, The client-defined alias of the certification path
} tas_CreateCertificationPath_REQ;

typedef struct
{
        char    CertificationPathID[64];                // required, The ID of the generated certification path
} tas_CreateCertificationPath_RES;

typedef struct
{
    char    CertificationPathID[64];                    // required, The ID of the certification path to retrieve
} tas_GetCertificationPath_REQ;

typedef struct
{
    onvif_CertificationPath CertificationPath;          // required, The certification path that is stored under the given ID in the keystore
} tas_GetCertificationPath_RES;

typedef struct
{
    char    CertificationPathID[64];                    // required, The ID of the certification path to be modidfied
    onvif_CertificationPath CertificationPath;          // required, The updated certification path to be stored in the keystore
} tas_SetCertificationPath_REQ;

typedef struct
{
    char    CertificationPathID[64];                    // required, The ID of the certification path to delete
} tas_DeleteCertificationPath_REQ;

typedef struct 
{
    uint32  AliasFlag : 1;                              // Indicates whether the field Alias is valid
    uint32  Reserved  : 31;
    
    onvif_base64Binary Crl;                             // required, The CRL to be uploaded to the device
    char    Alias[64];                                  // optional, The alias to assign to the uploaded CRL
} tas_UploadCRL_REQ;

typedef struct 
{
    char    CrlID[64];                                  // required, The ID of the uploaded CRL
} tas_UploadCRL_RES;

typedef struct 
{
    char    CrlID[64];                                  // required, The ID of the CRL to be returned
} tas_GetCRL_REQ;

typedef struct 
{
    onvif_CRL Crl;                                      // required, The CRL with the requested ID
} tas_GetCRL_RES;

typedef struct 
{
    char    CrlID[64];                                  // required, The ID of the CRL to be deleted
} tas_DeleteCRL_REQ;

typedef struct 
{
    uint32  AliasFlag : 1;                              // Indicates whether the field Alias is valid
    uint32  Reserved  : 31;
    
    char    Alias[64];                                  // optional, The alias to assign to the created certification path validation policy
    onvif_CertPathValidationParameters Parameters;      // required, The parameters of the certification path validation policy to be created
    uint32  sizeTrustAnchor;
    onvif_TrustAnchor TrustAnchor[4];                   // optional, The trust anchors of the certification path validation policy to be created
} tas_CreateCertPathValidationPolicy_REQ;

typedef struct 
{
    char    CertPathValidationPolicyID[64];             // required, The ID of the created certification path validation policy
} tas_CreateCertPathValidationPolicy_RES;

typedef struct 
{
    char    CertPathValidationPolicyID[64];             // required, The ID of the certification path validation policy
} tas_GetCertPathValidationPolicy_REQ;

typedef struct 
{
    onvif_CertPathValidationPolicy CertPathValidationPolicy;    // required, The certification path validation policy that is stored under the requested ID
} tas_GetCertPathValidationPolicy_RES;

typedef struct 
{
    char    CertPathValidationPolicyID[64];             // required, The ID of the certification path validation policy to be modified
    onvif_CertPathValidationPolicy CertPathValidationPolicy;    // required, The updated certification path validation policy to be stored
} tas_SetCertPathValidationPolicy_REQ;

typedef struct 
{
    char    CertPathValidationPolicyID[64];             // required, The ID of the certification path validation policy to be deleted
} tas_DeleteCertPathValidationPolicy_REQ;

typedef struct 
{
    char    CertificationPathID[64];                    // required, The ID of the certification path
} tas_AddServerCertificateAssignment_REQ;

typedef struct 
{
    char    CertificationPathID[64];                    // required, The ID of the certification path
} tas_RemoveServerCertificateAssignment_REQ;

typedef struct 
{
    char    OldCertificationPathID[64];                 // required, The ID of the certification path
    char    NewCertificationPathID[64];                 // required, The ID of the certification path
} tas_ReplaceServerCertificateAssignment_REQ;

typedef struct 
{
    int     sizeCertificationPathID;
    char    CertificationPathID[8][64];                 // The IDs of all certification paths that are assigned to the TLS server on the device
} tas_GetAssignedServerCertificates_RES;

typedef struct 
{
    BOOL     clientAuthenticationRequired;              // required, 
} tas_SetClientAuthenticationRequired_REQ;

typedef struct 
{
    BOOL     clientAuthenticationRequired;              // required, 
} tas_GetClientAuthenticationRequired_RES;

typedef struct 
{
    BOOL     cnMapsToUser;                              // required, 
} tas_SetCnMapsToUser_REQ;

typedef struct 
{
    BOOL     cnMapsToUser;                              // required, 
} tas_GetCnMapsToUser_RES;

typedef struct
{
    char    CertPathValidationPolicyID[64];             // required, The ID of the certification path validation policy to assign to the TLS server
} tas_AddCertPathValidationPolicyAssignment_REQ;

typedef struct
{
    char    CertPathValidationPolicyID[64];             // required, The ID of the certification path validation policy to de-assign from the TLS server
} tas_RemoveCertPathValidationPolicyAssignment_REQ;

typedef struct
{
    char    OldCertPathValidationPolicyID[64];          // required, The ID of the certification path validation policy to be de-assigned from the TLS server
    char    NewCertPathValidationPolicyID[64];          // required, The ID of the certification path validation policy to assign to the TLS server
} tas_ReplaceCertPathValidationPolicyAssignment_REQ;

typedef struct 
{
    int     sizeCertPathValidationPolicyID;
    char    CertPathValidationPolicyID[8][64];          // A list of IDs of the certification path validation policies that are assigned to the TLS server
} tas_GetAssignedCertPathValidationPolicies_RES;

typedef struct 
{
    char    Versions[64];                               // required, List of TLS versions to allow
} tas_SetEnabledTLSVersions_REQ;

typedef struct 
{
    char    Versions[64];                               // required, List of TLS versions to allow
} tas_GetEnabledTLSVersions_RES;

typedef struct 
{
    uint32  PassphraseAliasFlag : 1;                    // Indicates whether the field PassphraseAlias is valid
    uint32  Reserved  : 31;
    
    char    Passphrase[64];                             // required, The passphrase to upload
    char    PassphraseAlias[64];                        // optional, The alias for the passphrase to upload
} tas_UploadPassphrase_REQ;

typedef struct 
{
    char    PassphraseID[64];                           // required, The PassphraseID of the uploaded passphrase
} tas_UploadPassphrase_RES;

typedef struct 
{
    char    PassphraseID[64];                           // required, The ID of the passphrase that is to be deleted from the keystore
} tas_DeletePassphrase_REQ;

#ifdef __cplusplus
extern "C" {
#endif

BOOL onvif_save_public_key(RSA *rsa_key, KeyList * p_key);
BOOL onvif_save_private_key(RSA *rsa_key, KeyList * p_key);
BOOL onvif_get_x509_base64_buff(X509 * cert, char ** buff, int * size);

ONVIF_RET onvif_tas_CreateRSAKeyPair(tas_CreateRSAKeyPair_REQ * p_req, tas_CreateRSAKeyPair_RES * p_res);
ONVIF_RET onvif_tas_CreateECCKeyPair(tas_CreateECCKeyPair_REQ * p_req, tas_CreateECCKeyPair_RES * p_res);
ONVIF_RET onvif_tas_UploadKeyPairInPKCS8(tas_UploadKeyPairInPKCS8_REQ * p_req, tas_UploadKeyPairInPKCS8_RES * p_res);
ONVIF_RET onvif_tas_GetKeyStatus(tas_GetKeyStatus_REQ * p_req, tas_GetKeyStatus_RES * p_res);
ONVIF_RET onvif_tas_GetPrivateKeyStatus(tas_GetPrivateKeyStatus_REQ * p_req, tas_GetPrivateKeyStatus_RES * p_res);
ONVIF_RET onvif_tas_DeleteKey(tas_DeleteKey_REQ * p_req);
ONVIF_RET onvif_tas_CreatePKCS10CSR(tas_CreatePKCS10CSR_REQ * p_req, tas_CreatePKCS10CSR_RES * p_res);
ONVIF_RET onvif_tas_CreateSelfSignedCertificate(tas_CreateSelfSignedCertificate_REQ * p_req, tas_CreateSelfSignedCertificate_RES * p_res);
ONVIF_RET onvif_tas_UploadCertificate(tas_UploadCertificate_REQ * p_req, tas_UploadCertificate_RES * p_res);
ONVIF_RET onvif_tas_UploadCertificateWithPrivateKeyInPKCS12(tas_UploadCertificateWithPrivateKeyInPKCS12_REQ * p_req, tas_UploadCertificateWithPrivateKeyInPKCS12_RES * p_res);
ONVIF_RET onvif_tas_GetCertificate(tas_GetCertificate_REQ * p_req, tas_GetCertificate_RES * p_res);
ONVIF_RET onvif_tas_DeleteCertificate(tas_DeleteCertificate_REQ * p_req);
ONVIF_RET onvif_tas_CreateCertificationPath(tas_CreateCertificationPath_REQ * p_req, tas_CreateCertificationPath_RES * p_res);
ONVIF_RET onvif_tas_GetCertificationPath(tas_GetCertificationPath_REQ * p_req, tas_GetCertificationPath_RES * p_res);
ONVIF_RET onvif_tas_SetCertificationPath(tas_SetCertificationPath_REQ * p_req);
ONVIF_RET onvif_tas_DeleteCertificationPath(tas_DeleteCertificationPath_REQ * p_req);
ONVIF_RET onvif_tas_UploadCRL(tas_UploadCRL_REQ * p_req, tas_UploadCRL_RES * p_res);
ONVIF_RET onvif_tas_GetCRL(tas_GetCRL_REQ * p_req, tas_GetCRL_RES * p_res);
ONVIF_RET onvif_tas_DeleteCRL(tas_DeleteCRL_REQ * p_req);
ONVIF_RET onvif_tas_CreateCertPathValidationPolicy(tas_CreateCertPathValidationPolicy_REQ * p_req, tas_CreateCertPathValidationPolicy_RES * p_res);
ONVIF_RET onvif_tas_GetCertPathValidationPolicy(tas_GetCertPathValidationPolicy_REQ * p_req, tas_GetCertPathValidationPolicy_RES * p_res);
ONVIF_RET onvif_tas_SetCertPathValidationPolicy(tas_SetCertPathValidationPolicy_REQ * p_req);
ONVIF_RET onvif_tas_DeleteCertPathValidationPolicy(tas_DeleteCertPathValidationPolicy_REQ * p_req);
ONVIF_RET onvif_tas_AddServerCertificateAssignment(tas_AddServerCertificateAssignment_REQ * p_req);
ONVIF_RET onvif_tas_RemoveServerCertificateAssignment(tas_RemoveServerCertificateAssignment_REQ * p_req);
ONVIF_RET onvif_tas_ReplaceServerCertificateAssignment(tas_ReplaceServerCertificateAssignment_REQ * p_req);
ONVIF_RET onvif_tas_GetAssignedServerCertificates(tas_GetAssignedServerCertificates_RES * p_res);
ONVIF_RET onvif_tas_SetClientAuthenticationRequired(tas_SetClientAuthenticationRequired_REQ * p_req);
ONVIF_RET onvif_tas_GetClientAuthenticationRequired(tas_GetClientAuthenticationRequired_RES * p_res);
ONVIF_RET onvif_tas_SetCnMapsToUser(tas_SetCnMapsToUser_REQ * p_req);
ONVIF_RET onvif_tas_GetCnMapsToUser(tas_GetCnMapsToUser_RES * p_res);
ONVIF_RET onvif_tas_AddCertPathValidationPolicyAssignment(tas_AddCertPathValidationPolicyAssignment_REQ * p_req);
ONVIF_RET onvif_tas_RemoveCertPathValidationPolicyAssignment(tas_RemoveCertPathValidationPolicyAssignment_REQ * p_req);
ONVIF_RET onvif_tas_ReplaceCertPathValidationPolicyAssignment(tas_ReplaceCertPathValidationPolicyAssignment_REQ * p_req);
ONVIF_RET onvif_tas_GetAssignedCertPathValidationPolicies(tas_GetAssignedCertPathValidationPolicies_RES * p_res);
ONVIF_RET onvif_tas_SetEnabledTLSVersions(tas_SetEnabledTLSVersions_REQ * p_req);
ONVIF_RET onvif_tas_GetEnabledTLSVersions(tas_GetEnabledTLSVersions_RES * p_res);
ONVIF_RET onvif_tas_UploadPassphrase(tas_UploadPassphrase_REQ * p_req, tas_UploadPassphrase_RES * p_res);
ONVIF_RET onvif_tas_DeletePassphrase(tas_DeletePassphrase_REQ * p_req);

#ifdef __cplusplus
}
#endif

#endif



