#include "app_support.h"
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); failures++; } } while (0)

static void Write(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb"); CHECK(file != NULL);
    if (file) { CHECK(fputs(text, file) >= 0); fclose(file); }
}

int main(void)
{
    char path[256];
    CHECK(BuildExportFolderPath("/Users/test/Weather Project", path, sizeof(path)));
    CHECK(strcmp(path, "/Users/test/Weather Project/exports") == 0);
    CHECK(BuildExportFolderPath("/Users/test/Weather Project/", path, sizeof(path)));
    CHECK(strcmp(path, "/Users/test/Weather Project/exports") == 0);
    CHECK(BuildExportFolderPath("C:\\Projects\\Weather Project\\", path, sizeof(path)));
    CHECK(strcmp(path, "C:\\Projects\\Weather Project\\exports") == 0);
    CHECK(!BuildExportFolderPath(NULL, path, sizeof(path)) && path[0] == '\0');
    CHECK(!BuildExportFolderPath("", path, sizeof(path)));
    CHECK(!BuildExportFolderPath("/Project", path, 4) && path[0] == '\0');
    CHECK(!OpenExportFolder(""));

    const struct { time_t stamp; const char *date; } cases[] = {
        {1767310200, "2026-01-01"}, {1782948600, "2026-07-02"},
        {1774740600, "2026-03-28"}, {1774827000, "2026-03-30"},
        {1792884600, "2026-10-25"}, {1792971000, "2026-10-25"}
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char date[11]; CHECK(IrelandCalendarDate(cases[i].stamp, date));
        CHECK(strcmp(date, cases[i].date) == 0);
    }
    EnvironmentData data = {.forecastValid = true, .dayCount = 2};
    snprintf(data.days[0].date, sizeof(data.days[0].date), "2026-11-03");
    snprintf(data.days[1].date, sizeof(data.days[1].date), "2026-11-04");
    CHECK(ForecastContainsDate(&data, "2026-11-03"));
    CHECK(!ForecastContainsDate(&data, "2026-11-02"));
    CHECK(!ForecastContainsDate(&data, "2026-11-05"));
    data.dayCount = FORECAST_DAYS + 1;
    CHECK(!ForecastContainsDate(&data, "2026-11-03"));

    char source[160], target[160];
    snprintf(source, sizeof(source), "copy_test_%lld_%lu_source.txt", (long long)time(NULL), (unsigned long)clock());
    snprintf(target, sizeof(target), "%s.copy", source);
    Write(source, "original CSV bytes\r\n");
    CHECK(CopyFileNoReplace(source, target));
    Write(source, "new content, must not overwrite destination");
    CHECK(!CopyFileNoReplace(source, target));
    FILE *file = fopen(target, "rb"); CHECK(file != NULL);
    if (file) { char contents[64] = {0}; fread(contents, 1, sizeof(contents) - 1, file); fclose(file); CHECK(strcmp(contents, "original CSV bytes\r\n") == 0); }
    CHECK(!CopyFileNoReplace(source, source));
    CHECK(remove(source) == 0); CHECK(remove(target) == 0);
    printf("Project paths/dates/copy tests: %s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
