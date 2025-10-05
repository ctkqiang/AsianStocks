#include <microhttpd.h>
#include <json-c/json.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "../includes/endpoints.h"

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
    if (!connection || !url || !method) return MHD_NO;

    if (strcmp(method, "GET") != 0) {
        struct json_object *err = json_object_new_object();
        json_object_object_add(err, "error", json_object_new_string("method not allowed"));
  
        enum MHD_Result ret = send_json(connection, MHD_HTTP_METHOD_NOT_ALLOWED, err);
        json_object_put(err);
  
        return ret;
    }

    struct json_object *res = json_object_new_object();

    if (strcmp(url, INDEX_ENDPOINT) == 0) {
        json_object_object_add(res, "msg", json_object_new_string("hello world"));
        enum MHD_Result ret = send_json(connection, MHD_HTTP_OK, res);
        json_object_put(res);
        return ret;
    }

    if (strcmp(url, BURSA_ENDPOINT) == 0) {
        json_object_object_add(res, "msg", json_object_new_string("ok"));
        enum MHD_Result ret = send_json(connection, MHD_HTTP_OK, res);
        json_object_put(res);
        return ret;
    }

    json_object_object_add(res, "error", json_object_new_string("not found"));
    enum MHD_Result ret = send_json(connection, MHD_HTTP_NOT_FOUND, res);
    json_object_put(res);
    return ret;
}

void start_api() {
    struct MHD_Daemon *daemon = MHD_start_daemon(
        MHD_USE_INTERNAL_POLLING_THREAD,
        API_PORT,
        NULL,
        NULL,
        &handle_request,
        NULL,
        MHD_OPTION_END
    );

    if (!daemon) {
        fprintf(stderr, "FATAL: HTTP server failed to start\n");
        exit(EXIT_FAILURE);
    }
    
    printf("JSON API running safely on http://localhost:%d\n", API_PORT);
    
    getchar();
    MHD_stop_daemon(daemon);
}