#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "app_support.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

bool BuildExportFolderPath(const char *projectDirectory, char *path, size_t capacity)
{
    if (!path || !capacity) return false;
    path[0] = '\0';
    if (!projectDirectory || !projectDirectory[0]) return false;
    size_t length = strlen(projectDirectory);
    const char *separator = projectDirectory[length - 1] == '/' || projectDirectory[length - 1] == '\\' ? "" : "/";
    int written = snprintf(path, capacity, "%s%sexports", projectDirectory, separator);
    if (written < 0 || (size_t)written >= capacity) { path[0] = '\0'; return false; }
    return true;
}

bool OpenExportFolder(const char *path)
{
    if (!path || !path[0]) return false;
#ifdef _WIN32
    wchar_t widePath[4096];
    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, widePath, 4096)) return false;
    return (INT_PTR)ShellExecuteW(NULL, L"open", widePath, NULL, NULL, SW_SHOWNORMAL) > 32;
#else
    /* Pass the directory as one argument, never through a shell command. */
#if defined(__APPLE__)
    char *arguments[] = {"/usr/bin/open", (char *)path, NULL};
#else
    char *arguments[] = {"xdg-open", (char *)path, NULL};
#endif
    pid_t child;
    int started = posix_spawnp(&child, arguments[0], NULL, NULL, arguments, environ);
    if (started != 0) return false;
    int status;
    pid_t waited;
    do { waited = waitpid(child, &status, 0); } while (waited == -1 && errno == EINTR);
    return waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif
}

bool CopyFileNoReplace(const char *source, const char *destination)
{
    if (!source || !destination || strcmp(source, destination) == 0) return false;
    FILE *input = fopen(source, "rb");
    if (!input) return false;
    FILE *output = fopen(destination, "wbx");
    if (!output) { fclose(input); return false; }
    unsigned char buffer[4096];
    size_t length;
    bool ok = true;
    while ((length = fread(buffer, 1, sizeof(buffer), input)) > 0) {
        if (fwrite(buffer, 1, length, output) != length) { ok = false; break; }
    }
    if (ferror(input)) ok = false;
    fclose(input);
    if (fclose(output) != 0) ok = false;
    if (!ok) remove(destination);
    return ok;
}

bool IrelandCalendarDate(time_t now, char date[11])
{
    if (!date) return false;
    struct tm *value = gmtime(&now);
    if (!value) { date[0] = '\0'; return false; }
    struct tm utc = *value;
    bool summer = utc.tm_mon > 2 && utc.tm_mon < 9;
    if (utc.tm_mon == 2 || utc.tm_mon == 9) {
        /* Ireland follows the EU transition at 01:00 UTC on the last Sunday
           of March/October. Both months have 31 days. No process-wide TZ change. */
        int lastSunday = 31 - (utc.tm_wday + 31 - utc.tm_mday) % 7;
        bool transitioned = utc.tm_mday > lastSunday || (utc.tm_mday == lastSunday && utc.tm_hour >= 1);
        summer = utc.tm_mon == 2 ? transitioned : !transitioned;
    }
    if (summer) {
        time_t ireland = now + 3600;
        value = gmtime(&ireland);
        if (!value) { date[0] = '\0'; return false; }
        utc = *value;
    }
    return strftime(date, 11, "%Y-%m-%d", &utc) == 10;
}

bool ForecastContainsDate(const EnvironmentData *data, const char *date)
{
    if (!data || !date || !data->forecastValid || data->dayCount < 1 || data->dayCount > FORECAST_DAYS) return false;
    for (int i = 0; i < data->dayCount; i++) if (strcmp(data->days[i].date, date) == 0) return true;
    return false;
}
