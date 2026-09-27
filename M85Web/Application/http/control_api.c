/* Debug only: print every raw HTTP request/response body (build with
 * -DCONTROL_HTTP_RAW_LOG_ENABLE=1). */
#ifndef CONTROL_HTTP_RAW_LOG_ENABLE
#define CONTROL_HTTP_RAW_LOG_ENABLE 0
#endif
#if CONTROL_HTTP_RAW_LOG_ENABLE
#include <tm/tmonitor.h>
#endif
#include "../interface/controller_if.h"
#include "../protocol/control_json.h"
#include "control_api.h"
#include "lwip/apps/fs.h"
#include "lwip/apps/httpd.h"
#include "lwip/pbuf.h"
#include <stdio.h>
#include <string.h>
#include <tk/tkernel.h>
/* All HTTP state below is owned exclusively by tcpip_thread. */
static struct
{
    void *connection;
    size_t expected, used;
    bool failed;
    const char *error_uri;
    char body[CONTROL_JSON_LIMIT];
} post;
#define RESPONSE_SLOTS 4
static struct
{
    bool busy;
    char wire[192 + CONTROL_JSON_LIMIT];
} replies[RESPONSE_SLOTS];
/* This capability is consumed by fs_open_custom synchronously after finished.
 * A browser cannot GET the internal URI to obtain a successful API response. */
static bool response_pending;
#define RESPONSE_URI "/_control_reply.json"
#define ERROR_URI_PREFIX "/_control_error/"
static const char bad[] =
    "HTTP/1.0 400 Bad Request\r\nContent-Type: application/json\r\nContent-Length: "
    "23\r\nConnection: close\r\n\r\n{\"error\":\"bad_request\"}";
static const char large[] =
    "HTTP/1.0 413 Payload Too Large\r\nContent-Type: application/json\r\nContent-Length: "
    "29\r\nConnection: close\r\n\r\n{\"error\":\"payload_too_large\"}";
static const char media[] =
    "HTTP/1.0 415 Unsupported Media Type\r\nContent-Type: application/json\r\nContent-Length: "
    "34\r\nConnection: close\r\n\r\n{\"error\":\"unsupported_media_type\"}";
static const char missing[] =
    "HTTP/1.0 404 Not Found\r\nContent-Type: application/json\r\nContent-Length: 21\r\nConnection: "
    "close\r\n\r\n{\"error\":\"not_found\"}";
static const char method[] = "HTTP/1.0 405 Method Not Allowed\r\nAllow: POST\r\nContent-Type: "
                             "application/json\r\nContent-Length: 30\r\nConnection: "
                             "close\r\n\r\n{\"error\":\"method_not_allowed\"}";
static const char busy[] =
    "HTTP/1.0 503 Service Unavailable\r\nContent-Type: application/json\r\nContent-Length: "
    "16\r\nConnection: close\r\n\r\n{\"error\":\"busy\"}";
/* HTTPD cannot pass unsupported methods/URIs to the POST extension. Its
 * extended-status fallback serves this 501 page before application routing. */
static const char unimplemented[] =
    "HTTP/1.0 501 Not Implemented\r\nContent-Type: application/json\r\n"
    "Content-Length: 27\r\nConnection: close\r\n\r\n{\"error\":\"not_implemented\"}";
static void uri_copy(char *uri, u16_t cap, const char *name)
{
    if (cap)
    {
        size_t n = strlen(name);
        if (n >= cap)
            n = cap - 1;
        memcpy(uri, name, n);
        uri[n] = 0;
    }
}
static bool equal_ci(const char *a, size_t n, const char *b)
{
    if (n != strlen(b))
        return false;
    for (size_t i = 0; i < n; ++i)
    {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z')
            x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z')
            y += 'a' - 'A';
        if (x != y)
            return false;
    }
    return true;
}
/* Header buffer is bounded and is not guaranteed to be NUL terminated. HTTPD
 * replaces the final CRLF with NUL before invoking begin. */
