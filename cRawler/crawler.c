/*
 * GitHub Repository Crawler
 *
 * Finds Go repositories that use both gin-gonic/gin and samber/lo
 * by searching go.mod files via the GitHub Code Search API.
 *
 * Results are saved incrementally to an output file.
 *
 * Usage: ./crawler [GITHUB_TOKEN] [OUTPUT_FILE]
 *   GITHUB_TOKEN - optional, increases rate limit from 10 to 30 req/min
 *   OUTPUT_FILE  - defaults to "repos.txt"
 *
 * Alternatively set GITHUB_TOKEN environment variable.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <curl/curl.h>
#include "cjson.h"

#define DEFAULT_OUTPUT "repos.txt"
#define MAX_URL_LEN    2048
#define GITHUB_API     "https://api.github.com"
#define USER_AGENT     "C-GitHub-Crawler/1.0"

/* Per-page results (max 100 for code search) */
#define PER_PAGE 100

/* Rate limit: unauthenticated code search = 10 req/min, authenticated = 30 req/min */
#define DELAY_UNAUTH_SEC 7
#define DELAY_AUTH_SEC   3

typedef struct {
    char *data;
    size_t size;
} Buffer;

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t total = size * nmemb;
    Buffer *buf = (Buffer *)userp;
    char *tmp = realloc(buf->data, buf->size + total + 1);
    if (!tmp) return 0;
    buf->data = tmp;
    memcpy(buf->data + buf->size, contents, total);
    buf->size += total;
    buf->data[buf->size] = '\0';
    return total;
}

static int url_encode_query(const char *input, char *output, size_t outsize) {
    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    char *encoded = curl_easy_escape(curl, input, 0);
    if (!encoded) { curl_easy_cleanup(curl); return -1; }
    snprintf(output, outsize, "%s", encoded);
    curl_free(encoded);
    curl_easy_cleanup(curl);
    return 0;
}

/*
 * Perform a GitHub API GET request.
 * Returns the response body (caller must free buf->data) and HTTP status code.
 */
static long github_get(const char *url, const char *token, Buffer *buf) {
    CURL *curl = curl_easy_init();
    if (!curl) return -1;

    buf->data = NULL;
    buf->size = 0;

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Accept: application/vnd.github.v3+json");
    headers = curl_slist_append(headers, "X-GitHub-Api-Version: 2022-11-28");

    char auth_header[512];
    if (token && token[0]) {
        snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", token);
        headers = curl_slist_append(headers, auth_header);
    }

    char ua[] = USER_AGENT;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, ua);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, buf);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    } else {
        fprintf(stderr, "[ERROR] curl: %s\n", curl_easy_strerror(res));
        http_code = -1;
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return http_code;
}

/* Check if a URL is already in the output file (dedup) */
static int url_already_saved(const char *filepath, const char *url) {
    FILE *f = fopen(filepath, "r");
    if (!f) return 0;
    char line[MAX_URL_LEN];
    while (fgets(line, sizeof(line), f)) {
        /* Strip newline */
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';
        if (strcmp(line, url) == 0) {
            fclose(f);
            return 1;
        }
    }
    fclose(f);
    return 0;
}

/* Append a URL to the output file */
static int save_url(const char *filepath, const char *url) {
    if (url_already_saved(filepath, url)) return 0;
    FILE *f = fopen(filepath, "a");
    if (!f) {
        perror("fopen");
        return -1;
    }
    fprintf(f, "%s\n", url);
    fclose(f);
    return 1;
}

/*
 * Strategy:
 * 1. Use GitHub Code Search to find go.mod files containing "github.com/samber/lo"
 *    filtered to Go language.
 * 2. For each result, extract the repository full_name and construct the URL.
 * 3. Verify the repo also uses gin by checking go.mod content (second search or
 *    direct file fetch).
 *
 * We do two-phase approach:
 *   Phase 1: Search code for go.mod files with "samber/lo" in Go repos
 *   Phase 2: For each found repo, check if go.mod also contains "gin-gonic/gin"
 *
 * Alternative (faster but may miss some): single query with both terms.
 * GitHub code search supports multiple terms in one query.
 * Query: "samber/lo" "gin-gonic/gin" filename:go.mod language:Go
 */

