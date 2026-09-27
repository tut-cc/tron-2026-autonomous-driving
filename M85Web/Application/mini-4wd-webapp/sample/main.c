#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>

#include "constants.h"
#include "controller.h"
#include "http_server.h"

static VehicleController g_controller;
static HttpServer        g_server;
static volatile int      g_app_running = 1;

/* シグナルハンドラ (Ctrl+C で安全終了) */
static void handle_sigint(int sig) {
    (void)sig;
    g_app_running = 0;
    http_server_stop(&g_server);
}

/* HTTPサーバースレッド */
static void* http_server_thread_func(void *arg) {
    HttpServer *srv = (HttpServer *)arg;
    http_server_run(srv);
    return NULL;
}

/* 制御周期タイマースレッド (100ms周期: TORカウントダウン・通信途絶・デッドマン監視) */
static void* tick_timer_thread_func(void *arg) {
    VehicleController *ctrl = (VehicleController *)arg;
    while (g_app_running) {
        usleep(HEARTBEAT_INTERVAL_MS * 1000);
        controller_tick(ctrl, HEARTBEAT_INTERVAL_MS);
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    int port = DEFAULT_PORT;
    const char *static_dir = ".."; /* デフォルト: mini-4wd-webapp ルート */

    if (argc > 1) {
        port = atoi(argv[1]);
    }
    if (argc > 2) {
        static_dir = argv[2];
    }

    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigint);

    /* 1. 制御コア初期化 */
    controller_init(&g_controller);

    /* 2. HTTPサーバー初期化 */
    http_server_init(&g_server, &g_controller, port, static_dir);

    /* 3. スレッド起動 (HTTPサーバー & 100msタイマー) */
    pthread_t th_http, th_timer;
    if (pthread_create(&th_http, NULL, http_server_thread_func, &g_server) != 0) {
        perror("pthread_create(http) failed");
        return 1;
    }
    if (pthread_create(&th_timer, NULL, tick_timer_thread_func, &g_controller) != 0) {
        perror("pthread_create(timer) failed");
        return 1;
    }

    printf("====================================================\n");
    printf("  Mini 4WD WebApp - C Sample Server (KISS)\n");
    printf("  Web UI : http://localhost:%d\n", port);
    printf("  API    : http://localhost:%d/api/telemetry\n", port);
    printf("====================================================\n");
    printf("[Terminal Keys] [1]Manual [2]Auto [3]TOR [4]AutoAbort [5]ManualAbort [q]Quit\n\n");

    /* 4. メインループ: 端末キーボード入力受付 (シナリオ切替) */
    if (isatty(STDIN_FILENO)) {
        char line[64];
        while (g_app_running && fgets(line, sizeof(line), stdin)) {
            char key = line[0];
            if (key >= '1' && key <= '5') {
                controller_apply_scenario(&g_controller, key - '0');
            } else if (key == 'q' || key == 'Q') {
                printf("Quitting...\n");
                break;
            }
        }
    } else {
        /* 非対話モード (バックグラウンド起動など) */
        while (g_app_running) {
            pause();
        }
    }

    g_app_running = 0;
    http_server_stop(&g_server);

    printf("Server stopped. Bye.\n");
    return 0;
}
