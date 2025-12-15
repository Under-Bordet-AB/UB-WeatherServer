/* Minimal, client-only, browser-like TLS config for mbedTLS */

#ifndef MBEDTLS_USER_CONFIG_H
#define MBEDTLS_USER_CONFIG_H

/* ==== PROTOCOLS ==== */
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_SSL_PROTO_TLS1_3

/* Client only */
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_TLS_C

/* SNI + ALPN (optional but common) */
#define MBEDTLS_SSL_SERVER_NAME_INDICATION
#define MBEDTLS_SSL_ALPN

/* ==== X.509 ==== */
#define MBEDTLS_X509_CRT_PARSE_C
#define MBEDTLS_X509_USE_C
#define MBEDTLS_PEM_PARSE_C

/* Optional: CRL / OCSP (uncomment if you need) */
/* #define MBEDTLS_X509_CRL_PARSE_C */
/* #define MBEDTLS_X509_REMOVE_TIME (disable if you verify time) */

/* Verify cert validity period (needs a working clock) */
#define MBEDTLS_HAVE_TIME
#define MBEDTLS_HAVE_TIME_DATE

/* ==== CRYPTO PRIMITIVES (modern) ==== */
#define MBEDTLS_AES_C
#define MBEDTLS_GCM_C
#define MBEDTLS_CHACHAPOLY_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_MD_C
#define MBEDTLS_SHA256_C

/* ECC for ECDHE/ECDSA */
#define MBEDTLS_ECP_C
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECDSA_C

/* Public-key wrapper and parsing */
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C

/* Big number + ASN.1/OID */
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_OID_C

/* RNG */
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_CTR_DRBG_C

/* Error strings (handy while integrating) */
#define MBEDTLS_ERROR_C

/* Optional: built-in sockets helper (you can use your own I/O instead) */
/* #define MBEDTLS_NET_C */

/* ==== TLS 1.3 ciphersuites (keep it small & modern) ==== */
#define MBEDTLS_SSL_TLS1_3_KEY_EXCHANGE_MODE_EPHEMERAL_ENABLED
#define MBEDTLS_SSL_CIPHERSUITE_TLS1_3_AES_128_GCM_SHA256
#define MBEDTLS_SSL_CIPHERSUITE_TLS1_3_CHACHA20_POLY1305_SHA256

/* ==== TLS 1.2 (fallback, still common) ==== */
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
#define MBEDTLS_GCM_C
#define MBEDTLS_CHACHAPOLY_C
/* The library will pick sane defaults from enabled algs */

/* Reduce size by trimming features you don’t need */

#endif /* MBEDTLS_USER_CONFIG_H */
