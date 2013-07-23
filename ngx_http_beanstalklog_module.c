#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include <beanstalk.h>
#include <errno.h>
#include <fcntl.h>

#define BEANSTALKLOG_VERSION   "1.0.0"

/* not standard */
#define BS_STATUS_DISCONNECTED -254

typedef struct {
    int bsl_socket;
    ngx_str_t bsl_host;
    ngx_str_t bsl_tube;
    ngx_uint_t bsl_port;
    ngx_uint_t bsl_connect_timeout;
    ngx_uint_t bsl_priority;
    ngx_uint_t bsl_delay;
    ngx_uint_t bsl_ttr;
    ngx_flag_t enable;
} ngx_http_beanstalklog_main_conf_t;

static void *ngx_http_beanstalklog_create_main_conf(ngx_conf_t *cf);
static char *ngx_http_beanstalklog_init_main_conf(ngx_conf_t *cf, void *conf);

static ngx_int_t ngx_http_beanstalklog_init(ngx_conf_t *cf);

static ngx_int_t ngx_http_beanstalklog_handler(ngx_http_request_t *r);
static ngx_int_t ngx_http_beanstalklog_module_init(ngx_cycle_t *cycle);
static void ngx_http_read_client_request_body_handler(ngx_http_request_t *r);

int ngx_http_beanstalklog_socket_poll(int rw, int fd);

static ngx_command_t ngx_http_beanstalklog_commands[] = {
    {
    ngx_string("beanstalklog"),
    NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
    ngx_conf_set_flag_slot,
    NGX_HTTP_MAIN_CONF_OFFSET,
    offsetof(ngx_http_beanstalklog_main_conf_t, enable),
    NULL
    },
    {
    ngx_string("beanstalklog_host"),
    NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
    ngx_conf_set_str_slot,
    NGX_HTTP_MAIN_CONF_OFFSET,
    offsetof(ngx_http_beanstalklog_main_conf_t, bsl_host),
    NULL
    },
    {
    ngx_string("beanstalklog_tube"),
    NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
    ngx_conf_set_str_slot,
    NGX_HTTP_MAIN_CONF_OFFSET,
    offsetof(ngx_http_beanstalklog_main_conf_t, bsl_tube),
    NULL
    },
    {
    ngx_string("beanstalklog_port"),
    NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
    ngx_conf_set_num_slot,
    NGX_HTTP_MAIN_CONF_OFFSET,
    offsetof(ngx_http_beanstalklog_main_conf_t, bsl_port),
    NULL
    },
    {
    ngx_string("beanstalklog_connect_timeout"),
    NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
    ngx_conf_set_num_slot,
    NGX_HTTP_MAIN_CONF_OFFSET,
    offsetof(ngx_http_beanstalklog_main_conf_t, bsl_connect_timeout),
    NULL
    },
    {
    ngx_string("beanstalklog_priority"),
    NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
    ngx_conf_set_num_slot,
    NGX_HTTP_MAIN_CONF_OFFSET,
    offsetof(ngx_http_beanstalklog_main_conf_t, bsl_priority),
    NULL
    },
    {
    ngx_string("beanstalklog_delay"),
    NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
    ngx_conf_set_num_slot,
    NGX_HTTP_MAIN_CONF_OFFSET,
    offsetof(ngx_http_beanstalklog_main_conf_t, bsl_delay),
    NULL
    },
    {
    ngx_string("beanstalklog_ttr"),
    NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
    ngx_conf_set_num_slot,
    NGX_HTTP_MAIN_CONF_OFFSET,
    offsetof(ngx_http_beanstalklog_main_conf_t, bsl_ttr),
    NULL
    },
    ngx_null_command
};

static ngx_http_module_t ngx_http_beanstalklog_ctx = {
    NULL, /* preconfiguration */
    ngx_http_beanstalklog_init, /* postconfiguration */
    ngx_http_beanstalklog_create_main_conf, /* create main configuration */
    ngx_http_beanstalklog_init_main_conf,  /* init main configuration */
    NULL, /* create server configuration */
    NULL, /* merge server configuration */
    NULL, /* create location configuration */
    NULL /* merge location configuration */
};

ngx_module_t ngx_http_beanstalklog_module = {
    NGX_MODULE_V1,
    &ngx_http_beanstalklog_ctx, /* module context */
    ngx_http_beanstalklog_commands, /* module directives */
    NGX_HTTP_MODULE, /* module type */
    NULL, /* init master */
    ngx_http_beanstalklog_module_init, /* init module */
    NULL, /* init process */
    NULL, /* init thread */
    NULL, /* exit thread */
    NULL, /* exit process */
    NULL, /* exit master */
    NGX_MODULE_V1_PADDING
};

