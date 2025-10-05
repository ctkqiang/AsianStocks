#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <regex.h>
#include <json-c/json.h>
#include <unistd.h>
#include <time.h>

#include "../includes/endpoints.h"
#include "../includes/bursa.h"
#include "../includes/memory.h"
#include "../includes/announcement_entry.h"

#define MAX_RETRIES 3
#define RETRY_DELAY 5  // ~ 试延迟到5秒
#define INITIAL_BUFFER_SIZE 32768  // ?大概32KB
#define MAX_BUFFER_SIZE (10 * 1024 * 1024)  // ?大概10MB

#define CONNECT_TIMEOUT 10L
#define TRANSFER_TIMEOUT 30L
#define LOW_SPEED_TIME 5L
#define LOW_SPEED_LIMIT 100L

static void log_with_time(const char *level, const char *fmt, ...) {
    time_t now;
    struct tm *tm_info;
    char time_str[26];
    va_list args;

    time(&now);
    tm_info = localtime(&now);
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);

    fprintf(stderr, "[%s] [%s] ", time_str, level);
    
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    
    fprintf(stderr, "\n");
}

#define LOG_INFO(fmt, ...) log_with_time("INFO", fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) log_with_time("ERROR", fmt, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) log_with_time("DEBUG", fmt, ##__VA_ARGS__)

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realSize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;

    // 检查是否超过最大缓冲区大小
    if (mem->size + realSize > MAX_BUFFER_SIZE) {
        LOG_ERROR("接收数据超过最大限制 (%d bytes)", MAX_BUFFER_SIZE);
        return 0;
    }

    char *ptr = realloc(mem->memory, mem->size + realSize + 1);
    if (!ptr) {
        LOG_ERROR("内存分配失败 (请求大小: %zu bytes)", mem->size + realSize + 1);
        return 0;
    }

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realSize);
    mem->size += realSize;
    mem->memory[mem->size] = 0;

    LOG_DEBUG("成功接收数据: %zu bytes", realSize);
    return realSize;
}

static char *fetch_html() {
    CURL *curl = NULL;
    CURLcode res;
    struct MemoryStruct chunk = {0};
    int retry_count = 0;
    char *result = NULL;
    struct curl_slist *headers = NULL;
    long http_code = 0;
    char curl_errbuf[CURL_ERROR_SIZE] = {0};

    chunk.memory = malloc(INITIAL_BUFFER_SIZE);
    if (!chunk.memory) {
        LOG_ERROR("初始内存分配失败");
        return NULL;
    }
    chunk.size = 0;

    while (retry_count < MAX_RETRIES) {
        if (retry_count > 0) {
            LOG_INFO("第 %d 次重试中...", retry_count + 1);
            sleep(RETRY_DELAY * retry_count);  // 递增延迟时间
        }

        curl = curl_easy_init();
        if (!curl) {
            LOG_ERROR("CURL初始化失败");
            continue;
        }

        // 设置详细的错误信息
        curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_errbuf);
        
        // 基本设置
        curl_easy_setopt(curl, CURLOPT_URL, BURSA_COMPANY_ANNOUNCEMENT);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
        
        // 超时设置
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, CONNECT_TIMEOUT);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, TRANSFER_TIMEOUT);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, LOW_SPEED_LIMIT);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, LOW_SPEED_TIME);

        // 网络相关设置
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 120L);
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 60L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
        
        // SSL设置
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

        // 请求头设置
        headers = curl_slist_append(headers, "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8");
        headers = curl_slist_append(headers, "Accept-Language: zh-CN,zh;q=0.9,en;q=0.8");
        headers = curl_slist_append(headers, "Accept-Encoding: gzip, deflate, br");
        headers = curl_slist_append(headers, "Cache-Control: no-cache");
        headers = curl_slist_append(headers, "Pragma: no-cache");
        headers = curl_slist_append(headers, "Connection: keep-alive");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        // User-Agent设置
        curl_easy_setopt(curl, CURLOPT_USERAGENT, 
            "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) "
            "AppleWebKit/537.36 (KHTML, like Gecko) "
            "Chrome/91.0.4472.124 Safari/537.36");

        LOG_INFO("开始请求: %s", BURSA_COMPANY_ANNOUNCEMENT);
        res = curl_easy_perform(curl);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

        if (res == CURLE_OK && http_code == 200) {
            LOG_INFO("请求成功 (HTTP %ld)", http_code);
            result = chunk.memory;
            break;
        }

        const char *error_msg = curl_errbuf[0] ? curl_errbuf : curl_easy_strerror(res);
        LOG_ERROR("请求失败 (HTTP %ld): %s", http_code, error_msg);

        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
        headers = NULL;
        
        if (chunk.memory) {
            free(chunk.memory);
            chunk.memory = malloc(INITIAL_BUFFER_SIZE);
            if (!chunk.memory) {
                LOG_ERROR("重试时内存分配失败");
                return NULL;
            }
        }
        chunk.size = 0;

        retry_count++;
    }

    if (curl) curl_easy_cleanup(curl);
    if (headers) curl_slist_free_all(headers);

    if (!result) {
        LOG_ERROR("在%d次尝试后获取数据失败", MAX_RETRIES);
        if (chunk.memory) free(chunk.memory);
        return NULL;
    }

    return result;
}

