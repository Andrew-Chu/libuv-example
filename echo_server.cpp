#include <stdio.h>
#include <stdlib.h>

#include <uv.h>
// ------------------------------------------------------------------------------------------------
typedef struct {
    uv_write_t req;
    uv_buf_t   buf;
} write_req_t;
// ------------------------------------------------------------------------------------------------
void free_write_req(uv_write_t* req);
void cb_alloc_buffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);
void cb_echo_write(uv_write_t* req, int status);
void cb_echo_read(uv_stream_t* client, ssize_t nread, const uv_buf_t* buf);
void cb_new_connection(uv_stream_t* server, int status);
// ------------------------------------------------------------------------------------------------
int main() {
    uv_loop_t* loop = uv_default_loop();

    uv_tcp_t server;
    uv_tcp_init(loop, &server); // no socket is created of yet

    sockaddr_in addr;
    uv_ip4_addr("127.0.0.1", 12345, &addr);

    uv_tcp_bind(&server, (const struct sockaddr*)&addr, 0);

    int r = uv_listen((uv_stream_t*)&server, 2, cb_new_connection);
    if (r) {
        fprintf(stderr, "Listen error %s\n", uv_strerror(r));
        return 1;
    }
    return uv_run(loop, UV_RUN_DEFAULT);
}
// ------------------------------------------------------------------------------------------------
void free_write_req(uv_write_t* req) {
    write_req_t* wr = (write_req_t*)req;
    free(wr->buf.base);
    free(wr);
}

void cb_alloc_buffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    buf->base = (char*)malloc(suggested_size);
    buf->len  = suggested_size;
}

// Logic which handles the write result
void cb_echo_write(uv_write_t* req, int status) {
    if (status) {
        fprintf(stderr, "Write error %s\n", uv_strerror(status));
    }
    free_write_req(req);
}

void cb_echo_read(uv_stream_t* client, ssize_t nread, const uv_buf_t* buf) {
    if (nread > 0) { // nread is > 0 if there is data available
        write_req_t* req = (write_req_t*)malloc(sizeof(write_req_t));
        req->buf         = uv_buf_init(buf->base, nread);
        uv_write((uv_write_t*)req, client, &req->buf, 1, cb_echo_write);
        return;
    }
    if (nread < 0) { // or < 0 on error
        if (nread != UV_EOF)
            fprintf(stderr, "Read error %s\n", uv_err_name(nread));
        uv_close((uv_handle_t*)client, NULL); // Request handle to be closed.
    }

    free(buf->base);
}

// cb_new_connection callback
void cb_new_connection(uv_stream_t* server, int status) {
    if (status < 0) {
        fprintf(stderr, "New connection error %s\n", uv_strerror(status));
        return;
    }
    uv_loop_t* loop = uv_default_loop();

    uv_tcp_t* client = (uv_tcp_t*)malloc(sizeof(uv_tcp_t));
    uv_tcp_init(loop, client);
    if (uv_accept(server, (uv_stream_t*)client) == 0) {
        uv_read_start((uv_stream_t*)client, cb_alloc_buffer, cb_echo_read);
    }
    else { // not accept
        uv_close((uv_handle_t*)client, NULL);
    }
}
