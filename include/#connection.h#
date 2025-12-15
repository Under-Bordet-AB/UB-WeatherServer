/**
 * Header file: connection.h
 * Contains the generic struct for a connection (client fd),
 * the generic struct for a listening server (server fd)
 * and the vtables to allow for polymorphic functions
 * for TCP and TLS.
 **/

#ifndef __CONNECTION_H__
#define __CONNECTION_H__

#include "../mbedtls/include/mbedtls/net_sockets.h"
#include "../mbedtls/include/mbedtls/ssl.h"
#include "../mbedtls/include/mbedtls/entropy.h"
#include "../mbedtls/include/mbedtls/ctr_drbg.h"
#include "../mbedtls/include/mbedtls/error.h"
#include "../mbedtls/include/mbedtls/x509_crt.h"
#include "../mbedtls/include/mbedtls/pk.h"
#include "smw.h"
#include <stdint.h>

typedef struct conn_vtable conn_vtable_t;
typedef struct conn conn_t;
typedef struct conn_tcp conn_tcp_t;
typedef struct conn_tls conn_tls_t;
typedef struct conn_listen_server conn_listen_server_t;
typedef struct conn_listen_server_vtable conn_listen_server_vtable_t;
typedef struct conn_listen_server_tcp conn_listen_server_tcp_t;
typedef struct conn_listen_server_tls conn_listen_server_tls_t;

////////////////////////////////////////
// CONNECTION INTERFACE
////////////////////////////////////////

/* vtable for the incoming connection */
struct conn_vtable
{
	/* we need fncptr for:  */
	int  (*read)(conn_t *self, void *buf, int count);
	int  (*write)(conn_t *self, const void *buf, int count);
	void (*close)(conn_t *self);
	/* these will be match with specific functions for tcp and tls */
};

/* this is the base/ parent struct */
struct conn
{
	/* it holds a pointer to the vtable */
	const conn_vtable_t *vtable;
	/* it also holds the client fd */
	int client_fd;
};

/* tcp and tls struct embed base/parent */
struct conn_tcp
{
	conn_t base;
};

struct conn_tls
{
	conn_t base;
	/* mbed TLS specifics here */
	mbedtls_ssl_context ssl;
	mbedtls_net_context net;	
};

////////////////////////////////////////
// LISTENING SERVER INTERFACE
////////////////////////////////////////

/* same old callback on accept */
typedef int (*OnAcceptCallBack)(conn_t *new_conn, void *ctx);

/* vtable for listening server */
struct conn_listen_server_vtable
{
	/* our accept now returns a connection */
	conn_t *(*accept_client)(conn_listen_server_t *self);
	void (*dispose)(conn_listen_server_t *self);
};

struct conn_listen_server
{
	/* holds pointer to vtable  */
	const conn_listen_server_vtable_t *vtable;
	int listen_fd;
	/* holds pointer to work task */
	smw_task *task;
	/* holds callback to call when a new client is accepted */
	OnAcceptCallBack on_accept;
	void *user_ctx;
	/* timeout */
	int recent_connections;
	uint64_t recent_connections_time;
};

struct conn_listen_server_tcp
{
	/* always embed base */
	conn_listen_server_t base;	
};

struct conn_listen_server_tls
{
	conn_listen_server_t base;
	/* holds TLS global state */
	mbedtls_entropy_context  entropy;
	mbedtls_ctr_drbg_context ctr_drbg;
	mbedtls_ssl_config       conf;
	mbedtls_x509_crt         srvcert;
	mbedtls_pk_context       pkey;
};

////////////////////////////////////////
// PUBLIC API
////////////////////////////////////////

/* factory functions */
conn_listen_server_t *conn_listen_server_tcp_init(const char *port, OnAcceptCallBack cb, void *ctx);
conn_listen_server_t *conn_listen_server_tls_init(const char *port, OnAcceptCallBack cb, void *ctx);
/* tcp connection functions */
int conn_tcp_read(conn_t *self, void *buf, int count);
int conn_tcp_write(conn_t *self, const void *buf, int count);
void conn_tcp_close(conn_t *self);
/* tls connection functions */
int conn_tls_read(conn_t *self, void *buf, int count);
int conn_tls_write(conn_t *self, const void *buf, int count);
void conn_tls_close(conn_t *self);
/* accept functions */
conn_t *conn_listen_server_tcp_accept_factory(conn_listen_server_t *self);
conn_t *conn_listen_server_tls_accept_factory(conn_listen_server_t *self);
/* clean up functions */
void conn_listen_server_dispose(conn_listen_server_t *self);
void conn_listen_server_tcp_dispose(conn_listen_server_t *self);
void conn_listen_server_tls_dispose(conn_listen_server_t *self);

#endif /* __CONNECTION_H__ */
