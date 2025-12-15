/**
 * Implementation file: connection.c
 * Contains wired up vtables for tcp and tls functions
 * Implementation of factory functions to create new clients
 * and listening servers
 **/

#include "../include/connection.h"
#include "../global_defines.h"

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* forward declarations */
void static conn_listen_server_base_cleanup(conn_listen_server_t *self);
void static conn_listen_server_tcp_cleanup(conn_listen_server_t *self);
void static conn_listen_server_tls_cleanup(conn_listen_server_t *self);
void conn_listen_server_dispose(conn_listen_server_t *self);

////////////////////////////////////////
// VTABLE DEFINITION
////////////////////////////////////////

/* set each vtable to correct functions */
const conn_vtable_t TCP_CONN_VTABLE =
{
	.read  = conn_tcp_read,
	.write = conn_tcp_write,
	.close = conn_tcp_close	
};

const conn_vtable_t TLS_CONN_VTABLE =
{
	.read  = conn_tls_read,
	.write = conn_tls_write,
	.close = conn_tls_close
};

const conn_listen_server_vtable_t TCP_LISTEN_SERVER_VTABLE =
{
	.accept_client = conn_listen_server_tcp_accept_factory,
	.dispose       = conn_listen_server_tcp_dispose
};

const conn_listen_server_vtable_t TLS_LISTEN_SERVER_VTABLE =
{
	.accept_client = conn_listen_server_tls_accept_factory,
	.dispose       = conn_listen_server_tls_dispose
};

////////////////////////////////////////
// HELPER FUNCTION
////////////////////////////////////////

static int conn_set_nonblocking(int fd)
{
	/* return fd flags */
	int fd_flags = fcntl(fd, F_GETFL, 0);
	if (fd_flags < 0) return -1;
	/* set fd to nonblocking (flags | MACRO) < sets the bits in flags */
	return fcntl(fd, F_SETFL, fd_flags | O_NONBLOCK);
}

static int conn_bind_fd(const char *port)
{
	struct addrinfo hints = {0}, *res = NULL;
	hints.ai_family   = AF_UNSPEC;   /* IPv4 or IPv6 */
	hints.ai_socktype = SOCK_STREAM; /* TCP */
	hints.ai_flags    = AI_PASSIVE;  /* suitable for binding */

	if (getaddrinfo(NULL, port, &hints, &res) != 0) return -1;

	int listen_fd = -1;
	int yes       = 1;
	for (struct addrinfo *rp = res; rp != NULL; rp = rp->ai_next)
	{
		listen_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (listen_fd < 0)
		{
			continue;
		}
		/* yes is boolean, turns ON reuseaddr @ socket level */
		setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
		if (bind(listen_fd, rp->ai_addr, rp->ai_addrlen) == 0)
		{
			break; 
		}
		close(listen_fd);
		listen_fd = -1;
	}
	freeaddrinfo(res);
	return listen_fd;
}

////////////////////////////////////////
// CLEANUP IMPLEMENTATION
////////////////////////////////////////

static void conn_listen_server_base_cleanup(conn_listen_server_t *self)
{
    if (self->task)
	{
        smw_destroyTask(self->task);
        self->task = NULL;
    }
    if (self->listen_fd >= 0)
	{
        close(self->listen_fd);
        self->listen_fd = -1;
    }
    free(self);
}

static void conn_listen_server_tcp_cleanup(conn_listen_server_t *self)
{
    conn_listen_server_base_cleanup(self);
}

static void conn_listen_server_tls_cleanup(conn_listen_server_t *self)
{
    conn_listen_server_tls_t *tls_server = (conn_listen_server_tls_t*)self;
    // Clean up mbedTLS global contexts
    mbedtls_ssl_config_free(&tls_server->conf);
    mbedtls_x509_crt_free(&tls_server->srvcert);
    mbedtls_pk_free(&tls_server->pkey);
    mbedtls_ctr_drbg_free(&tls_server->ctr_drbg);
    mbedtls_entropy_free(&tls_server->entropy);
    
    conn_listen_server_base_cleanup(self);
}

