#include "http_server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#ifdef LWIP_PLATFORM
  #include "lwip/sockets.h"
  #include "lwip/netdb.h"
  #define closesocket(s) lwip_close(s)
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #define closesocket(s) close(s)
#endif

#define RECV_BUF_SIZE 4096
#define RESP_BUF_SIZE 2048

/* MIMEタイプの判別 (KISS: 拡張子の比較のみ) */
static const char* get_mime_type(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";
    if (strcasecmp(dot, ".html") == 0) return "text/html; charset=utf-8";
    if (strcasecmp(dot, ".css") == 0)  return "text/css; charset=utf-8";
    if (strcasecmp(dot, ".js") == 0)   return "application/javascript; charset=utf-8";
    if (strcasecmp(dot, ".json") == 0) return "application/json; charset=utf-8";
    if (strcasecmp(dot, ".png") == 0)  return "image/png";
    if (strcasecmp(dot, ".ico") == 0)  return "image/x-icon";
    return "application/octet-stream";
}

/* KISS JSONパーサ (外部ライブラリ非依存、文字列探索のみ) */
static void parse_command_json(const char *json, VehicleCommand *cmd) {
    memset(cmd, 0, sizeof(*cmd));
    strncpy(cmd->mode_request, "NONE", sizeof(cmd->mode_request) - 1);

    if (!json) return;

    /* 手動中断要求 */
    if (strstr(json, "\"manual_abort_request\": true") ||
        strstr(json, "\"manual_abort_request\":true") ||
        strstr(json, "\"manual_abort_request\": 1")) {
        cmd->manual_abort_request = 1;
    }

    /* 中断解除 (RESET) 要求 */
    if (strstr(json, "\"reset_abort_request\": true") ||
        strstr(json, "\"reset_abort_request\":true") ||
        strstr(json, "\"reset_abort_request\": 1")) {
        cmd->reset_abort_request = 1;
    }

    /* スロットル */
    const char *p = strstr(json, "\"throttle\":");
    if (p) {
        cmd->throttle = (float)strtod(p + 11, NULL);
    }

    /* ステアリング */
    p = strstr(json, "\"steering\":");
    if (p) {
        cmd->steering = (float)strtod(p + 11, NULL);
    }

    /* モード切替要求 */
    p = strstr(json, "\"mode_request\":");
    if (p) {
        const char *q1 = strchr(p + 14, '\"');
        if (q1) {
            const char *q2 = strchr(q1 + 1, '\"');
            if (q2 && (q2 - q1 - 1) < (int)sizeof(cmd->mode_request)) {
                size_t len = q2 - q1 - 1;
                strncpy(cmd->mode_request, q1 + 1, len);
                cmd->mode_request[len] = '\0';
            }
        }
    }

    /* クライアント認識モード */
    p = strstr(json, "\"client_mode\":");
    if (p) {
        const char *q1 = strchr(p + 13, '\"');
        if (q1) {
            const char *q2 = strchr(q1 + 1, '\"');
            if (q2 && (q2 - q1 - 1) < (int)sizeof(cmd->client_mode)) {
                size_t len = q2 - q1 - 1;
                strncpy(cmd->client_mode, q1 + 1, len);
                cmd->client_mode[len] = '\0';
            }
        }
    }
}