/**
 * grab_company_announcement - 抓取公司公告信息并以 JSON 数组形式返回
 *
 * 此函数通过抓取网页 HTML 内容，并使用正则表达式解析其中的公司公告条目，
 * 每个条目包括公告日期、公司名称、下载链接及公告标题。最终将这些数据封装成
 * JSON 对象数组返回。
 *
 * 返回值:
 *   成功时返回一个包含公告信息的 JSON 数组对象；
 *   若无法获取 HTML 或正则编译失败，则返回 NULL 或空数组。
 */
struct json_object *grab_company_announcement() {
    char *html = fetch_html();
    if (!html) {
        fprintf(stderr, "[error] 无法获取HTML内容\n");
        return NULL;
    }

    // 使用更精确和健壮的正则表达式模式
    const char *pattern =
        "<tr[^>]*>"                                           // 开始标签
        "\\s*<td[^>]*>[^<]*</td>"                           // 第一列（忽略）
        "\\s*<td[^>]*>"                                     // 第二列开始
        ".*?<div class=\"d-lg-inline-block d-none\">"      // 日期div开始
        "([^<]+)"                                          // 捕获组1：日期
        "</div>.*?</td>"                                   // 日期div结束
        "\\s*<td[^>]*>"                                    // 第三列开始
        "<a href=\"([^\"]+)\"[^>]*>"                      // 捕获组2：公司链接
        "([^<]+)"                                         // 捕获组3：公司名称
        "</a></td>"                                       // 第三列结束
        "\\s*<td[^>]*>"                                   // 第四列开始
        "<a href=\"([^\"]+)\"[^>]*>"                     // 捕获组4：公告链接
        "([^<]+)"                                        // 捕获组5：公告标题
        "</a></td>";

    regex_t regex;
    int reg_result = regcomp(&regex, pattern, REG_EXTENDED | REG_NEWLINE);
    if (reg_result != 0) {
        char error_buf[256];
        regerror(reg_result, &regex, error_buf, sizeof(error_buf));
        fprintf(stderr, "[error] 正则表达式编译失败: %s\n", error_buf);
        free(html);
        return NULL;
    }

    struct json_object *array = json_object_new_array();
    if (!array) {
        fprintf(stderr, "[error] 创建JSON数组失败\n");
        regfree(&regex);
        free(html);
        return NULL;
    }

    char *cursor = html;
    regmatch_t matches[6];
    int count = 0;
    int max_entries = 50;

