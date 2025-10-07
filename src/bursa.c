#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <json-c/json.h>
#include <pthread.h>
#include <time.h>

#include "../includes/endpoints.h"
#include "../includes/bursa.h"
#include "../includes/memory.h"
#include "../includes/announcement_entry.h"

// 互斥锁，用于保护共享资源
static pthread_mutex_t curl_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t json_mutex = PTHREAD_MUTEX_INITIALIZER;

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
    if (ptr == NULL) {
        fprintf(stderr, "[错误] 内存分配失败，请求大小: %zu 字节\n", mem->size + realSize + 1);
        return 0;  // 内存分配失败
    }

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realSize);
    mem->size += realSize;
    mem->memory[mem->size] = 0;

    return realSize;
}

/**
 * 构建API请求URL
 * @param page 页码
 * @return 返回构建好的URL字符串，使用后需要手动释放
 */
static char *build_api_url(int page) {
    char *url = malloc(512);
    if (!url) {
        fprintf(stderr, "[错误] URL内存分配失败\n");
        return NULL;
    }

    time_t now = time(NULL);
    snprintf(url, 512, "%s?ann_type=company&per_page=%d&page=%d&_=%ld",
             BURSA_API_ANNOUNCEMENT_SEARCH,
             BURSA_API_PER_PAGE,
             page,
             now);

    return url;
}

/**
 * 从API获取单页数据
 * @param page 页码
 * @return 返回JSON对象，失败返回NULL
 */
static struct json_object *fetch_page(int page) {
    CURL *curl;
    CURLcode res;
    struct MemoryStruct chunk = {0};
    struct json_object *json = NULL;
    char *url = build_api_url(page);
    struct curl_slist *headers = NULL;

    if (!url) return NULL;

    pthread_mutex_lock(&curl_mutex);
    curl = curl_easy_init();

    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
        curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "gzip, deflate, br");
        curl_easy_setopt(curl, CURLOPT_COOKIE, "NPS_91b5b7fb_last_seen=1757148907498; saved_link=equities_prices,indices_prices,derivatives_prices,shariah_compliant_equities_prices,company_announcements; _ga=GA1.1.1910937320.1757148907; _cfuvid=ceY4XveCckGoncVaqbeFuJ2JxtWwyHMG6dCsv.kHAUU-1759763899018-0.0.1.1-604800000; _ga_3NMNNC625P=GS2.1.s1759763882$o6$g1$t1759763902$j40$l0$h0; cf_clearance=iWnbUjPTypEfLmSgvu8w6r6v6IkUo422G03Fhk.rBJQ-1759763903-1.2.1.1-qWQttthNL6uAe5PNthpH3CMOBjpc_y4CKKr.Xu7K936BS7vn.8V.eePp24a45mj9PMaDycQRUEf1GQfw_mi5EaMkFCVhnQ4Zkn6NtoNs51EVbfFtwvOhJhIRUj.76Atp9NJLiBQIxmD97.uqNsmktlkiG4cq9OX2wKMNrrzFkIvwwcwGuQfKY_w1Trd6iomXFQIWGmKhfk7XwJMcteJJzdCSapkEXkFDgIeLPc8xbMc; _locomotiveapp_session=SUJsL1l4UE01cjVPaG5qeEx3SG40QWd1SGZFQkxJMEY5a3ZKVmszV0Vuc2pqaWpnTDh0TDA0Q0p5R2RlNWdvQ2FzTW1wdlBla3lmZjlvOEtIM05sc1Y5OVR2NHIyMGh2YXI5R1J3cis5c2oyRlVKUU8wY2lDYkJpQ0dqVWJXL1RFbGorbXFIbVJZOVFvek92Y2ZrdTB3PT0tLU81aDJjTGNGVFYyTkl3OWEybDg0Snc9PQ%3D%3D--59dbbc50e80bf00bea962491b1ebaf42ecfc6802");

        headers = curl_slist_append(headers, "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.7");
        headers = curl_slist_append(headers, "Accept-Language: zh-CN,zh;q=0.9"); 
        headers = curl_slist_append(headers, "Cache-Control: max-age=0");
        headers = curl_slist_append(headers, "If-None-Match: W/\"422476fb3e90e8fe25a3387a08e32cb1-gzip\"");
        headers = curl_slist_append(headers, "Sec-Ch-Ua: \"Chromium\";v=\"123\", \"Not:A-Brand\";v=\"8\"");
        headers = curl_slist_append(headers, "Sec-Ch-Ua-Mobile: ?0");
        headers = curl_slist_append(headers, "Sec-Ch-Ua-Platform: \"macOS\"");
        headers = curl_slist_append(headers, "Sec-Fetch-Dest: document");
        headers = curl_slist_append(headers, "Sec-Fetch-Mode: navigate");
        headers = curl_slist_append(headers, "Sec-Fetch-Site: none");
        headers = curl_slist_append(headers, "Sec-Fetch-User: ?1");
        headers = curl_slist_append(headers, "Upgrade-Insecure-Requests: 1");
        headers = curl_slist_append(headers, "Referer: https://www.bursamalaysia.com/");
        headers = curl_slist_append(headers, "Origin: https://www.bursamalaysia.com");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &chunk);

        res = curl_easy_perform(curl);
        
        if (res != CURLE_OK) {
            fprintf(stderr, "[错误] 页面 %d 请求失败: %s\n", page, curl_easy_strerror(res));
        } else {
            fprintf(stdout, "[DEBUG] 页面 %d 原始数据: %s\n", page, chunk.memory);
            json = json_tokener_parse(chunk.memory);
        }

        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
    }

    pthread_mutex_unlock(&curl_mutex);
    
    free(url);
    if (chunk.memory) {
        free(chunk.memory);
    }

    return json;
}