static ngx_int_t ngx_http_beanstalklog_init(ngx_conf_t *cf)
{
    ngx_http_handler_pt *h;
    ngx_http_core_main_conf_t *cmcf;

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);

    /* install handler in pre access phase */
    h = ngx_array_push(&cmcf->phases[NGX_HTTP_PREACCESS_PHASE].handlers);

    if (h == NULL) {
        return NGX_ERROR;
    }

    *h = ngx_http_beanstalklog_handler;

    return NGX_OK;
}

static void * ngx_http_beanstalklog_create_main_conf(ngx_conf_t *cf)
{
    ngx_http_beanstalklog_main_conf_t *bmcf;

    bmcf = ngx_pcalloc(cf->pool, sizeof(ngx_http_beanstalklog_main_conf_t));

    if (bmcf == NULL) {
        return NULL;
    }
    bmcf->bsl_port = NGX_CONF_UNSET_UINT;
    bmcf->bsl_connect_timeout = NGX_CONF_UNSET_UINT;
    bmcf->enable = NGX_CONF_UNSET;

    return bmcf;
}

static char * ngx_http_beanstalklog_init_main_conf(ngx_conf_t *cf, void *conf)
{
    ngx_http_beanstalklog_main_conf_t *bmcf = conf;

    if (bmcf->enable == NGX_CONF_UNSET) {
        bmcf->enable = 0;
    }

    if (bmcf->enable) {
        if (bmcf->bsl_host.len == 0) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "no beanstalklog_host set to log requests");
            return NGX_CONF_ERROR;
        }

        if (bmcf->bsl_tube.len == 0) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "no beanstalklog_tube set");
            return NGX_CONF_ERROR;
        }

        /* set default port */
        if (bmcf->bsl_port == NGX_CONF_UNSET_UINT){
            bmcf->bsl_port = 11300;
        }

        /* set default connect timeout */
        if (bmcf->bsl_connect_timeout == NGX_CONF_UNSET_UINT){
            bmcf->bsl_connect_timeout = 2;
        }

        /* set default priority */
        if (bmcf->bsl_priority == NGX_CONF_UNSET_UINT){
            bmcf->bsl_priority = 0;
        }
        /* set default delay */
        if (bmcf->bsl_delay == NGX_CONF_UNSET_UINT){
            bmcf->bsl_delay = 0;
        }
        /* set default ttr */
        if (bmcf->bsl_ttr == NGX_CONF_UNSET_UINT){
            bmcf->bsl_ttr = 60;
        }
    }

    /* start with disconnected state */
    bmcf->bsl_socket = BS_STATUS_DISCONNECTED;

    return NGX_CONF_OK;
}

int ngx_http_beanstalklog_socket_poll(int rw, int fd) {
    fd_set read_set, write_set;
    FD_ZERO(&read_set);
    FD_ZERO(&write_set);

    if ((rw & 1) == 1)
        FD_SET(fd, &read_set);
    if ((rw & 2) == 2)
        FD_SET(fd, &write_set);

    return select(fd + 1, &read_set, &write_set, 0, 0) > 0 ? 1 : 0;
}

static void ngx_http_read_client_request_body_handler(ngx_http_request_t *r)
{
    ngx_http_beanstalklog_main_conf_t *bmcf;
    ngx_str_t full_request;
    ngx_list_part_t *part;
    ngx_table_elt_t *h;
    ngx_uint_t i;
    ngx_chain_t *cn;
    ngx_str_t crlf = ngx_string("\r\n");
    ngx_str_t hsp = ngx_string(": ");
    size_t offset;
    ngx_int_t rc;
    int sv_errno;

    ngx_log_debug(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "beanstalklog: ngx_htt_read_client_request_body_handler");

    bmcf = ngx_http_get_module_main_conf(r, ngx_http_beanstalklog_module);

    if (bmcf == NULL) {
        return ngx_http_finalize_request(r, NGX_OK);
    }

    full_request.data = (u_char *) ngx_palloc(r->pool, r->request_length);

    if (full_request.data == NULL) {
        return ngx_http_finalize_request(r, NGX_OK);
    }

    full_request.len = r->request_length;

    /* copy request line */
    ngx_memcpy(full_request.data, r->request_line.data, r->request_line.len);
    offset = r->request_line.len;

    ngx_memcpy(full_request.data + offset * sizeof(u_char), crlf.data, crlf.len);
    offset += crlf.len;

    /* copy headers */
    part = &r->headers_in.headers.part;
    h = part->elts;

    for (i = 0; /* void */ ; i++) {
        if (i >= part->nelts) {
            if (part->next == NULL) {
                /* The last part, search is done. */
                break;
            }
            part = part->next;
            h = part->elts;
            i = 0;
        }

        ngx_memcpy(full_request.data + offset * sizeof(u_char), h[i].key.data, h[i].key.len);
        offset += h[i].key.len;

        ngx_memcpy(full_request.data + offset * sizeof(u_char), hsp.data, hsp.len);
        offset += hsp.len;

        ngx_memcpy(full_request.data + offset * sizeof(u_char), h[i].value.data, h[i].value.len);
        offset += h[i].value.len;

        ngx_memcpy(full_request.data + offset * sizeof(u_char), crlf.data, crlf.len);
        offset += crlf.len;
    }

    /* copy body */
    if (!(r->request_body == NULL || r->request_body->temp_file || r->request_body-> bufs == NULL)) {
        ngx_memcpy(full_request.data + offset * sizeof(u_char), crlf.data, crlf.len);
        offset += crlf.len;
        for (cn = r->request_body->bufs; cn; cn = cn->next) {
            ngx_memcpy(full_request.data + offset * sizeof(u_char), cn->buf->pos, cn->buf->last - cn->buf->pos);
            offset += cn->buf->last - cn->buf->pos;
        }
    }

    rc = bs_put(bmcf->bsl_socket, bmcf->bsl_priority, bmcf->bsl_delay, bmcf->bsl_ttr, (char *) full_request.data, offset);

    if (rc == BS_STATUS_FAIL) {
        sv_errno = errno;
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "beanstalklog: bs_put failed, returned %d, errno %d", rc, sv_errno);

        /* check if we are disconnected */
        switch(sv_errno) {
            case ENOENT:
            case EPIPE:
            case ECONNRESET:
            case ENOTCONN:
            case ENOTSOCK:
                /* reset polling, disconnect the socket and set socket as disconneted */
                bs_reset_polling();
                bs_disconnect(bmcf->bsl_socket);
                bmcf->bsl_socket = BS_STATUS_DISCONNECTED;
                ngx_log_debug(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "beanstalklog: disconnected socket");
        }
    } else {
        ngx_log_debug(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "beanstalklog: submitted job id %d", rc);
    }

    ngx_pfree(r->pool, full_request.data);
    ngx_http_finalize_request(r, NGX_OK);
}