    while (count < max_entries && regexec(&regex, cursor, 6, matches, 0) == 0) {
        // 检查所有捕获组是否有效
        int valid_matches = 1;
        for (int i = 1; i < 6; i++) {
            if (matches[i].rm_so == -1 || matches[i].rm_eo == -1) {
                valid_matches = 0;
                fprintf(stderr, "[warn] 跳过无效的匹配项（捕获组 %d 无效）\n", i);
                break;
            }
        }

        if (!valid_matches) {
            cursor += matches[0].rm_eo > 0 ? matches[0].rm_eo : 1;
            continue;
        }

        // 预分配所有缓冲区
        char date[128] = {0};
        char company_link[512] = {0};
        char company_name[256] = {0};
        char ann_link[512] = {0};
        char ann_title[512] = {0};

        // 使用安全的字符串操作
        if (matches[1].rm_eo - matches[1].rm_so < sizeof(date)) {
            snprintf(date, sizeof(date), "%.*s",
                (int)(matches[1].rm_eo - matches[1].rm_so),
                cursor + matches[1].rm_so
            );
        } else {
            fprintf(stderr, "[warn] 日期字段过长，已截断\n");
            snprintf(date, sizeof(date), "%.*s...",
                (int)(sizeof(date) - 4),
                cursor + matches[1].rm_so
            );
        }

        snprintf(company_link, sizeof(company_link),
            "https://www.bursamalaysia.com%.*s",
            (int)(matches[2].rm_eo - matches[2].rm_so),
            cursor + matches[2].rm_so
        );

        if (matches[3].rm_eo - matches[3].rm_so < sizeof(company_name)) {
            snprintf(company_name, sizeof(company_name), "%.*s",
                (int)(matches[3].rm_eo - matches[3].rm_so),
                cursor + matches[3].rm_so
            );
        } else {
            fprintf(stderr, "[warn] 公司名称过长，已截断\n");
            snprintf(company_name, sizeof(company_name), "%.*s...",
                (int)(sizeof(company_name) - 4),
                cursor + matches[3].rm_so
            );
        }

        snprintf(ann_link, sizeof(ann_link),
            "https://www.bursamalaysia.com%.*s",
            (int)(matches[4].rm_eo - matches[4].rm_so),
            cursor + matches[4].rm_so
        );

        if (matches[5].rm_eo - matches[5].rm_so < sizeof(ann_title)) {
            snprintf(ann_title, sizeof(ann_title), "%.*s",
                (int)(matches[5].rm_eo - matches[5].rm_so),
                cursor + matches[5].rm_so
            );
        } else {
            fprintf(stderr, "[warn] 公告标题过长，已截断\n");
            snprintf(ann_title, sizeof(ann_title), "%.*s...",
                (int)(sizeof(ann_title) - 4),
                cursor + matches[5].rm_so
            );
        }

        // 验证提取的数据
        if (strlen(date) == 0 || strlen(company_name) == 0 || 
            strlen(ann_link) == 0 || strlen(ann_title) == 0) {
            fprintf(stderr, "[warn] 跳过数据不完整的条目\n");
            cursor += matches[0].rm_eo;
            continue;
        }

        AnnouncementEntry entry = {
            .announcement_date = date,
            .company = company_name,
            .download_link = ann_link,
            .memo = ann_title
        };

        struct json_object *json_entry = announcement_entry_to_json(&entry);
        if (!json_entry) {
            fprintf(stderr, "[error] 创建JSON条目失败\n");
            cursor += matches[0].rm_eo;
            continue;
        }

        if (json_object_array_add(array, json_entry) < 0) {
            fprintf(stderr, "[error] 添加JSON条目到数组失败\n");
            json_object_put(json_entry);
            cursor += matches[0].rm_eo;
            continue;
        }

        fprintf(stderr, "[ok] %s | %s | %s\n", date, company_name, ann_title);
        cursor += matches[0].rm_eo;
        count++;
    }

    regfree(&regex);
    free(html);

    if (count == 0) {
        fprintf(stderr, "[warn] 未找到匹配的公告条目\n");
        json_object_put(array);
        return NULL;
    }

    fprintf(stderr, "[info] 成功解析 %d 条公告\n", count);
    return array;
}
