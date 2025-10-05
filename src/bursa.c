#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <regex.h>
#include <json-c/json.h>
#include <unistd.h>

#include "../includes/endpoints.h"
#include "../includes/bursa.h"
#include "../includes/memory.h"
#include "../includes/announcement_entry.h"

#define MAX_RETRIES 3
#define RETRY_DELAY 2

/**
 * 写入内存回调函数，用于将接收到的数据写入到内存缓冲区中
 * @param contents 指向接收到的数据内容的指针
 * @param size 每个数据元素的大小（字节数）
 * @param nmemb 数据元素的数量
 * @param userp 指向用户自定义数据结构的指针（这里是指向MemoryStruct结构体）
 * @return 返回实际写入的字节数，如果分配内存失败则返回0
 */
static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realSize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;

    char *ptr = realloc(mem->memory, mem->size + realSize + 1);
    
    /** 
     * 当内存不足时，返回0
     */
    if (ptr == NULL) {
        fprintf(stderr, "[error] 内存分配失败\n");
        return 0;
    }

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realSize);
    
    mem->size += realSize;
    mem->memory[mem->size] = 0;

    return realSize;
}

/**
 * 获取网页内容，支持重试机制
 * @return 成功返回HTML内容，失败返回NULL
 */
static char *fetch_html() {
    CURL *curl = NULL;
    CURLcode res;
    struct MemoryStruct chunk = {0};
    int retry_count = 0;
    char *result = NULL;
    struct curl_slist *headers = NULL;

    while (retry_count < MAX_RETRIES) {
        curl = curl_easy_init();
        if (!curl) {
            fprintf(stderr, "[error] curl初始化失败，重试次数：%d\n", retry_count + 1);
            sleep(RETRY_DELAY);
            retry_count++;
            continue;
        }

        // 添加必要的请求头
        headers = curl_slist_append(headers, "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8");
        headers = curl_slist_append(headers, "Accept-Language: zh-CN,zh;q=0.9,en;q=0.8");
        headers = curl_slist_append(headers, "Accept-Encoding: gzip, deflate, br");
        headers = curl_slist_append(headers, "Cache-Control: no-cache");
        headers = curl_slist_append(headers, "Pragma: no-cache");
        headers = curl_slist_append(headers, "Connection: keep-alive");

        curl_easy_setopt(curl, CURLOPT_URL, BURSA_COMPANY_ANNOUNCEMENT);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
        curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
        
        // 启用自动解压缩
        curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
        
        // 添加SSL选项
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

        res = curl_easy_perform(curl);

        if (res == CURLE_OK) {
            result = chunk.memory;
            break;
        }

        fprintf(stderr, "[error] curl请求失败: %s，重试次数：%d\n", 
                curl_easy_strerror(res), retry_count + 1);

        curl_easy_cleanup(curl);
        if (headers) {
            curl_slist_free_all(headers);
            headers = NULL;
        }
        free(chunk.memory);
        chunk.memory = NULL;
        chunk.size = 0;

        if (retry_count < MAX_RETRIES - 1) {
            sleep(RETRY_DELAY);
        }
        retry_count++;
    }

    if (curl) {
        curl_easy_cleanup(curl);
    }
    if (headers) {
        curl_slist_free_all(headers);
    }

    if (!result) {
        fprintf(stderr, "[error] 在%d次尝试后获取数据失败\n", MAX_RETRIES);
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