static ngx_int_t ngx_http_beanstalklog_handler(ngx_http_request_t *r)
{
    ngx_http_beanstalklog_main_conf_t *bmcf;
    ngx_int_t rc;

    ngx_log_debug(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "beanstalklog: in handler");

    bmcf = ngx_http_get_module_main_conf(r, ngx_http_beanstalklog_module);

    if (bmcf == NULL){
        return NGX_DECLINED;
    }

    /* not enabled nothing to do */
    if (!bmcf->enable) {
        return NGX_DECLINED;
    }

    /* check if we are connected */
    if (bmcf->bsl_socket == BS_STATUS_DISCONNECTED) {
        bmcf->bsl_socket = bs_connect_with_timeout((char *) bmcf->bsl_host.data, bmcf->bsl_port, bmcf->bsl_connect_timeout);

        /* failed to connect */
        if (bmcf->bsl_socket == BS_STATUS_FAIL) {
            bmcf->bsl_socket = BS_STATUS_DISCONNECTED;

            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "beanstalklog: failed to connect to beanstalkd on %s:%d, errno %d",
                          bmcf->bsl_host.data, bmcf->bsl_port, errno);

            /* beanstalkd maybe down intentionally */
            return NGX_DECLINED;
        } else {
            ngx_log_debug(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "beanstalklog: connected to beanstalkd on %s:%d",
                          bmcf->bsl_host.data, bmcf->bsl_port);

            /* start polling */
            bs_start_polling(ngx_http_beanstalklog_socket_poll);

            /* set socket as non blocking */
            fcntl(bmcf->bsl_socket, F_SETFL, fcntl(bmcf->bsl_socket, F_GETFL) | O_NONBLOCK);

            /* set tube to use */
            rc = bs_use(bmcf->bsl_socket, (char *) bmcf->bsl_tube.data);

            if (rc != BS_STATUS_OK) {
                ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "beanstalklog: failed to use tube %s, errno %d, rc %d",
                             bmcf->bsl_tube.data, errno, rc);

                /* reset polling, disconnect the socket and set socket as disconneted */
                bs_reset_polling();
                bs_disconnect(bmcf->bsl_socket);
                bmcf->bsl_socket = BS_STATUS_DISCONNECTED;
                return NGX_DECLINED;
            } else {
                ngx_log_debug(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "beanstalklog: using tube %s", bmcf->bsl_tube.data);
            }
        }
    }

    /* install post request body read handler */
    rc = ngx_http_read_client_request_body(r, ngx_http_read_client_request_body_handler);

    if (rc >= NGX_HTTP_SPECIAL_RESPONSE) {
        return rc;
    }

    return NGX_DECLINED;
}

static ngx_int_t ngx_http_beanstalklog_module_init(ngx_cycle_t *cycle)
{
    int a, b, c;
    bs_version(&a, &b, &c);
    ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0,
                "beanstalklog: module version %s, using beanstalk-client version %d.%d.%d",
                 BEANSTALKLOG_VERSION, a, b, c);
    return NGX_OK;
}
