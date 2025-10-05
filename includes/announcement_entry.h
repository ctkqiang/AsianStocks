#pragma once

#ifndef ANNOUNCEMENT_ENTRY_H
#define ANNOUNCEMENT_ENTRY_H

#include <json-c/json.h>

typedef struct {
    const char *announcement_date;
    const char *company;
    const char *download_link;
    const char *memo;
} AnnouncementEntry;

static inline struct json_object *announcement_entry_to_json(const AnnouncementEntry *entry) {
    if (!entry) return NULL;
 
    struct json_object *obj = json_object_new_object();

    json_object_object_add(obj, "announcement_date", json_object_new_string(entry->announcement_date));
    json_object_object_add(obj, "company", json_object_new_string(entry->company));
    json_object_object_add(obj, "download_link", json_object_new_string(entry->download_link));
    json_object_object_add(obj, "memo", json_object_new_string(entry->memo));
 
    return obj;
}

#endif