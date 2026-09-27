#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include "controller.h"

/* HTTPサーバー設定 */
typedef struct {
    int  port;
    char static_dir[256];
    VehicleController *controller;
    volatile int running;
} HttpServer;

/* 初期化 */
void http_server_init(HttpServer *server, VehicleController *ctrl, int port, const char *static_dir);

/* サーバーループ実行 (ブロッキング、別スレッドで実行推奨) */
void http_server_run(HttpServer *server);

/* サーバー停止 */
void http_server_stop(HttpServer *server);

#endif /* HTTP_SERVER_H */