/* 静的ファイルの配信 */
static void serve_static_file(int client_fd, const char *static_dir, const char *rel_path) {
    char file_path[512];
    char resp_header[512];
    char file_buf[4096];

    /* パストラバーサル防止 */
    if (strstr(rel_path, "..")) {
        const char *err403 = "HTTP/1.1 403 Forbidden\r\nContent-Length: 9\r\nConnection: close\r\n\r\nForbidden";
        send(client_fd, err403, strlen(err403), 0);
        return;
    }

    /* ルートアクセスは index.html に変換 */
    const char *target = (strcmp(rel_path, "/") == 0 || strlen(rel_path) == 0) ? "/index.html" : rel_path;
    if (target[0] == '/') target++;

    snprintf(file_path, sizeof(file_path), "%s/%s", static_dir, target);

    FILE *fp = fopen(file_path, "rb");
    if (!fp) {
        const char *err404 = "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\nContent-Length: 13\r\nConnection: close\r\n\r\n404 Not Found";
        send(client_fd, err404, strlen(err404), 0);
        return;
    }

    /* ファイルサイズ取得 */
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    const char *mime = get_mime_type(file_path);
    int hlen = snprintf(resp_header, sizeof(resp_header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n\r\n",
        mime, file_size
    );
    send(client_fd, resp_header, hlen, 0);

    /* ファイル内容を送信 */
    size_t n;
    while ((n = fread(file_buf, 1, sizeof(file_buf), fp)) > 0) {
        send(client_fd, file_buf, n, 0);
    }

    fclose(fp);
}

void http_server_init(HttpServer *server, VehicleController *ctrl, int port, const char *static_dir) {
    server->port = port;
    strncpy(server->static_dir, static_dir, sizeof(server->static_dir) - 1);
    server->controller = ctrl;
    server->running = 0;
}

void http_server_stop(HttpServer *server) {
    server->running = 0;
}

void http_server_run(HttpServer *server) {
    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    char buf[RECV_BUF_SIZE];

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket failed");
        return;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server->port);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        closesocket(server_fd);
        return;
    }

    if (listen(server_fd, 10) < 0) {
        perror("listen failed");
        closesocket(server_fd);
        return;
    }

    server->running = 1;
    printf("[HTTP Server] Listening on http://0.0.0.0:%d\n", server->port);
    printf("[HTTP Server] Static directory: %s\n", server->static_dir);

    while (server->running) {
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            if (!server->running) break;
            continue;
        }

        int total_received = 0;
        int content_length = 0;
        char *body_ptr = NULL;

        /* リクエスト読み込み */
        while (total_received < (int)sizeof(buf) - 1) {
            int r = recv(client_fd, buf + total_received, sizeof(buf) - 1 - total_received, 0);
            if (r <= 0) break;
            total_received += r;
            buf[total_received] = '\0';

            /* ヘッダー終端 (\r\n\r\n) を探索 */
            char *hdr_end = strstr(buf, "\r\n\r\n");
            if (hdr_end) {
                body_ptr = hdr_end + 4;
                /* Content-Length の取得 */
                char *cl = strcasestr(buf, "Content-Length:");
                if (cl) {
                    content_length = atoi(cl + 15);
                }
                int current_body_len = total_received - (body_ptr - buf);
                if (current_body_len >= content_length) {
                    break; /* 全文受信完了 */
                }
            }
        }

        if (total_received > 0) {
            char method[16] = {0};
            char full_path[256] = {0};
            char version[16] = {0};

            sscanf(buf, "%15s %255s %15s", method, full_path, version);

            /* クエリ文字列の除去 (? 以降を切り捨て) */
            char *qmark = strchr(full_path, '?');
            if (qmark) *qmark = '\0';

            /* 1. CORS OPTIONS プリフライト対応 */
            if (strcmp(method, "OPTIONS") == 0) {
                const char *cors_res =
                    "HTTP/1.1 204 No Content\r\n"
                    "Access-Control-Allow-Origin: *\r\n"
                    "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                    "Access-Control-Allow-Headers: Content-Type\r\n"
                    "Connection: close\r\n\r\n";
                send(client_fd, cors_res, strlen(cors_res), 0);
            }
            /* 2. 制御 & テレメトリ API (POST /api/command, GET /api/telemetry) */
            else if (strcmp(full_path, "/api/command") == 0 || strcmp(full_path, "/api/telemetry") == 0) {
                if (strcmp(method, "POST") == 0 && body_ptr) {
                    VehicleCommand cmd;
                    parse_command_json(body_ptr, &cmd);
                    controller_process_command(server->controller, &cmd);
                }

                char json_body[512];
                controller_get_telemetry_json(server->controller, json_body, sizeof(json_body));

                char resp_hdr[256];
                int hlen = snprintf(resp_hdr, sizeof(resp_hdr),
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: application/json; charset=utf-8\r\n"
                    "Access-Control-Allow-Origin: *\r\n"
                    "Cache-Control: no-cache, no-store, must-revalidate\r\n"
                    "Content-Length: %zu\r\n"
                    "Connection: close\r\n\r\n",
                    strlen(json_body)
                );
                send(client_fd, resp_hdr, hlen, 0);
                send(client_fd, json_body, strlen(json_body), 0);
            }
            /* 3. カメラ映像配信 (/video_feed) - 未接続時は404を返却 */
            else if (strcmp(full_path, "/video_feed") == 0) {
                const char *no_cam =
                    "HTTP/1.1 404 Not Found\r\n"
                    "Content-Type: text/plain; charset=utf-8\r\n"
                    "Content-Length: 21\r\n"
                    "Connection: close\r\n\r\n"
                    "Camera not configured";
                send(client_fd, no_cam, strlen(no_cam), 0);
            }
            /* 4. 静的Webファイル配信 (index.html, JS, CSS) */
            else if (strcmp(method, "GET") == 0) {
                serve_static_file(client_fd, server->static_dir, full_path);
            }
            /* その他 */
            else {
                const char *err405 = "HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                send(client_fd, err405, strlen(err405), 0);
            }
        }

        /* コネクション切断 (KISS原則: ステートレスな一問一答でリソース即時解放) */
        closesocket(client_fd);
    }

    closesocket(server_fd);
}
