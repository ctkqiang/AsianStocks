#include <microhttpd.h>
#include <json-c/json.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#include "../includes/routes.h"
#include "../includes/bursa.h"

#define LOG_FILE "build/server.log"
#define MAX_PORT_RETRIES 10
#define PORT_INCREMENT 1

static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

static void log_event(const char *level, const char *method, const char *url, int status) {
    pthread_mutex_lock(&log_mutex);

    FILE *logf = fopen(LOG_FILE, "a");
    if (!logf) {
        perror("日志文件打开失败");
        pthread_mutex_unlock(&log_mutex);
        return;
    }

    time_t now = time(NULL);
    struct tm *t = localtime(&now);

    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", t);

    fprintf(logf, "[%s] [%s] %s %s -> %d\n", timestamp, level, method, url, status);
    fprintf(stdout, "[%s] [%s] %s %s -> %d\n", timestamp, level, method, url, status);

    fclose(logf);
    pthread_mutex_unlock(&log_mutex);
}

static int check_port_available(uint16_t port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        fprintf(stderr, "错误: 无法创建套接字: %s\n", strerror(errno));
        return 0;
    }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    // 设置套接字选项以允许重用地址
    int opt = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        fprintf(stderr, "警告: 设置套接字选项失败: %s\n", strerror(errno));
    }

    int result = bind(sock, (struct sockaddr*)&addr, sizeof(addr));
    close(sock);

    return result == 0;
}

static enum MHD_Result send_json(
    struct MHD_Connection *connection,
    unsigned int status_code,
    struct json_object *obj
) {
    if (!connection || !obj) return MHD_NO;

    const char *json_str = json_object_to_json_string_ext(obj, JSON_C_TO_STRING_PLAIN);
    if (!json_str) return MHD_NO;

    struct MHD_Response *response = MHD_create_response_from_buffer(
        strlen(json_str),
        (void *)json_str,
        MHD_RESPMEM_MUST_COPY
    );

    if (!response) return MHD_NO;

    MHD_add_response_header(response, "Content-Type", "application/json");
    MHD_add_response_header(response, "Cache-Control", "no-store");

    enum MHD_Result ret = MHD_queue_response(connection, status_code, response);
    MHD_destroy_response(response);
    return ret;
}

static enum MHD_Result handle_request(
    void *cls,
    struct MHD_Connection *connection,
    const char *url,
    const char *method,
    const char *version,
    const char *upload_data,
    size_t *upload_data_size,
    void **con_cls
) {
    (void)cls;
    (void)version;
    (void)upload_data;
    (void)upload_data_size;
    (void)con_cls;

    if (!connection || !url || !method) return MHD_NO;

    struct json_object *res = json_object_new_object();

    if (strcmp(method, "GET") != 0) {
        json_object_object_add(res, "error", json_object_new_string("method not allowed"));
        enum MHD_Result ret = send_json(connection, MHD_HTTP_METHOD_NOT_ALLOWED, res);
        log_event("WARN", method, url, 405);
        json_object_put(res);
        return ret;
    }

    if (strcmp(url, INDEX) == 0) {
        json_object_object_add(res, "msg", json_object_new_string("hello world"));
        enum MHD_Result ret = send_json(connection, MHD_HTTP_OK, res);
        log_event("INFO", method, url, 200);
        json_object_put(res);
        return ret;
    }

   if (strcmp(url, BURSA) == 0) {
        struct json_object *announcements = grab_company_announcement();

        if (!announcements) {
            json_object_object_add(res, "msg", json_object_new_string("failed"));
            json_object_object_add(res, "error", json_object_new_string("unable to fetch announcements"));
            
            enum MHD_Result ret = send_json(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, res);
            
            log_event("ERROR", method, url, 500);
            json_object_put(res);

            return ret;
        }

        json_object_object_add(res, "msg", json_object_new_string("ok"));
        json_object_object_add(res, "data", announcements);

        enum MHD_Result ret = send_json(connection, MHD_HTTP_OK, res);
        log_event("INFO", method, url, 200);

        json_object_put(res);

        return ret;
    }

    json_object_object_add(res, "error", json_object_new_string("not found"));
    enum MHD_Result ret = send_json(connection, MHD_HTTP_NOT_FOUND, res);
    log_event("ERROR", method, url, 404);
    json_object_put(res);
    return ret;
}

void start_api() {
    uint16_t port = API_PORT;
    struct MHD_Daemon *daemon = NULL;
    int retries = 0;

    // 尝试在不同端口启动服务器
    while (retries < MAX_PORT_RETRIES) {
        if (!check_port_available(port)) {
            fprintf(stderr, "警告: 端口 %d 已被占用，尝试下一个端口...\n", port);
            port += PORT_INCREMENT;
            retries++;
            continue;
        }

        daemon = MHD_start_daemon(
            MHD_USE_INTERNAL_POLLING_THREAD | MHD_USE_ERROR_LOG,
            port,
            NULL,
            NULL,
            &handle_request,
            NULL,
            MHD_OPTION_CONNECTION_TIMEOUT, (unsigned int) 120,
            MHD_OPTION_END
        );

        if (daemon) break;

        fprintf(stderr, "警告: 在端口 %d 启动服务器失败，尝试下一个端口...\n", port);
        port += PORT_INCREMENT;
        retries++;
    }

    if (!daemon) {
        fprintf(stderr, "严重错误: 在尝试 %d 个端口后服务器仍无法启动\n", MAX_PORT_RETRIES);
        fprintf(stderr, "请检查系统资源和权限设置\n");
        exit(EXIT_FAILURE);
    }

    printf("JSON API 服务器成功启动在 http://localhost:%d\n", port);
    log_event("INFO", "SERVER", "START", 200);

    printf("按回车键停止服务器...\n");
    getchar();
    
    MHD_stop_daemon(daemon);
    log_event("INFO", "SERVER", "STOP", 0);
    printf("服务器已停止\n");
}