void conn_listen_server_tcp_dispose(conn_listen_server_t *self)
{
    conn_listen_server_tcp_cleanup(self);
}

void conn_listen_server_tls_dispose(conn_listen_server_t *self)
{
    conn_listen_server_tls_cleanup(self);
}

void conn_listen_server_dispose(conn_listen_server_t *self)
{
    if (self && self->vtable && self->vtable->dispose)
	{
        self->vtable->dispose(self);
    }
}

////////////////////////////////////////
// MAIN WORK FUNCTION
////////////////////////////////////////
/* checking rate limiting here instead of inside accept
   also handles polymorphic dispatch and hand-off to http
*/
void conn_listen_server_taskwork(void *ctx, uint64_t montime)
{
	conn_listen_server_t *server = (conn_listen_server_t*)ctx;
	/* check limit */
	if (montime >= server->recent_connections_time + TCPServer_MAX_CONNECTIONS_WINDOW_SECONDS * 1000)
	{
		server->recent_connections      = 0;
		server->recent_connections_time = montime;		
	}
	if (server->recent_connections >= TCPServer_MAX_CONNECTIONS_PER_WINDOW)
	{
		/* we've accepted to many clients, do nothing */
		return;
	}

	/* polymorphic factory call to accept */
	conn_t *new_conn = server->vtable->accept_client(server);
	if (new_conn)
	{
		/* we got another client */
		server->recent_connections++;
		if (server->on_accept)
		{
			/* call back to http layer */
			server->on_accept(new_conn, server->user_ctx);
		}
		else
		{
			/* no cb (should not happen), close connection to avoid leaks */
			new_conn->vtable->close(new_conn);
		}
	}
}

////////////////////////////////////////
// TCP IMPLEMENTATION
////////////////////////////////////////

conn_t *conn_listen_server_tcp_accept_factory(conn_listen_server_t *self)
{
	int client_fd = accept(self->listen_fd, NULL, NULL);
	if (client_fd < 0)
	{
		if (errno == EAGAIN || errno == EWOULDBLOCK)
		{
			/* no new client */
			return NULL;
		}
		perror("accept tcp");
		return NULL;
	}
	/* set new connection to nonblocking */
	conn_set_nonblocking(client_fd);	
	/* allocate for new connection, freed in conn_tcp_close */
	conn_tcp_t *new_conn = (conn_tcp_t*)malloc(sizeof(conn_tcp_t));
	if (!new_conn)
	{
		close(client_fd);
		return NULL;
	}
	/* wire it up */
	new_conn->base.vtable = &TCP_CONN_VTABLE;
	new_conn->base.client_fd = client_fd;
	return &new_conn->base;
}

int conn_tcp_read(conn_t *self, void *buf, int count)
{
	return recv(self->client_fd, buf, count, 0);
}
int conn_tcp_write(conn_t *self, const void *buf, int count)
{
	return send(self->client_fd, buf, count, 0);
}
void conn_tcp_close(conn_t *self)
{
	close(self->client_fd);
	/* this frees allocation done in tcp factory */
	free(self);
}

////////////////////////////////////////
// TLS IMPLEMENTATION (mbed TLS)
////////////////////////////////////////