static int crawl_repos(const char *token, const char *output_file) {
    int total_found = 0;
    int page = 1;
    int total_count = -1;
    int delay = (token && token[0]) ? DELAY_AUTH_SEC : DELAY_UNAUTH_SEC;

    printf("[INFO] Starting crawl. Output: %s\n", output_file);
    printf("[INFO] Authentication: %s\n", (token && token[0]) ? "YES (30 req/min)" : "NO (10 req/min)");
    printf("[INFO] Delay between requests: %d seconds\n", delay);
    printf("[INFO] Query: go.mod files containing both samber/lo AND gin-gonic/gin\n\n");

    /* URL-encode the search query */
    char query_encoded[1024];
    const char *raw_query = "\"samber/lo\" \"gin-gonic/gin\" filename:go.mod language:Go";
    if (url_encode_query(raw_query, query_encoded, sizeof(query_encoded)) != 0) {
        fprintf(stderr, "[ERROR] Failed to encode query\n");
        return -1;
    }

    while (1) {
        char url[MAX_URL_LEN];
        snprintf(url, sizeof(url),
                 "%s/search/code?q=%s&per_page=%d&page=%d",
                 GITHUB_API, query_encoded, PER_PAGE, page);

        printf("[INFO] Fetching page %d...\n", page);

        Buffer buf = {0};
        long status = github_get(url, token, &buf);

        if (status == 403 || status == 429) {
            fprintf(stderr, "[WARN] Rate limited (HTTP %ld). Waiting 60s...\n", status);
            if (buf.data) free(buf.data);
            sleep(60);
            continue;
        }

        if (status != 200) {
            fprintf(stderr, "[ERROR] HTTP %ld\n", status);
            if (buf.data) {
                fprintf(stderr, "[ERROR] Response: %.500s\n", buf.data);
                free(buf.data);
            }
            return -1;
        }

        /* Parse JSON response */
        cJSON *root = cJSON_Parse(buf.data);
        free(buf.data);

        if (!root) {
            fprintf(stderr, "[ERROR] Failed to parse JSON response\n");
            return -1;
        }

        /* Get total_count on first page */
        if (total_count < 0) {
            cJSON *tc = cJSON_GetObjectItem(root, "total_count");
            if (tc && tc->type == cJSON_Number) {
                total_count = cJSON_GetInt(tc);
            } else {
                total_count = 0;
            }
            printf("[INFO] Total results reported by GitHub: %d\n", total_count);
            if (total_count == 0) {
                printf("[INFO] No results found. Try with authentication for better results.\n");
                cJSON_Delete(root);
                break;
            }
        }

        /* Extract items */
        cJSON *items = cJSON_GetObjectItem(root, "items");
        int count = cJSON_GetArraySize(items);

        if (count == 0) {
            printf("[INFO] No more results on page %d.\n", page);
            cJSON_Delete(root);
            break;
        }

        for (int i = 0; i < count; i++) {
            cJSON *item = cJSON_GetArrayItem(items, i);
            if (!item) continue;

            cJSON *repo = cJSON_GetObjectItem(item, "repository");
            if (!repo) continue;

            cJSON *full_name = cJSON_GetObjectItem(repo, "full_name");
            if (!full_name || full_name->type != cJSON_String) continue;

            char repo_url[MAX_URL_LEN];
            snprintf(repo_url, sizeof(repo_url), "https://github.com/%s", full_name->valuestring);

            int saved = save_url(output_file, repo_url);
            if (saved > 0) {
                total_found++;
                printf("[FOUND #%d] %s\n", total_found, repo_url);
            } else if (saved == 0) {
                /* Already saved (dedup) */
            }
        }

        cJSON_Delete(root);

        /* Check if we've fetched all available results */
        /* GitHub limits code search to first 1000 results */
        int fetched_so_far = page * PER_PAGE;
        if (fetched_so_far >= total_count || fetched_so_far >= 1000) {
            printf("[INFO] Reached end of results.\n");
            break;
        }

        page++;
        printf("[INFO] Sleeping %ds for rate limit...\n", delay);
        sleep(delay);
    }

    printf("\n[DONE] Found %d unique repositories. Saved to: %s\n", total_found, output_file);
    return total_found;
}

int main(int argc, char *argv[]) {
    const char *token = NULL;
    const char *output_file = DEFAULT_OUTPUT;

    /* Parse arguments */
    if (argc > 1) token = argv[1];
    if (argc > 2) output_file = argv[2];

    /* Fallback to environment variable */
    if (!token || !token[0]) {
        token = getenv("GITHUB_TOKEN");
    }

    if (!token || !token[0]) {
        fprintf(stderr, "[WARN] No GitHub token provided. Rate limit: 10 requests/minute.\n");
        fprintf(stderr, "[WARN] Set GITHUB_TOKEN env var or pass as first argument for 30 req/min.\n\n");
    }

    curl_global_init(CURL_GLOBAL_DEFAULT);

    int result = crawl_repos(token, output_file);

    curl_global_cleanup();
    return (result >= 0) ? 0 : 1;
}