/**
 * 从JSON数组中提取公告信息
 * @param data JSON数组
 * @return 返回处理后的公告条目数组
 */
static struct json_object *process_announcements(struct json_object *data) {
    struct json_object *array = json_object_new_array();
    int len = json_object_array_length(data);

    for (int i = 0; i < len; i++) {
        struct json_object *entry = json_object_array_get_idx(data, i);
        if (!json_object_is_type(entry, json_type_array)) continue;

        struct json_object *date_obj = json_object_array_get_idx(entry, 1);
        struct json_object *company_obj = json_object_array_get_idx(entry, 2);
        struct json_object *announcement_obj = json_object_array_get_idx(entry, 3);

        if (!date_obj || !company_obj || !announcement_obj) continue;

        const char *date_html = json_object_get_string(date_obj);
        const char *company_html = json_object_get_string(company_obj);
        const char *announcement_html = json_object_get_string(announcement_obj);

        // 提取日期（假设格式为：<div class='d-lg-inline-block d-none'>06 Oct 2025</div>）
        char date[32] = {0};
        sscanf(date_html, "<div class='d-lg-inline-block d-none'>%[^<]</div>", date);

        // 提取公司链接和名称
        char company_link[512] = {0};
        char company_name[256] = {0};
        sscanf(company_html, "<a href='%[^']'>%[^<]</a>", company_link, company_name);

        // 提取公告链接和标题
        char announcement_link[512] = {0};
        char announcement_title[512] = {0};
        sscanf(announcement_html, "<a href='%[^']'>%[^<]</a>", announcement_link, announcement_title);

        // 构建完整URL
        char full_company_link[1024] = {0};
        char full_announcement_link[1024] = {0};
        snprintf(full_company_link, sizeof(full_company_link), "https://www.bursamalaysia.com%s", company_link);
        snprintf(full_announcement_link, sizeof(full_announcement_link), "https://www.bursamalaysia.com%s", announcement_link);

        AnnouncementEntry ann_entry = {
            .announcement_date = date,
            .company = company_name,
            .download_link = full_announcement_link,
            .memo = announcement_title
        };

        struct json_object *json_entry = announcement_entry_to_json(&ann_entry);
        if (json_entry) {
            json_object_array_add(array, json_entry);
        }
    }

    return array;
}

/**
 * 获取所有公司公告
 * @return 返回包含所有公告的JSON数组
 */
struct json_object *grab_company_announcement() {
    struct json_object *result = json_object_new_array();
    int total_records = 0;
    int processed_records = 0;

    // 获取第一页以确定总记录数
    struct json_object *first_page = fetch_page(1);
    if (!first_page) {
        fprintf(stderr, "[错误] 无法获取第一页数据\n");
        return result;
    }

    struct json_object *records_total_obj;
    if (json_object_object_get_ex(first_page, "recordsTotal", &records_total_obj)) {
        total_records = json_object_get_int(records_total_obj);
        fprintf(stdout, "[信息] 总记录数: %d\n", total_records);
    }

    struct json_object *data_obj;
    if (json_object_object_get_ex(first_page, "data", &data_obj)) {
        struct json_object *processed = process_announcements(data_obj);
        int count = json_object_array_length(processed);
        processed_records += count;

        // 合并数组
        for (int i = 0; i < count; i++) {
            struct json_object *entry = json_object_array_get_idx(processed, i);
            json_object_array_add(result, json_object_get(entry));
        }
        json_object_put(processed);
    }

    json_object_put(first_page);

    // 获取剩余页面
    int max_pages = (total_records + BURSA_API_PER_PAGE - 1) / BURSA_API_PER_PAGE;
    if (max_pages > BURSA_API_MAX_PAGES) max_pages = BURSA_API_MAX_PAGES;

    for (int page = 2; page <= max_pages; page++) {
        struct json_object *json = fetch_page(page);
        if (!json) {
            fprintf(stderr, "[警告] 页面 %d 获取失败，跳过\n", page);
            continue;
        }

        struct json_object *data_obj;
        if (json_object_object_get_ex(json, "data", &data_obj)) {
            struct json_object *processed = process_announcements(data_obj);
            int count = json_object_array_length(processed);
            processed_records += count;

            // 合并数组
            for (int i = 0; i < count; i++) {
                struct json_object *entry = json_object_array_get_idx(processed, i);
                json_object_array_add(result, json_object_get(entry));
            }
            json_object_put(processed);
        }

        json_object_put(json);
        fprintf(stdout, "[信息] 已处理 %d/%d 条记录\n", processed_records, total_records);
    }

    fprintf(stdout, "[信息] 数据获取完成，共处理 %d 条记录\n", processed_records);
    return result;
}