conn_t *conn_listen_server_tls_accept_factory(conn_listen_server_t *self)
{
	conn_listen_server_tls_t *server_tls = (conn_listen_server_tls_t*)self;
	int client_fd = accept(self->listen_fd, NULL, NULL);
	if (client_fd < 0)
	{
		if (errno == EAGAIN || errno == EWOULDBLOCK)
		{
			/* no new client */
			return NULL;
		}
		perror("accept tls");
		return NULL;
	}

	conn_set_nonblocking(client_fd);
	/* allocate for new connection, freed in conn_tcp_close */
	conn_tls_t *new_conn_tls = (conn_tls_t*)malloc(sizeof(conn_tls_t));
	if (!new_conn_tls)
	{
		close(client_fd);
		return NULL;
	}
	/* wire it up */
	new_conn_tls->base.vtable    = &TLS_CONN_VTABLE;
	new_conn_tls->base.client_fd = client_fd;
	/* init tls for this connection */
	/* Initialize a context Just makes the context ready to be used or freed safely. */
	mbedtls_net_init(&new_conn_tls->net);
	/* Initialize an SSL context Just makes the context ready for mbedtls_ssl_setup() or mbedtls_ssl_free() */
	mbedtls_ssl_init(&new_conn_tls->ssl);
	/* mbedtls_net_context: Wrapper type for sockets. */
	new_conn_tls->net.fd = client_fd;
	/* Set up an SSL context for use.
	   param:  ssl  - SSL context
	   param:  conf - SSL configuration to use
	   return: 0 if succesfull  */	  
	if (mbedtls_ssl_setup(&new_conn_tls->ssl, &server_tls->conf) != 0)
	{
		close(client_fd);
		free(new_conn_tls);
		return NULL;
	}
	/* Set the underlying BIO callbacks for write, read and read-with-timeout.
	   param: ssl    – SSL context
	   param: p_bio  – parameter (context) shared by BIO callbacks
	   param: f_send – write callback
	   param: f_recv – read callback
	   param: f_recv_timeout – blocking read callback with timeout.
	 */
	mbedtls_ssl_set_bio(&new_conn_tls->ssl,
		                &new_conn_tls->net,
		                mbedtls_net_send,
		                mbedtls_net_recv,
		                NULL); /* we're nonblocking */
	/* we asume handshake is done and return, if handshake is missing
	   when HTTPServerConnection calls conn_tls_read() for the first time
	   mbedtls_ssl_read() will detect the handskae is missing and attempt
	   to perform it, lazy-mode is e-z mode */
	return &new_conn_tls->base;
	
}
int conn_tls_read(conn_t *self, void *buf, int count)
{
	conn_tls_t *tls = (conn_tls_t*)self;
	/* Read at most ‘len’ application data bytes.
	   param:  ssl – SSL context
	   param:  buf – buffer that will hold the data
	   param:  len – maximum number of bytes to read
	   return: The (positive) number of bytes read if successful.
	 */
	int bytes_read = mbedtls_ssl_read(&tls->ssl, (unsigned char*)buf, count);
	/* MBEDTLS_ERR_SSL_WANT_READ or MBEDTLS_ERR_SSL_WANT_WRITE if the handshake
	   is incomplete and waiting for data to be available for reading from or
	   writing to the underlying transport - in this case you must call this
	   function again when the underlying transport is ready for the operation.	  
	 */
	if (bytes_read == MBEDTLS_ERR_SSL_WANT_READ ||
		bytes_read == MBEDTLS_ERR_SSL_WANT_WRITE)
	{
		/* no data aviable, return and try again later */
		return 0;
	}
	if (bytes_read < 0)
	{
		/* connection error/ closed */
		return -1;
	}
	
	return bytes_read;
}
int conn_tls_write(conn_t *self, const void *buf, int count)
{
	conn_tls_t *tls = (conn_tls_t*)self;
	int bytes_sent = mbedtls_ssl_write(&tls->ssl, (const unsigned char*)buf, count);
	if (bytes_sent == MBEDTLS_ERR_SSL_WANT_READ ||
		bytes_sent == MBEDTLS_ERR_SSL_WANT_WRITE)
	{
		return 0;
	}
	if (bytes_sent < 0)
	{
		return -1;
	}
	
	return bytes_sent;
}
void conn_tls_close(conn_t *self)
{
	conn_tls_t *tls = (conn_tls_t*)self;
	/* Notify the peer that the connection is being closed.
	   param:  ssl – SSL context
	   return: 0 if successful, or a specific SSL error code.
	 */
	mbedtls_ssl_close_notify(&tls->ssl);
	mbedtls_ssl_free(&tls->ssl);
	mbedtls_net_free(&tls->net);
	close(self->client_fd);
	free(self);
}