static bool json_content_type(const char *headers, size_t len)
{
    size_t pos = 0;
    unsigned found = 0;
    bool good = false;
    while (pos < len && headers[pos])
    {
        size_t start = pos;
        while (pos < len && headers[pos] && headers[pos] != '\r' && headers[pos] != '\n')
            ++pos;
        size_t end = pos, colon = start;
        while (colon < end && headers[colon] != ':')
            ++colon;
        if (colon < end && equal_ci(headers + start, colon - start, "Content-Type"))
        {
            ++found;
            size_t value = colon + 1;
            while (value < end && (headers[value] == ' ' || headers[value] == '\t'))
                ++value;
            size_t stop = value;
            while (stop < end && headers[stop] != ';' && headers[stop] != ' ' &&
                   headers[stop] != '\t')
                ++stop;
            good = equal_ci(headers + value, stop - value, "application/json");
            while (stop < end && (headers[stop] == ' ' || headers[stop] == '\t'))
                ++stop;
            if (stop < end && headers[stop] != ';')
                good = false;
        }
        while (pos < len && (headers[pos] == '\r' || headers[pos] == '\n'))
            ++pos;
    }
    return found == 1 && good;
}
void control_api_init(void)
{
    memset(&post, 0, sizeof(post));
    memset(replies, 0, sizeof(replies));
    response_pending = false;
}
err_t httpd_post_begin(void *connection, const char *uri, const char *headers, u16_t headers_len,
                       int content_len, char *response_uri, u16_t response_uri_len, u8_t *auto_wnd)
{
    const char *error = NULL;
    *auto_wnd = 1;
    if (strcmp(uri, "/api/control"))
        error = ERROR_URI_PREFIX "404";
    else if (content_len < 0)
        error = ERROR_URI_PREFIX "400";
    else if (content_len > (int)CONTROL_JSON_LIMIT)
        error = ERROR_URI_PREFIX "413";
    else if (!json_content_type(headers, headers_len))
        error = ERROR_URI_PREFIX "415";
    else if (post.connection)
        error = ERROR_URI_PREFIX "503";
    if (post.connection || content_len < 0)
    {
        uri_copy(response_uri, response_uri_len,
                 post.connection ? ERROR_URI_PREFIX "503" : ERROR_URI_PREFIX "400");
        return ERR_ARG;
    }
    /* Drain rejected bodies through the standard POST callbacks, without
     * storing them. Closing while a separately transmitted body is unread
     * can reset TCP and prevent the peer receiving the error response. */
    post.error_uri = error;
    post.connection = connection;
    post.expected = (size_t)content_len;
    post.used = 0;
    post.failed = false;
    return ERR_OK;
}
err_t httpd_post_receive_data(void *connection, struct pbuf *p)
{
    err_t result = ERR_OK;
    if (connection != post.connection)
        result = ERR_ARG;
    else
    {
        for (struct pbuf *q = p; q; q = q->next)
        {
            if (q->len > post.expected - post.used ||
                (!post.error_uri && q->len > sizeof(post.body) - post.used))
            {
                post.failed = true;
                break;
            }
            if (!post.error_uri)
                memcpy(post.body + post.used, q->payload, q->len);
            post.used += q->len;
        }
    }
    /* HTTPD transfers ownership of every pbuf, including rejected bodies. */
    if (p)
        pbuf_free(p);
    return result;
}
void httpd_post_finished(void *connection, char *response_uri, u16_t cap)
{
    control_request_t request;
    SYSTIM time;
    if (connection != post.connection)
    {
        uri_copy(response_uri, cap, ERROR_URI_PREFIX "400");
        return;
    }
    if (post.error_uri)
    {
        const char *error_uri = post.error_uri;
        post.connection = NULL;
        uri_copy(response_uri, cap, error_uri);
        return;
    }
    bool valid = !post.failed && post.used == post.expected &&
                 control_json_decode(post.body, post.used, &request);
    post.connection = NULL;
    if (!valid)
    {
        uri_copy(response_uri, cap, ERROR_URI_PREFIX "400");
        return;
    }
    if (tk_get_otm(&time) != E_OK || !control_if_set_request(&request, time.lo))
    {
        uri_copy(response_uri, cap, ERROR_URI_PREFIX "503");
        return;
    }
#if CONTROL_HTTP_RAW_LOG_ENABLE
    tm_printf((UB *)"[HTTP RAW RX] %.*s\n", (int)post.used, post.body);
#endif
    response_pending = true;
    uri_copy(response_uri, cap, RESPONSE_URI);
}
static void file_set(struct fs_file *file, const char *data, size_t len)
{
    file->data = data;
    file->len = (int)len;
    file->index = (int)len;
    file->flags = FS_FILE_FLAGS_HEADER_INCLUDED;
    file->pextension = NULL;
}
int fs_open_custom(struct fs_file *file, const char *name)
{
    const char *error = NULL;
    if (!strcmp(name, "/501.html"))
        error = unimplemented;
    else if (!strcmp(name, "/400.html"))
        error = bad;
    else if (!strcmp(name, "/api/control"))
        error = method;
    else if (!strcmp(name, ERROR_URI_PREFIX "400"))
        error = bad;
    else if (!strcmp(name, ERROR_URI_PREFIX "413"))
        error = large;
    else if (!strcmp(name, ERROR_URI_PREFIX "415"))
        error = media;
    else if (!strcmp(name, ERROR_URI_PREFIX "404"))
        error = missing;
    else if (!strcmp(name, ERROR_URI_PREFIX "503"))
        error = busy;
    else if (!strcmp(name, RESPONSE_URI))
    {
        if (!response_pending)
            error = missing;
        else
        {
            response_pending = false;
            for (unsigned i = 0; i < RESPONSE_SLOTS; ++i)
                if (!replies[i].busy)
                {
                    control_response_t state;
                    char body[CONTROL_JSON_LIMIT];
                    size_t n;
                    if (!control_if_get_response(&state) ||
                        !control_json_encode(&state, body, sizeof(body), &n))
                    {
                        error = busy;
                        break;
                    }
#if CONTROL_HTTP_RAW_LOG_ENABLE
                    tm_printf((UB *)"[HTTP RAW TX] %.*s\n", (int)n, body);
#endif
                    int h = snprintf(
                        replies[i].wire, 192,
                        "HTTP/1.0 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                        "%u\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n",
                        (unsigned)n);
                    if (h < 0 || h >= 192)
                    {
                        error = busy;
                        break;
                    }
                    memcpy(replies[i].wire + h, body, n);
                    replies[i].busy = true;
                    file_set(file, replies[i].wire, (size_t)h + n);
                    file->pextension = &replies[i];
                    return 1;
                }
            if (!error)
                error = busy;
        }
    }
    if (error)
    {
        file_set(file, error, strlen(error));
        return 1;
    }
    return 0;
}
void fs_close_custom(struct fs_file *file)
{
    for (unsigned i = 0; i < RESPONSE_SLOTS; ++i)
        if (file->pextension == &replies[i])
        {
            replies[i].busy = false;
            break;
        }
}
