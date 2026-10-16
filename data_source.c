#include "data_source.h"
#include "cJSON.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
static HANDLE worker;
#else
#include <curl/curl.h>
#include <pthread.h>
static pthread_t worker;
static pthread_mutex_t workerMutex = PTHREAD_MUTEX_INITIALIZER;
static bool workerDone;
#endif

#define MAX_RESPONSE_SIZE 65536

/* Coordinates of Carlow town, Ireland. Times returned in Europe/Dublin. */
static const char *weatherUrl =
    "https://api.open-meteo.com/v1/forecast?"
    "latitude=52.84083&longitude=-6.92611&"
    "current=temperature_2m,relative_humidity_2m&"
    "timezone=Europe%2FDublin&forecast_days=1";

static const char *airUrl =
    "https://air-quality-api.open-meteo.com/v1/air-quality?"
    "latitude=52.84083&longitude=-6.92611&current=pm2_5&"
    "timezone=Europe%2FDublin&forecast_days=1";

static bool workerActive;
static EnvironmentData workerResult;

#ifndef _WIN32
typedef struct {
    char *text;
    size_t length;
} ResponseBuffer;

static size_t ReceiveResponse(char *contents, size_t size,
                              size_t count, void *context)
{
    ResponseBuffer *buffer = context;
    if (size != 0 && count > MAX_RESPONSE_SIZE / size) return 0;
    size_t bytes = size * count;
    if (bytes > MAX_RESPONSE_SIZE - buffer->length) return 0;
    memcpy(buffer->text + buffer->length, contents, bytes);
    buffer->length += bytes;
    buffer->text[buffer->length] = '\0';
    return bytes;
}
#endif

static char *GetJson(const char *url, char *error, size_t errorSize)
{
    char *text = calloc(MAX_RESPONSE_SIZE + 1, 1);
    if (!text) {
        snprintf(error, errorSize, "Not enough memory");
        return NULL;
    }

#ifdef _WIN32
    /* WinHTTP is part of Windows; no separate curl installation is needed. */
    wchar_t wideUrl[512];
    wchar_t host[128];
    wchar_t path[512];
    URL_COMPONENTS parts = {0};
    HINTERNET session = NULL;
    HINTERNET connection = NULL;
    HINTERNET request = NULL;
    bool success = false;
    size_t length = 0;
    DWORD status = 0;
    DWORD statusSize = sizeof(status);

    parts.dwStructSize = sizeof(parts);
    parts.lpszHostName = host;
    parts.dwHostNameLength = (DWORD)(sizeof(host) / sizeof(host[0]));
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = (DWORD)(sizeof(path) / sizeof(path[0]));

    if (!MultiByteToWideChar(CP_UTF8, 0, url, -1, wideUrl, 512) ||
        !WinHttpCrackUrl(wideUrl, 0, 0, &parts)) goto cleanup;

    session = WinHttpOpen(L"CarlowDashboard/0.1",
                         WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                         WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) goto cleanup;
    WinHttpSetTimeouts(session, 4000, 4000, 6000, 6000);
    connection = WinHttpConnect(session, host, parts.nPort, 0);
    if (!connection) goto cleanup;
    request = WinHttpOpenRequest(connection, L"GET", path, NULL,
                                WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                WINHTTP_FLAG_SECURE);
    if (!request ||
        !WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request, NULL)) goto cleanup;

    if (!WinHttpQueryHeaders(request,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
            WINHTTP_NO_HEADER_INDEX)) goto cleanup;
    if (status != 200) {
        snprintf(error, errorSize, "HTTP error %lu", (unsigned long)status);
        goto cleanup;
    }

    while (length < MAX_RESPONSE_SIZE) {
        DWORD bytes = 0;
        if (!WinHttpReadData(request, text + length,
                (DWORD)(MAX_RESPONSE_SIZE - length), &bytes)) goto cleanup;
        if (bytes == 0) {
            success = true;
            break;
        }
        length += bytes;
    }

cleanup:
    if (!success && error[0] == '\0') {
        snprintf(error, errorSize, "Network request failed (Windows %lu)",
                 (unsigned long)GetLastError());
    }
    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    if (session) WinHttpCloseHandle(session);
    if (!success) {
        free(text);
        return NULL;
    }