////////////////////////////////////////
// INITIALIZATION FUNCTIONS
////////////////////////////////////////

conn_listen_server_t *conn_listen_server_tcp_init(const char *port, OnAcceptCallBack cb, void *ctx)
{
	int listening_fd = conn_bind_fd(port);
	if (listening_fd < 0)
	{
		return NULL;
	}
	if (listen(listening_fd, TCPServer_MAX_CLIENTS) < 0)
	{
		close(listening_fd);
		return NULL;
	}
	conn_set_nonblocking(listening_fd);
	conn_listen_server_tcp_t *new_server = (conn_listen_server_tcp_t*)malloc(sizeof(conn_listen_server_tcp_t));
	if (!new_server)
	{
		close(listening_fd);
		return NULL;
	}
	/* wire up base/ parent */
	new_server->base.vtable    = &TCP_LISTEN_SERVER_VTABLE;
	new_server->base.listen_fd = listening_fd;
	/* setting call back here */
	new_server->base.on_accept = cb;
	new_server->base.user_ctx  = ctx;
	new_server->base.recent_connections      = 0;
	new_server->base.recent_connections_time = 0;
	/* add to scheduler  */
	new_server->base.task = smw_createTask(&new_server->base, conn_listen_server_taskwork);
	
	return &new_server->base;
}

