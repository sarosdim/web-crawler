/*
  Minimal JSON parser for the crawler - handles objects, arrays, strings, numbers.
  Subset of cJSON API sufficient for GitHub API responses.
*/
#ifndef CJSON_H
#define CJSON_H

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

typedef struct cJSON {
    struct cJSON *next, *prev;
    struct cJSON *child;
    int type;
    char *valuestring;
    char *string; /* key name */
} cJSON;

#define cJSON_False  0
#define cJSON_True   1
#define cJSON_NULL   2
#define cJSON_Number 3
#define cJSON_String 4
#define cJSON_Array  5
#define cJSON_Object 6

static cJSON *cJSON_New(void) {
    cJSON *node = (cJSON *)calloc(1, sizeof(cJSON));
    return node;
}

static void cJSON_Delete(cJSON *item) {
    if (!item) return;
    cJSON *child = item->child;
    while (child) {
        cJSON *next = child->next;
        cJSON_Delete(child);
        child = next;
    }
    free(item->valuestring);
    free(item->string);
    free(item);
}

static const char *skip_ws(const char *p) {
    while (p && *p && isspace((unsigned char)*p)) p++;
    return p;
}

static const char *parse_string_raw(const char *p, char **out) {
    if (*p != '"') return NULL;
    p++;
    const char *start = p;
    size_t len = 0;
    /* first pass: count length */
    const char *s = start;
    while (*s && *s != '"') {
        if (*s == '\\') { s++; len++; }
        else { len++; }
        s++;
    }
    *out = (char *)malloc(len + 1);
    char *d = *out;
    s = start;
    while (*s && *s != '"') {
        if (*s == '\\') {
            s++;
            switch (*s) {
                case '"': *d++ = '"'; break;
                case '\\': *d++ = '\\'; break;
                case '/': *d++ = '/'; break;
                case 'n': *d++ = '\n'; break;
                case 't': *d++ = '\t'; break;
                case 'r': *d++ = '\r'; break;
                default: *d++ = *s; break;
            }
        } else {
            *d++ = *s;
        }
        s++;
    }
    *d = '\0';
    if (*s == '"') s++;
    return s;
}

static const char *parse_value(const char *p, cJSON *item);

static const char *parse_object(const char *p, cJSON *item) {
    item->type = cJSON_Object;
    p++; /* skip '{' */
    p = skip_ws(p);
    if (*p == '}') return p + 1;

    cJSON *prev = NULL;
    while (p && *p) {
        p = skip_ws(p);
        cJSON *child = cJSON_New();
        if (!child) return NULL;

        p = parse_string_raw(p, &child->string);
        if (!p) { cJSON_Delete(child); return NULL; }
        p = skip_ws(p);
        if (*p != ':') { cJSON_Delete(child); return NULL; }
        p = skip_ws(p + 1);
        p = parse_value(p, child);
        if (!p) { cJSON_Delete(child); return NULL; }

        if (!item->child) item->child = child;
        if (prev) { prev->next = child; child->prev = prev; }
        prev = child;

        p = skip_ws(p);
        if (*p == ',') { p++; continue; }
        if (*p == '}') { p++; break; }
        return NULL;
    }
    return p;
}

static const char *parse_array(const char *p, cJSON *item) {
    item->type = cJSON_Array;
    p++; /* skip '[' */
    p = skip_ws(p);
    if (*p == ']') return p + 1;

    cJSON *prev = NULL;
    while (p && *p) {
        p = skip_ws(p);
        cJSON *child = cJSON_New();
        if (!child) return NULL;

        p = parse_value(p, child);
        if (!p) { cJSON_Delete(child); return NULL; }

        if (!item->child) item->child = child;
        if (prev) { prev->next = child; child->prev = prev; }
        prev = child;

        p = skip_ws(p);
        if (*p == ',') { p++; continue; }
        if (*p == ']') { p++; break; }
        return NULL;
    }
    return p;
}

static const char *parse_number(const char *p, cJSON *item) {
    item->type = cJSON_Number;
    const char *start = p;
    if (*p == '-') p++;
    while (isdigit((unsigned char)*p)) p++;
    if (*p == '.') { p++; while (isdigit((unsigned char)*p)) p++; }
    if (*p == 'e' || *p == 'E') { p++; if (*p == '+' || *p == '-') p++; while (isdigit((unsigned char)*p)) p++; }
    size_t len = (size_t)(p - start);
    item->valuestring = (char *)malloc(len + 1);
    memcpy(item->valuestring, start, len);
    item->valuestring[len] = '\0';
    return p;
}

static const char *parse_value(const char *p, cJSON *item) {
    p = skip_ws(p);
    if (!p || !*p) return NULL;

    if (*p == '"') {
        item->type = cJSON_String;
        p = parse_string_raw(p, &item->valuestring);
        return p;
    }
    if (*p == '{') return parse_object(p, item);
    if (*p == '[') return parse_array(p, item);
    if (*p == '-' || isdigit((unsigned char)*p)) return parse_number(p, item);
    if (strncmp(p, "true", 4) == 0) { item->type = cJSON_True; return p + 4; }
    if (strncmp(p, "false", 5) == 0) { item->type = cJSON_False; return p + 5; }
    if (strncmp(p, "null", 4) == 0) { item->type = cJSON_NULL; return p + 4; }
    return NULL;
}

static cJSON *cJSON_Parse(const char *value) {
    if (!value) return NULL;
    cJSON *root = cJSON_New();
    if (!root) return NULL;
    const char *end = parse_value(value, root);
    if (!end) { cJSON_Delete(root); return NULL; }
    return root;
}

static cJSON *cJSON_GetObjectItem(const cJSON *obj, const char *key) {
    if (!obj || obj->type != cJSON_Object) return NULL;
    cJSON *child = obj->child;
    while (child) {
        if (child->string && strcmp(child->string, key) == 0) return child;
        child = child->next;
    }
    return NULL;
}

static int cJSON_GetArraySize(const cJSON *arr) {
    if (!arr || arr->type != cJSON_Array) return 0;
    int count = 0;
    cJSON *child = arr->child;
    while (child) { count++; child = child->next; }
    return count;
}

static cJSON *cJSON_GetArrayItem(const cJSON *arr, int index) {
    if (!arr || arr->type != cJSON_Array) return NULL;
    cJSON *child = arr->child;
    for (int i = 0; i < index && child; i++) child = child->next;
    return child;
}

static int cJSON_GetInt(const cJSON *item) {
    if (!item || !item->valuestring) return 0;
    return atoi(item->valuestring);
}

#endif /* CJSON_H */