#else
    CURL *request = curl_easy_init();
    if (!request) {
        snprintf(error, errorSize, "Cannot initialise HTTPS client");
        free(text);
        return NULL;
    }
    ResponseBuffer buffer = {text, 0};
    curl_easy_setopt(request, CURLOPT_URL, url);
    curl_easy_setopt(request, CURLOPT_USERAGENT, "CarlowDashboard/0.1");
    curl_easy_setopt(request, CURLOPT_CONNECTTIMEOUT, 4L);
    curl_easy_setopt(request, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(request, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(request, CURLOPT_WRITEFUNCTION, ReceiveResponse);
    curl_easy_setopt(request, CURLOPT_WRITEDATA, &buffer);

    CURLcode code = curl_easy_perform(request);
    long status = 0;
    curl_easy_getinfo(request, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(request);

    if (code != CURLE_OK || status != 200) {
        if (code != CURLE_OK) {
            snprintf(error, errorSize, "%s", curl_easy_strerror(code));
        } else {
            snprintf(error, errorSize, "HTTP error %ld", status);
        }
        free(text);
        return NULL;
    }
#endif
    return text;
}

static bool ReadNumber(const cJSON *object, const char *key, double *value)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble)) return false;
    *value = item->valuedouble;
    return true;
}

static bool ReadTime(const cJSON *object, char *time, size_t capacity)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, "time");
    if (!cJSON_IsString(item) || !item->valuestring ||
        strlen(item->valuestring) != 16) return false;
    snprintf(time, capacity, "%s", item->valuestring);
    time[10] = ' ';
    return true;
}

void FetchEnvironmentData(EnvironmentData *result)
{
    memset(result, 0, sizeof(*result));
    char *text = GetJson(weatherUrl, result->weatherError,
                         sizeof(result->weatherError));
    if (text) {
        cJSON *root = cJSON_Parse(text);
        const cJSON *current = cJSON_GetObjectItemCaseSensitive(root, "current");
        result->weatherValid =
            ReadNumber(current, "temperature_2m", &result->temperature) &&
            ReadNumber(current, "relative_humidity_2m", &result->humidity) &&
            result->humidity >= 0.0 && result->humidity <= 100.0 &&
            ReadTime(current, result->weatherTime, sizeof(result->weatherTime));
        if (!result->weatherValid) {
            snprintf(result->weatherError, sizeof(result->weatherError),
                     "Missing or invalid weather data");
        }
        cJSON_Delete(root);
        free(text);
    }

    text = GetJson(airUrl, result->airError, sizeof(result->airError));
    if (text) {
        cJSON *root = cJSON_Parse(text);
        const cJSON *current = cJSON_GetObjectItemCaseSensitive(root, "current");
        result->airValid =
            ReadNumber(current, "pm2_5", &result->pm25) && result->pm25 >= 0.0 &&
            ReadTime(current, result->airTime, sizeof(result->airTime));
        if (!result->airValid) {
            snprintf(result->airError, sizeof(result->airError),
                     "Missing or invalid air quality data");
        }
        cJSON_Delete(root);
        free(text);
    }
}

#ifdef _WIN32
static DWORD WINAPI RefreshWorker(LPVOID unused)
{
    (void)unused;
    FetchEnvironmentData(&workerResult);
    return 0;
}
#else
static void *RefreshWorker(void *unused)
{
    (void)unused;
    FetchEnvironmentData(&workerResult);
    pthread_mutex_lock(&workerMutex);
    workerDone = true;
    pthread_mutex_unlock(&workerMutex);
    return NULL;
}
#endif

bool DataSourceInit(void)
{
#ifdef _WIN32
    return true;
#else
    return curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
#endif
}

bool StartDataRefresh(void)
{
    if (workerActive) return false;
#ifdef _WIN32
    worker = CreateThread(NULL, 0, RefreshWorker, NULL, 0, NULL);
    if (!worker) return false;
#else
    pthread_mutex_lock(&workerMutex);
    workerDone = false;
    pthread_mutex_unlock(&workerMutex);
    if (pthread_create(&worker, NULL, RefreshWorker, NULL) != 0) return false;
#endif
    workerActive = true;
    return true;
}

bool PollDataRefresh(EnvironmentData *result)
{
    if (!workerActive) return false;
#ifdef _WIN32
    if (WaitForSingleObject(worker, 0) != WAIT_OBJECT_0) return false;
    CloseHandle(worker);
    worker = NULL;
#else
    pthread_mutex_lock(&workerMutex);
    bool done = workerDone;
    pthread_mutex_unlock(&workerMutex);
    if (!done) return false;
    pthread_join(worker, NULL);
#endif
    *result = workerResult;
    workerActive = false;
    return true;
}

void DataSourceClose(void)
{
    if (workerActive) {
#ifdef _WIN32
        WaitForSingleObject(worker, INFINITE);
        CloseHandle(worker);
#else
        pthread_join(worker, NULL);
#endif
        workerActive = false;
    }
#ifndef _WIN32
    curl_global_cleanup();
#endif
}