conn_listen_server_t *conn_listen_server_tls_init(const char *port, OnAcceptCallBack cb, void *ctx)
{
	(void)port;	
	int listen_fd = conn_bind_fd(TLS_PORT);
	if (listen_fd < 0)
	{
		return NULL;
	}
	if (listen(listen_fd, TCPServer_MAX_CLIENTS) < 0)
	{
		close(listen_fd);
		return NULL;
	}
	conn_set_nonblocking(listen_fd);
	conn_listen_server_tls_t *new_server = (conn_listen_server_tls_t*)calloc(1, sizeof(conn_listen_server_tls_t));
	if (!new_server)
	{
		close(listen_fd);
		return NULL;
	}
	/* initialization of embedtls global state */
	mbedtls_ssl_config_init(&new_server->conf);
	mbedtls_x509_crt_init(&new_server->srvcert);
	mbedtls_pk_init(&new_server->pkey);
	mbedtls_entropy_init(&new_server->entropy);
	mbedtls_ctr_drbg_init(&new_server->ctr_drbg);
	/* rng and cert setup */
	int rv;
	const char *pers = "https_server";
	/* The personalization string is a small protection against a lack of startup
	   entropy and ensures each application has at least a different starting point.
	*/
	rv = mbedtls_ctr_drbg_seed(&new_server->ctr_drbg, mbedtls_entropy_func,
		                       &new_server->entropy, (const unsigned char *)pers, strlen(pers));
	if (rv != 0)
	{
		printf("TLS failed to seed Random Number Generator (error: %d)\n", rv);
		conn_listen_server_tls_cleanup((conn_listen_server_t*)new_server);
		return NULL;	   
	}
	/* load cert */
	rv = mbedtls_x509_crt_parse_file(&new_server->srvcert, CERT_FILE_PATH);
	if (rv != 0)
	{
		printf("TLS failed to load cert %s (error: %d)\n", CERT_FILE_PATH, rv);
		conn_listen_server_tls_cleanup((conn_listen_server_t*)new_server);
		return NULL;
	}
	/* load private key */
	/* Did not work
	  rv = mbedtls_pk_parse_keyfile(&new_server->pkey, PRIVKEY_FILE_PATH, NULL);*/
	// START FIX: Manually read key file content and use mbedtls_pk_parse_key (robust method)
	// Friendly AI sloop
    FILE *f = NULL;
    long file_size = -1;
    unsigned char *key_buffer = NULL;
    // 1. Open the file
    f = fopen(PRIVKEY_FILE_PATH, "rb");
    if (f == NULL)
	{
        printf("TLS failed to open private key file %s\n", PRIVKEY_FILE_PATH);
        conn_listen_server_tls_cleanup((conn_listen_server_t*)new_server);
        return NULL;
    }
    // 2. Get file size
    fseek(f, 0, SEEK_END);
    file_size = ftell(f);
    rewind(f);
    if (file_size < 0)
	{
        printf("TLS failed to get private key file size %s\n", PRIVKEY_FILE_PATH);
        fclose(f);
        conn_listen_server_tls_cleanup((conn_listen_server_t*)new_server);
        return NULL;
    }
    // 3. Allocate buffer (+1 for null terminator, required by mbedTLS in some cases)
    key_buffer = (unsigned char *)malloc(file_size + 1); 
    if (key_buffer == NULL)
	{
        printf("TLS failed to allocate memory for private key\n");
        fclose(f);
        conn_listen_server_tls_cleanup((conn_listen_server_t*)new_server);
        return NULL;
    }
    // 4. Read file content
    if (fread(key_buffer, 1, file_size, f) != file_size)
	{
        printf("TLS failed to read private key file content %s\n", PRIVKEY_FILE_PATH);
        free(key_buffer);
        fclose(f);
        conn_listen_server_tls_cleanup((conn_listen_server_t*)new_server);
        return NULL;
    }
    fclose(f);
    key_buffer[file_size] = '\0'; // Null-terminate the buffer

	/* load private key from buffer */
    // Use mbedtls_pk_parse_key with 7 arguments
	rv = mbedtls_pk_parse_key(&new_server->pkey, 
							  key_buffer, 
							  file_size + 1, // Pass length including null terminator
							  NULL, 0, NULL, NULL);

    // 5. Cleanup buffer
    free(key_buffer);
	
    // END FIX
	if (rv != 0)
	{
		printf("TLS failed to load private key %s (error: %d)\n", PRIVKEY_FILE_PATH, rv);
		conn_listen_server_tls_cleanup((conn_listen_server_t*)new_server);
		return NULL;
	}
	/* Load reasonable default SSL configuration values.
	   param: conf – SSL configuration context
	   param: endpoint – MBEDTLS_SSL_IS_CLIENT or MBEDTLS_SSL_IS_SERVER
	   param: transport – MBEDTLS_SSL_TRANSPORT_STREAM for TLS, or MBEDTLS_SSL_TRANSPORT_DATAGRAM for DTLS
	   param: preset – a MBEDTLS_SSL_PRESET_XXX value
	   return: 0 if successful, or MBEDTLS_ERR_XXX_ALLOC_FAILED on memory allocation error. 
	 */
	rv = mbedtls_ssl_config_defaults(&new_server->conf,
		                             MBEDTLS_SSL_IS_SERVER,
		                             MBEDTLS_SSL_TRANSPORT_STREAM,
		                             MBEDTLS_SSL_PRESET_DEFAULT);
	if (rv != 0)
	{
		printf("TLS failed to set config defaults (error: %d)\n", rv);
		conn_listen_server_tls_cleanup((conn_listen_server_t*)new_server);
		return NULL;
	}
	/* assign the rng and loaded cert/ key to configuration */
	mbedtls_ssl_conf_rng(&new_server->conf, mbedtls_ctr_drbg_random, &new_server->ctr_drbg);
	mbedtls_ssl_conf_own_cert(&new_server->conf, &new_server->srvcert, &new_server->pkey);
	/* finally wire up base/ parent */
	new_server->base.vtable    = &TLS_LISTEN_SERVER_VTABLE;
	new_server->base.listen_fd = listen_fd;
	new_server->base.on_accept = cb;
	new_server->base.user_ctx  = ctx;
	new_server->base.recent_connections      = 0;
	new_server->base.recent_connections_time = 0;
	new_server->base.task      = smw_createTask(&new_server->base, conn_listen_server_taskwork);
	if (!new_server->base.task)
	{
		printf("TLS failed to create task for TLS listener server\n");
		conn_listen_server_tls_cleanup((conn_listen_server_t*)new_server);
		return NULL;
	}
		
	return &new_server->base;
}


