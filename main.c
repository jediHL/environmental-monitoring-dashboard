#include "raylib.h"
#include "rlgl.h"
#include "counties.h"
#include "data_source.h"
#include "ireland_map.h"
#include "refresh_policy.h"
#include "weather_storage.h"
#include "app_support.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define CANVAS_WIDTH 1440.0f
#define CANVAS_HEIGHT 900.0f
#define ROW_HEIGHT 52

typedef struct {
    WeatherSnapshot snapshot;
    RefreshPolicy refresh;
    bool attempted;
    bool failed;
    bool weatherSaved;
    bool forecastSaved;
    bool airSaved;
    bool saveFailed;
} LocationCache;

static Font font;
static const Color INK = {28, 51, 62, 255};
static const Color MUTED = {85, 105, 117, 255};
static const Color TEAL = {29, 112, 108, 255};
static const Color PALE = {232, 244, 239, 255};
static const Color LINE = {231, 237, 239, 255};
static const Color AMBER = {171, 100, 23, 255};
static const Rectangle MAP_VIEW = {304, 205, 422, 516};
static const Rectangle LIST_VIEW = {36, 224, 222, 552};

static void Text(const char *value, float x, float y, float size, Color color)
{
    DrawTextEx(font, value, (Vector2){x, y}, size, 0, color);
}

static void FitText(const char *value, float x, float y, float size, float width, Color color)
{
    float measured = MeasureTextEx(font, value, size, 0).x;
    if (measured > width) size *= width / measured;
    Text(value, x, y, size, color);
}

static void Panel(Rectangle box, Color color)
{
    DrawRectangleRounded(box, 0.055f, 10, color);
}

/* daylight: 1 = day, 0 = night, -1 = not supplied (e.g. an older saved file). */
static void WeatherIcon(Vector2 p, int code, float size, int daylight, Color background)
{
    bool sunny = code <= 2;
    bool cloud = code != 0;
    if (sunny) {
        Vector2 sky = {p.x + (cloud ? -size * .22f : 0), p.y - size * (cloud ? .20f : .13f)};
        if (daylight == 1) {
            for (int i = 0; i < 8; i++) {
                float a = (float)i * PI / 4;
                DrawLineEx((Vector2){sky.x + cosf(a) * size * .34f, sky.y + sinf(a) * size * .34f},
                           (Vector2){sky.x + cosf(a) * size * .45f, sky.y + sinf(a) * size * .45f},
                           size * .055f, (Color){232, 169, 57, 255});
            }
            DrawCircleV(sky, size * .24f, (Color){245, 190, 78, 255});
        } else if (daylight == 0) {
            Color moon = {80, 111, 163, 255};
            DrawCircleV(sky, size * .29f, moon);
            DrawCircleV((Vector2){sky.x + size * .13f, sky.y - size * .10f}, size * .25f, background);
            if (!cloud) {
                DrawCircleV((Vector2){p.x + size * .33f, p.y - size * .31f}, size * .035f, moon);
                DrawCircleV((Vector2){p.x + size * .43f, p.y + size * .03f}, size * .022f, moon);
            }
        } else {
            /* Never guess day/night from the computer clock for old snapshots. */
            DrawCircleV(sky, size * .24f, MUTED);
            DrawCircleV(sky, size * .17f, background);
        }
    }
    if (cloud) {
        Color grey = {151, 174, 184, 255};
        DrawCircleV((Vector2){p.x - size * .2f, p.y}, size * .22f, grey);
        DrawCircleV((Vector2){p.x + size * .02f, p.y - size * .12f}, size * .29f, grey);
        DrawCircleV((Vector2){p.x + size * .27f, p.y}, size * .2f, grey);
        DrawRectangleRec((Rectangle){p.x - size * .2f, p.y, size * .47f, size * .2f}, grey);
        if (code >= 51) {
            Color blue = code >= 71 && code <= 77 ? (Color){167, 196, 212, 255} : (Color){71, 146, 182, 255};
            for (int i = 0; i < 3; i++) {
                float x = p.x + (i - 1) * size * .23f;
                DrawLineEx((Vector2){x, p.y + size * .3f},
                           (Vector2){x - size * .06f, p.y + size * .44f}, size * .055f, blue);
            }
        }
    }
}

static void DateLabel(const char *iso, int index, char *out, size_t capacity)
{
    (void)index; /* Always show the date, including when cached data are from yesterday. */
    int y = 0, m = 0, d = 0;
    if (sscanf(iso, "%d-%d-%d", &y, &m, &d) != 3) { snprintf(out, capacity, "%s", iso); return; }
    struct tm date = {0};
    date.tm_year = y - 1900; date.tm_mon = m - 1; date.tm_mday = d;
    date.tm_hour = 12; date.tm_isdst = -1;
    if (mktime(&date) == (time_t)-1) { snprintf(out, capacity, "%s", iso); return; }
    const char *week[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    snprintf(out, capacity, "%s %02d", week[date.tm_wday], d);
}

static void Metric(float x, const char *label, const char *value, Color accent)
{
    Panel((Rectangle){x, 376, 192, 76}, (Color){246, 249, 250, 255});
    Text(label, x + 14, 386, 15, MUTED);
    Text(value, x + 14, 409, 26, accent);
}

static void DrawTemperatureChart(const EnvironmentData *data, Vector2 mouse)
{
    const Color lowColor = {59, 137, 183, 255};
    const Color highColor = {211, 133, 48, 255};
    /* Leave room for readable axis labels instead of squeezing small text
       around the full-width plot. All sizes are logical pixels. */
    const Rectangle plot = {858, 626, 500, 127};
    const float axisFontSize = 18;
    const float dateFontSize = 17;
    const float valueFontSize = 18;
    double minimum = data->days[0].low, maximum = data->days[0].high;
    for (int i = 1; i < data->dayCount; i++) {
        minimum = fmin(minimum, data->days[i].low);
        maximum = fmax(maximum, data->days[i].high);
    }
    double bottom = floor(minimum - 1), top = ceil(maximum + 1);
    if (!isfinite(top - bottom) || top - bottom > 10000) {
        Text("Temperature range cannot be plotted.", 784, 640, 18, AMBER); return;
    }
    Text("TEMPERATURE / C", 784, 576, 15, INK);
    DrawLineEx((Vector2){1111, 585}, (Vector2){1133, 585}, 3, lowColor);
    Text("Minimum", 1140, 575, 17, INK);
    DrawLineEx((Vector2){1253, 585}, (Vector2){1275, 585}, 3, highColor);
    Text("Maximum", 1282, 575, 17, INK);
    for (int i = 0; i <= 4; i++) {
        float y = plot.y + plot.height * i / 4;
        double temperature = top - (top - bottom) * i / 4;
        DrawLineEx((Vector2){plot.x, y}, (Vector2){plot.x + plot.width, y}, 1, LINE);
        const char *label = TextFormat("%.1f", temperature);
        float width = MeasureTextEx(font, label, axisFontSize, 0).x;
        Text(label, plot.x - width - 14, y - axisFontSize / 2, axisFontSize, INK);
    }
    Vector2 lows[FORECAST_DAYS], highs[FORECAST_DAYS];
    for (int i = 0; i < data->dayCount; i++) {
        float x = plot.x + plot.width * i / (data->dayCount - 1);
        lows[i] = (Vector2){x, plot.y + (float)((top - data->days[i].low) / (top - bottom)) * plot.height};
        highs[i] = (Vector2){x, plot.y + (float)((top - data->days[i].high) / (top - bottom)) * plot.height};
        if (i) { DrawLineEx(lows[i - 1], lows[i], 2.5f, lowColor); DrawLineEx(highs[i - 1], highs[i], 2.5f, highColor); }
        char label[24]; DateLabel(data->days[i].date, i, label, sizeof(label));
        float width = MeasureTextEx(font, label, dateFontSize, 0).x;
        Text(label, x - width / 2, 779, dateFontSize, INK);
    }
    int hover = -1;
    if (CheckCollisionPointRec(mouse, (Rectangle){plot.x - 15, plot.y - 12, plot.width + 30, plot.height + 30}))
        hover = (int)roundf((mouse.x - plot.x) * (data->dayCount - 1) / plot.width);
    for (int i = 0; i < data->dayCount; i++) {
        if (hover == i) DrawLineEx((Vector2){lows[i].x, plot.y}, (Vector2){lows[i].x, plot.y + plot.height}, 1, Fade(MUTED, .4f));
        DrawCircleV(lows[i], hover == i ? 5 : 3.5f, lowColor);
        DrawCircleV(highs[i], hover == i ? 5 : 3.5f, highColor);
        const char *highLabel = TextFormat("%.0f", data->days[i].high);
        float highWidth = MeasureTextEx(font, highLabel, valueFontSize, 0).x;
        Text(highLabel, highs[i].x - highWidth / 2, highs[i].y - 28, valueFontSize, highColor);
        const char *lowLabel = TextFormat("%.0f", data->days[i].low);
        float lowWidth = MeasureTextEx(font, lowLabel, valueFontSize, 0).x;
        Text(lowLabel, lows[i].x - lowWidth / 2, fminf(lows[i].y + 8, 757), valueFontSize, lowColor);
    }
    if (hover >= 0 && hover < data->dayCount) {
        const DailyForecast *day = &data->days[hover];
        Text(TextFormat("%s  |  Minimum %.1f C  |  Maximum %.1f C", day->date, day->low, day->high), 784, 805, 16, INK);
    } else Text("Daily forecast, not historical measurements. Hover a point for details.", 784, 805, 14, MUTED);
}

static void DrawWeather(const LocationCache *entry, int location, Vector2 mouse, bool chartView)
{
    const EnvironmentData *data = &entry->snapshot.data;
    const CountyTown *town = &LOCATIONS[location];
    const County *county = &COUNTIES[town->countyId];
    Panel((Rectangle){760, 112, 656, 718}, WHITE);
    Text(TextFormat("COUNTY %s  /  %s", county->name, county->province), 784, 129, 17, TEAL);
    FitText(town->town, 784, 153, 34, county->townCount > 1 ? 292 : 590, INK);
    if (county->townCount > 1) {
        for (int i = 0; i < county->townCount; i++) {
            int id = county->firstLocation + i;
            Rectangle chip = {1120 + i * 133.0f, 159, 126, 31};
            Panel(chip, location == id ? TEAL : PALE);
            Text(LOCATIONS[id].town, chip.x + 12, chip.y + 6, 15, location == id ? WHITE : TEAL);
        }
    }
    Text("Forecast for the town, not a county-wide average", 784, 195, 16, MUTED);
    Panel((Rectangle){784, 224, 608, 134}, PALE);
    if (data->weatherValid) {
        Text(TextFormat("%.1f C", data->temperature), 804, 238, 49, INK);
        Text(data->daylightValid ? TextFormat("%s / %s", WeatherDescription(data->weatherCode), data->isDay ? "Day" : "Night")
             : WeatherDescription(data->weatherCode), 805, 294, 22, TEAL);
        Text(TextFormat("Feels like %.1f C", data->apparentTemperature), 805, 327, 18, MUTED);
        WeatherIcon((Vector2){1316, 279}, data->weatherCode, 77, data->daylightValid ? (data->isDay ? 1 : 0) : -1, PALE);
        char fetched[40]; FormatUtcTime(entry->snapshot.weatherFetchedAt, fetched, sizeof(fetched));
        Text(entry->weatherSaved ? "SAVED WEATHER" : data->weatherError[0] ? "EARLIER WEATHER" : "WEATHER UPDATED", 1108, 314, 15, entry->weatherSaved ? AMBER : MUTED);
        Text(fetched, 1108, 335, 16, MUTED);
    } else {
        Text("-- C", 804, 238, 49, MUTED);
        Text(entry->failed ? "Weather unavailable; see status below" : "No weather data loaded yet", 805, 304, 18, MUTED);
    }
    Metric(784, "RELATIVE HUMIDITY", data->weatherValid ? TextFormat("%.0f %%", data->humidity) : "--", TEAL);
    Metric(992, "WIND SPEED", data->weatherValid ? TextFormat("%.1f km/h", data->wind) : "--", INK);
    Metric(1200, entry->airSaved ? "PM2.5 / SAVED" : "PM2.5 / MODEL", data->airValid ? TextFormat("%.1f ug/m3", data->pm25) : "--", INK);
    char today[11] = "";
    IrelandCalendarDate(time(NULL), today);
    bool coversToday = ForecastContainsDate(data, today);
    bool expired = data->forecastValid && today[0] && strcmp(data->days[data->dayCount - 1].date, today) < 0;
    Text(expired ? "Saved forecast - expired" : "7-day outlook", 784, 473, 24, expired ? AMBER : INK);
    Panel((Rectangle){784, 512, 300, 31}, chartView ? (Color){246, 249, 250, 255} : PALE);
    Panel((Rectangle){1092, 512, 300, 31}, chartView ? PALE : (Color){246, 249, 250, 255});
    Text("Forecast table", 800, 516, 18, chartView ? MUTED : TEAL);
    Text("Temperature chart", 1108, 516, 18, chartView ? TEAL : MUTED);
    if (data->forecastValid) {
        char fetched[40]; FormatUtcTime(entry->snapshot.forecastFetchedAt, fetched, sizeof(fetched));
        Text(TextFormat("%s / %s / %s", entry->forecastSaved ? "SAVED FORECAST" : data->forecastError[0] ? "EARLIER FORECAST" : "FORECAST UPDATED", fetched,
             coversToday ? "covers today" : expired ? "EXPIRED" : "check dates"), 784, 550, 16, entry->forecastSaved || !coversToday ? AMBER : MUTED);
    }
    if (data->forecastValid && chartView) DrawTemperatureChart(data, mouse);
    else if (data->forecastValid) {
        Text("DAILY CONDITIONS", 784, 578, 15, INK);
        Text("LOW / HIGH", 1080, 578, 15, INK);
        Text("RAIN CHANCE", 1290, 578, 15, INK);
        double minimum = data->days[0].low, maximum = data->days[0].high;
        for (int i = 1; i < data->dayCount; i++) {
            minimum = fmin(minimum, data->days[i].low); maximum = fmax(maximum, data->days[i].high);
        }
        double range = fmax(1, maximum - minimum);
        for (int i = 0; i < data->dayCount; i++) {
            const DailyForecast *day = &data->days[i];
            float y = 600 + i * 28.0f;
            Rectangle row = {777, y - 1, 622, 27};
            if (CheckCollisionPointRec(mouse, row)) Panel(row, (Color){242, 247, 247, 255});
            char label[24]; DateLabel(day->date, i, label, sizeof(label));
            Text(label, 784, y + 3, 19, INK);
            /* Daily rows summarise a whole day, rather than a current night observation. */
            WeatherIcon((Vector2){876, y + 12}, day->weatherCode, 21, 1, WHITE);
            FitText(WeatherDescription(day->weatherCode), 902, y + 4, 17, 166, INK);
            Text(TextFormat("%.0f / %.0f C", day->low, day->high), 1080, y + 3, 19, INK);
            DrawLineEx((Vector2){1192, y + 13}, (Vector2){1271, y + 13}, 5, LINE);
            float a = 1192 + (float)((day->low - minimum) / range) * 79;
            float b = 1192 + (float)((day->high - minimum) / range) * 79;
            DrawLineEx((Vector2){a, y + 13}, (Vector2){fmaxf(a + 3, b), y + 13}, 5, TEAL);
            Text(TextFormat("%.0f %%", day->rainProbability), 1310, y + 3, 19, TEAL);
            DrawLine(784, (int)y + 27, 1392, (int)y + 27, LINE);
            if (CheckCollisionPointRec(mouse, row))
                Text(TextFormat("%s  |  Rain %.1f mm  |  Max wind %.1f km/h", day->date, day->rainMm, day->windMax), 784, 805, 16, INK);
        }
    } else {
        Text("No forecast loaded for this town yet.", 784, 622, 18, MUTED);
        Text("Connect to the internet to download this town's forecast.", 784, 654, 14, MUTED);
    }
}

static int CheckData(const char *name)
{
    int location = name ? FindLocation(name) : 0;
    if (location < 0) { fprintf(stderr, "Unknown county or county town: %s\n", name); return 2; }
    if (!DataSourceInit()) return 1;
    const CountyTown *town = &LOCATIONS[location];
    EnvironmentData data;
    FetchEnvironmentData(location, town->latitude, town->longitude, &data);
    printf("%s, County %s (%.5f, %.5f), Europe/Dublin\n", town->town, COUNTIES[town->countyId].name, town->latitude, town->longitude);
    if (data.weatherValid) printf("Weather %s: %.1f C, humidity %.0f %%, wind %.1f km/h, %s\n", data.weatherTime, data.temperature, data.humidity, data.wind, WeatherDescription(data.weatherCode));
    else fprintf(stderr, "Weather: %s\n", data.weatherError);
    if (data.daylightValid) printf("Daylight: %s (API is_day=%d)\n", data.isDay ? "day" : "night", data.isDay ? 1 : 0);
    if (data.forecastValid) {
        for (int i = 0; i < data.dayCount; i++) {
            DailyForecast *day = &data.days[i];
            printf("%s: %.1f..%.1f C, rain %.0f %% / %.1f mm, max wind %.1f km/h, %s\n", day->date, day->low, day->high, day->rainProbability, day->rainMm, day->windMax, WeatherDescription(day->weatherCode));
        }
    } else fprintf(stderr, "Forecast: %s\n", data.forecastError);
    if (data.airValid) printf("PM2.5 %s: %.1f ug/m3\n", data.airTime, data.pm25);
    else fprintf(stderr, "Air quality: %s\n", data.airError);
    DataSourceClose();
    return data.weatherValid && data.forecastValid && data.airValid ? 0 : 1;
}

static int CheckMap(const char *path)
{
    if (!LoadIrelandMap(path)) { fprintf(stderr, "Cannot load map: %s\n", path); return 1; }
    int failed = 0;
    for (int i = 0; i < COUNTY_COUNT; i++) {
        Vector2 p = MapCountyInterior(MAP_VIEW, i);
        if (PickMapCounty(MAP_VIEW, p) != i) { fprintf(stderr, "Map hit test failed: %s\n", COUNTIES[i].name); failed++; }
    }
    if (PickMapCounty(MAP_VIEW, (Vector2){MAP_VIEW.x, MAP_VIEW.y}) != -1) failed++;
    if (PickMapCounty(MAP_VIEW, MapTownPoint(MAP_VIEW, 54.60, -5.93)) != -1) failed++;
    printf("Map: %d/26 county interior hit tests passed; ocean/NI excluded\n", COUNTY_COUNT - failed);
    UnloadIrelandMap();
    return failed ? 1 : 0;
}

static void SelectLocation(int next, int *selected, double *changedAt)
{
    if (next >= 0 && next < LOCATION_COUNT && next != *selected) {
        *selected = next; *changedAt = GetTime();
    }
}

int main(int argc, char **argv)
{
    char mapPath[4096], fontPath[4096];
    snprintf(mapPath, sizeof(mapPath), "%sassets/maps/ireland_counties.json", GetApplicationDirectory());
    snprintf(fontPath, sizeof(fontPath), "%sassets/fonts/NotoSans.ttf", GetApplicationDirectory());
    if (argc >= 2 && strcmp(argv[1], "--check-data") == 0) return CheckData(argc >= 3 ? argv[2] : NULL);
    if (argc >= 2 && strcmp(argv[1], "--check-map") == 0) return CheckMap(argc >= 3 ? argv[2] : mapPath);
    bool offline = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--offline") == 0) offline = true;
        else { fprintf(stderr, "Usage: dashboard [--offline | --check-data [town] | --check-map [path]]\n"); return 2; }
    }
    SetConfigFlags(FLAG_WINDOW_HIGHDPI | FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(1380, 863, "Ireland Weather Atlas | Environmental Monitoring Dashboard");
    SetWindowMinSize(1000, 625);
    SetTargetFPS(60);
    font = LoadFontEx(fontPath, 64, NULL, 0);
    bool customFont = font.texture.id != GetFontDefault().texture.id;
    if (customFont) { GenTextureMipmaps(&font.texture); SetTextureFilter(font.texture, TEXTURE_FILTER_TRILINEAR); }
    bool mapReady = LoadIrelandMap(mapPath);
    bool sourceReady = !offline && DataSourceInit();
    char cacheDirectory[4096] = "", exportDirectory[4096] = "", legacyCache[4096], legacyExports[4096];
    /* CMake supplies this checkout's root, regardless of CLion's working directory.
       A standalone copy uses its executable directory if the checkout is absent. */
    const char *projectDirectory = GetApplicationDirectory();
#ifdef DASHBOARD_PROJECT_DIR
    if (DirectoryExists(DASHBOARD_PROJECT_DIR)) projectDirectory = DASHBOARD_PROJECT_DIR;
#endif
    bool exportWritable = BuildExportFolderPath(projectDirectory, exportDirectory, sizeof(exportDirectory)) && MakeDirectory(exportDirectory) == 0;
    int cachePathSize = snprintf(cacheDirectory, sizeof(cacheDirectory), "%s/user_data/cache", projectDirectory);
    bool cacheWritable = cachePathSize > 0 && cachePathSize < (int)sizeof(cacheDirectory) && MakeDirectory(cacheDirectory) == 0;
    snprintf(legacyCache, sizeof(legacyCache), "%suser_data/cache", GetApplicationDirectory());
    snprintf(legacyExports, sizeof(legacyExports), "%suser_data/exports", GetApplicationDirectory());
    /* Preserve existing exports and migrate older builds without deleting files. */
    if (exportWritable && DirectoryExists(legacyExports)) {
        FilePathList files = LoadDirectoryFilesEx(legacyExports, ".csv", false);
        for (unsigned int i = 0; i < files.count; i++) {
            char destination[4096];
            int length = snprintf(destination, sizeof(destination), "%s/%s", exportDirectory, GetFileName(files.paths[i]));
            if (length > 0 && length < (int)sizeof(destination) && !FileExists(destination))
                CopyFileNoReplace(files.paths[i], destination);
        }
        UnloadDirectoryFiles(files);
    }
    LocationCache cache[LOCATION_COUNT] = {0};
    for (int i = 0; i < LOCATION_COUNT; i++) {
        cache[i].snapshot.data.locationId = i; cache[i].refresh = RefreshPolicyInit(0);
        char path[4096], error[160];
        int length = snprintf(path, sizeof(path), "%s/town_%02d.json", cacheDirectory, i);
        if (cacheDirectory[0] && length > 0 && length < (int)sizeof(path) && FileExists(path) &&
            !LoadWeatherSnapshot(path, i, &cache[i].snapshot, error, sizeof(error)))
            TraceLog(LOG_WARNING, "Cache (%s): %s", LOCATIONS[i].town, error);
        char previousPath[4096];
        int previousLength = snprintf(previousPath, sizeof(previousPath), "%s/town_%02d.json", legacyCache, i);
        WeatherSnapshot previous;
        if (previousLength > 0 && previousLength < (int)sizeof(previousPath) && FileExists(previousPath) &&
            LoadWeatherSnapshot(previousPath, i, &previous, error, sizeof(error)) && MergeNewerSnapshot(&cache[i].snapshot, &previous) && cacheWritable &&
            length > 0 && length < (int)sizeof(path))
            SaveWeatherSnapshot(path, &cache[i].snapshot, error, sizeof(error));
        cache[i].weatherSaved = cache[i].snapshot.data.weatherValid;
        cache[i].forecastSaved = cache[i].snapshot.data.forecastValid;
        cache[i].airSaved = cache[i].snapshot.data.airValid;
    }
    int selected = 0, activeLocation = -1, scroll = 0;
    char query[64] = "";
    bool searchFocused = false, fetching = false;
    bool chartView = true;
    double changedAt = -1, networkAvailableAt = 0;
    char notice[240] = "";
    double noticeUntil = 0;
    bool noticeError = false;
    unsigned int exportSequence = 0;
    const Rectangle searchBox = {40, 170, 214, 37};
    const Rectangle refreshBox = {1220, 32, 196, 45};
    const Rectangle exportBox = {1048, 32, 156, 45};
    const Rectangle openExportsBox = {884, 32, 148, 45};

    while (!WindowShouldClose()) {
        double now = GetTime();
        float scale = fminf((float)GetScreenWidth() / CANVAS_WIDTH, (float)GetScreenHeight() / CANVAS_HEIGHT);
        Vector2 offset = {((float)GetScreenWidth() - CANVAS_WIDTH * scale) / 2, ((float)GetScreenHeight() - CANVAS_HEIGHT * scale) / 2};
        Vector2 rawMouse = GetMousePosition();
        Vector2 mouse = {(rawMouse.x - offset.x) / scale, (rawMouse.y - offset.y) / scale};
        bool clicked = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
        if (clicked) searchFocused = CheckCollisionPointRec(mouse, searchBox);
        if (searchFocused) {
            int character;
            while ((character = GetCharPressed()) != 0) {
                size_t length = strlen(query);
                if (character >= 32 && character <= 126 && length + 1 < sizeof(query)) {
                    query[length] = (char)character; query[length + 1] = '\0'; scroll = 0;
                }
            }
            if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) {
                size_t length = strlen(query); if (length) query[length - 1] = '\0'; scroll = 0;
            }
            if (IsKeyPressed(KEY_ENTER)) searchFocused = false;
        }
        int visible[COUNTY_COUNT], count = 0;
        for (int i = 0; i < COUNTY_COUNT; i++) if (CountyMatches(i, query)) visible[count++] = i;
        int maxScroll = count * ROW_HEIGHT - (int)LIST_VIEW.height;
        if (maxScroll < 0) maxScroll = 0;
        if (CheckCollisionPointRec(mouse, LIST_VIEW)) scroll -= (int)(GetMouseWheelMove() * ROW_HEIGHT);
        if (scroll < 0) scroll = 0;
        if (scroll > maxScroll) scroll = maxScroll;
        for (int i = 0; i < count; i++) {
            Rectangle row = {LIST_VIEW.x, LIST_VIEW.y + i * ROW_HEIGHT - scroll, LIST_VIEW.width - 4, ROW_HEIGHT - 2};
            if (clicked && CheckCollisionPointRec(mouse, LIST_VIEW) && CheckCollisionPointRec(mouse, row)) {
                int next = FindCountyTown(visible[i], query);
                SelectLocation(next, &selected, &changedAt);
            }
        }
        int hovered = mapReady ? PickMapCounty(MAP_VIEW, mouse) : -1;
        if (mapReady && clicked && CheckCollisionPointRec(mouse, MAP_VIEW)) {
            int next = -1; float closest = 10;
            for (int i = 0; i < LOCATION_COUNT; i++) {
                Vector2 point = MapTownPoint(MAP_VIEW, LOCATIONS[i].latitude, LOCATIONS[i].longitude);
                float distance = hypotf(mouse.x - point.x, mouse.y - point.y);
                if (distance < closest) { closest = distance; next = i; }
            }
            if (next < 0 && hovered >= 0) next = COUNTIES[hovered].firstLocation;
            if (next >= 0) {
                SelectLocation(next, &selected, &changedAt);
                query[0] = '\0'; count = COUNTY_COUNT;
                for (int i = 0; i < count; i++) visible[i] = i;
                scroll = LOCATIONS[next].countyId * ROW_HEIGHT - 5 * ROW_HEIGHT;
                if (scroll < 0) scroll = 0;
                int limit = COUNTY_COUNT * ROW_HEIGHT - (int)LIST_VIEW.height;
                if (scroll > limit) scroll = limit;
            }
        }
        int countyId = LOCATIONS[selected].countyId;
        if (clicked && CheckCollisionPointRec(mouse, (Rectangle){784, 512, 300, 31})) chartView = false;
        if (clicked && CheckCollisionPointRec(mouse, (Rectangle){1092, 512, 300, 31})) chartView = true;
        if (COUNTIES[countyId].townCount > 1) {
            for (int i = 0; i < 2; i++)
                if (clicked && CheckCollisionPointRec(mouse, (Rectangle){1120 + i * 133.0f, 159, 126, 31}))
                    SelectLocation(COUNTIES[countyId].firstLocation + i, &selected, &changedAt);
        }
        EnvironmentData incoming;
        if (fetching && PollDataRefresh(&incoming)) {
            fetching = false;
            if (incoming.locationId >= 0 && incoming.locationId < LOCATION_COUNT) {
                LocationCache *completed = &cache[incoming.locationId];
                MergeEnvironmentData(&completed->snapshot.data, &incoming);
                time_t fetchedAt = time(NULL);
                if (incoming.weatherValid) { completed->weatherSaved = false; completed->snapshot.weatherFetchedAt = fetchedAt; }
                if (incoming.forecastValid) { completed->forecastSaved = false; completed->snapshot.forecastFetchedAt = fetchedAt; }
                if (incoming.airValid) { completed->airSaved = false; completed->snapshot.airFetchedAt = fetchedAt; }
                if (incoming.weatherValid || incoming.forecastValid || incoming.airValid) {
                    char path[4096], error[160] = "Cache folder is not writable";
                    int length = snprintf(path, sizeof(path), "%s/town_%02d.json", cacheDirectory, incoming.locationId);
                    completed->saveFailed = !cacheWritable || length < 0 || length >= (int)sizeof(path) ||
                        !SaveWeatherSnapshot(path, &completed->snapshot, error, sizeof(error));
                    if (completed->saveFailed) TraceLog(LOG_WARNING, "Save (%s): %s", LOCATIONS[incoming.locationId].town, error);
                }
                completed->failed = !incoming.weatherValid || !incoming.forecastValid || !incoming.airValid;
                RefreshPolicyFinished(&completed->refresh, now, !completed->failed);
                /* Switching counties must not bypass a failed weather service's retry delay. */
                networkAvailableAt = now + ((!incoming.weatherValid && !incoming.forecastValid) ? INITIAL_RETRY_SECONDS : 1);
                if (!incoming.weatherValid) TraceLog(LOG_WARNING, "Weather (%s): %s", LOCATIONS[incoming.locationId].town, incoming.weatherError);
                if (!incoming.airValid) TraceLog(LOG_WARNING, "Air quality (%s): %s", LOCATIONS[incoming.locationId].town, incoming.airError);
            }
            activeLocation = -1;
        }
        LocationCache *entry = &cache[selected];
        if (clicked && CheckCollisionPointRec(mouse, openExportsBox)) {
            bool opened = exportWritable && OpenExportFolder(exportDirectory);
            snprintf(notice, sizeof(notice), "%s", opened ? "Opened the project's exports folder." : "Cannot open the exports folder.");
            noticeError = !opened; noticeUntil = now + 8;
        }
        if (clicked && CheckCollisionPointRec(mouse, exportBox)) {
            char error[160] = "Download a forecast before exporting";
            bool exported = false;
            if (entry->snapshot.data.forecastValid && exportWritable) {
                char suffix[32] = "unknown_time";
                time_t exportedAt = time(NULL);
                struct tm *utc = gmtime(&exportedAt);
                if (utc) strftime(suffix, sizeof(suffix), "%Y%m%d_%H%M%S", utc);
                for (int attempt = 0; attempt < 1000; attempt++) {
                    char path[4096], filename[128];
                    snprintf(filename, sizeof(filename), "forecast_%s_%s_%03u.csv", LOCATIONS[selected].town, suffix, exportSequence++);
                    int length = snprintf(path, sizeof(path), "%s/%s", exportDirectory, filename);
                    if (length < 0 || length >= (int)sizeof(path)) { snprintf(error, sizeof(error), "Export path is too long"); break; }
                    if (FileExists(path)) continue;
                    exported = ExportForecastCsv(path, &entry->snapshot, entry->forecastSaved, error, sizeof(error));
                    if (exported) {
                        snprintf(notice, sizeof(notice), "Saved %s  |  Project folder: exports. Use Open exports.", filename);
                        TraceLog(LOG_INFO, "CSV saved: %s", path);
                    }
                    break;
                }
            } else if (!exportWritable) snprintf(error, sizeof(error), "Export folder is not writable");
            if (!exported) snprintf(notice, sizeof(notice), "CSV export: %s", error);
            noticeError = !exported;
            noticeUntil = now + 10;
        }
        bool manual = (!searchFocused && IsKeyPressed(KEY_R)) || (clicked && CheckCollisionPointRec(mouse, refreshBox));
        if (sourceReady && now - changedAt >= .3 && now >= networkAvailableAt && RefreshPolicyDue(&entry->refresh, now, manual, fetching)) {
            const CountyTown *town = &LOCATIONS[selected];
            fetching = StartDataRefresh(selected, town->latitude, town->longitude);
            entry->attempted = true;
            RefreshPolicyStarted(&entry->refresh, now);
            if (fetching) activeLocation = selected;
            else {
                entry->failed = true;
                snprintf(entry->snapshot.data.weatherError, sizeof(entry->snapshot.data.weatherError), "Cannot start network refresh");
                RefreshPolicyFinished(&entry->refresh, now, false); networkAvailableAt = now + INITIAL_RETRY_SECONDS;
            }
        }
        double deadline = fmax(entry->refresh.manualAllowedAt, networkAvailableAt);
        int wait = RefreshSecondsLeft(deadline, now);
        bool canRefresh = sourceReady && !fetching && !wait;
        bool interactive = CheckCollisionPointRec(mouse, refreshBox) || CheckCollisionPointRec(mouse, exportBox) || CheckCollisionPointRec(mouse, openExportsBox) ||
                           CheckCollisionPointRec(mouse, (Rectangle){784, 512, 608, 31}) || hovered >= 0 || CheckCollisionPointRec(mouse, LIST_VIEW);
        SetMouseCursor(searchFocused ? MOUSE_CURSOR_IBEAM : interactive ? MOUSE_CURSOR_POINTING_HAND : MOUSE_CURSOR_DEFAULT);

        BeginDrawing();
        ClearBackground((Color){242, 246, 247, 255});
        /* Keep BeginDrawing's HiDPI transform; BeginMode2D resets it in raylib 6. */
        rlPushMatrix();
        rlTranslatef(offset.x, offset.y, 0);
        rlScalef(scale, scale, 1);
        Text("Ireland weather atlas", 24, 24, 35, INK);
        Text("ENVIRONMENTAL MONITORING  /  26 COUNTIES  /  7-DAY FORECAST", 26, 74, 16, MUTED);
        Panel(refreshBox, canRefresh ? TEAL : (Color){224, 231, 233, 255});
        Text(fetching ? "Updating..." : wait ? TextFormat("Refresh in %d s", wait) : "Refresh weather  [R]", 1236, 45, 16, canRefresh ? WHITE : MUTED);
        bool canExport = entry->snapshot.data.forecastValid && exportWritable;
        Panel(exportBox, canExport ? PALE : (Color){224, 231, 233, 255});
        Text("Export CSV", 1070, 45, 16, canExport ? TEAL : MUTED);
        Panel(openExportsBox, exportWritable ? PALE : (Color){224, 231, 233, 255});
        Text("Open exports", 900, 45, 16, exportWritable ? TEAL : MUTED);
        Panel((Rectangle){24, 112, 246, 718}, WHITE);
        Text("Counties", 40, 131, 24, INK);
        Panel(searchBox, (Color){244, 248, 249, 255});
        if (searchFocused) DrawRectangleRoundedLinesEx(searchBox, .15f, 6, 1, TEAL);
        FitText(query[0] ? query : "Search county / town", 51, 179, 17, 192, query[0] ? INK : MUTED);
        BeginScissorMode((int)(offset.x + LIST_VIEW.x * scale), (int)(offset.y + LIST_VIEW.y * scale), (int)ceilf(LIST_VIEW.width * scale), (int)ceilf(LIST_VIEW.height * scale));
        for (int i = 0; i < count; i++) {
            float y = LIST_VIEW.y + i * ROW_HEIGHT - scroll;
            if (y + ROW_HEIGHT < LIST_VIEW.y || y > LIST_VIEW.y + LIST_VIEW.height) continue;
            int id = visible[i]; Rectangle row = {36, y, 216, ROW_HEIGHT - 2}; bool picked = id == countyId;
            if (picked || CheckCollisionPointRec(mouse, row)) Panel(row, picked ? PALE : (Color){247, 249, 250, 255});
            Text(COUNTIES[id].name, 47, y + 3, 20, picked ? TEAL : INK);
            Text(id == 21 ? "Clonmel / Nenagh" : LOCATIONS[COUNTIES[id].firstLocation].town, 47, y + 28, 16, MUTED);
            if (picked) DrawCircle(237, (int)y + 20, 3, TEAL);
        }
        if (!count) Text("No counties found", 47, 244, 16, MUTED);
        EndScissorMode();
        if (maxScroll > 0 && !clicked) {
            float thumb = LIST_VIEW.height * LIST_VIEW.height / (count * ROW_HEIGHT);
            DrawRectangleRounded((Rectangle){259, LIST_VIEW.y + scroll * (LIST_VIEW.height - thumb) / maxScroll, 3, thumb}, .8f, 4, LINE);
        }
        Text(TextFormat("%d counties shown", count), 40, 786, 15, INK);
        Text("Scroll to explore", 40, 808, 15, MUTED);
        Panel((Rectangle){286, 112, 458, 718}, WHITE);
        Text("Explore Ireland", 306, 131, 24, INK);
        Text("Click a county or a town marker", 306, 170, 17, MUTED);
        if (mapReady) {
            DrawIrelandMap(MAP_VIEW, countyId, hovered);
            for (int i = 0; i < LOCATION_COUNT; i++) {
                Vector2 p = MapTownPoint(MAP_VIEW, LOCATIONS[i].latitude, LOCATIONS[i].longitude);
                DrawCircleV(p, 3, WHITE); DrawCircleV(p, 1.8f, (Color){78, 113, 112, 255});
            }
            Vector2 point = MapTownPoint(MAP_VIEW, LOCATIONS[selected].latitude, LOCATIONS[selected].longitude);
            DrawCircleV(point, 12, Fade(TEAL, .16f)); DrawCircleV(point, 7, WHITE); DrawCircleV(point, 4.5f, TEAL);
            float width = MeasureTextEx(font, LOCATIONS[selected].town, 16, 0).x + 20;
            float x = point.x - width / 2;
            if (x < 300) x = 300;
            if (x + width > 730) x = 730 - width;
            Panel((Rectangle){x, point.y - 42, width, 29}, INK);
            Text(LOCATIONS[selected].town, x + 10, point.y - 37, 16, WHITE);
            Vector2 ni = MapTownPoint(MAP_VIEW, 54.8, -6.7);
            Text("NORTHERN", ni.x - 43, ni.y - 13, 14, MUTED); Text("IRELAND", ni.x - 33, ni.y + 7, 14, MUTED);
        } else Text("Map asset missing. The list still works.", 306, 310, 17, AMBER);
        Text(TextFormat("COUNTY %s", COUNTIES[countyId].name), 306, 730, 20, TEAL);
        Text(TextFormat("%s  |  %.2f N, %.2f W", LOCATIONS[selected].town, LOCATIONS[selected].latitude, -LOCATIONS[selected].longitude), 306, 759, 17, INK);
        Text("Republic of Ireland: 26 traditional counties.", 306, 789, 16, MUTED);
        Text("Northern Ireland is shown for context only.", 306, 810, 15, MUTED);
        DrawWeather(entry, selected, mouse, chartView);
        const char *status = "Latest available model data. All times: Europe/Dublin.";
        Color statusColor = TEAL;
        bool saved = entry->weatherSaved || entry->forecastSaved || entry->airSaved;
        if (offline) { status = "OFFLINE MODE: saved snapshots only, not live data. Check the forecast dates."; statusColor = AMBER; }
        else if (!sourceReady) { status = saved ? "Network client unavailable. Showing saved data; check the dates." : "Network client unavailable. No saved data for this town."; statusColor = AMBER; }
        else if (fetching) status = activeLocation == selected ? TextFormat("Checking online for %s...%s", LOCATIONS[selected].town, saved ? " Showing last saved data while checking." : "") : TextFormat("Finishing %s request; %s is next.", LOCATIONS[activeLocation].town, LOCATIONS[selected].town);
        else if (entry->failed) {
            bool weatherFailed = entry->snapshot.data.weatherError[0] || entry->snapshot.data.forecastError[0];
            bool hasWeather = entry->snapshot.data.weatherValid || entry->snapshot.data.forecastValid;
            status = weatherFailed ? TextFormat("%s Weather service unavailable; retry in %d s.", hasWeather ? "Showing last saved data." : "No saved forecast for this town.", RefreshSecondsLeft(entry->refresh.nextRefresh, now))
                : "Weather updated. Air quality unavailable; keeping the last available PM2.5.";
            statusColor = AMBER;
        }
        else if (!entry->attempted) status = wait ? TextFormat("Waiting %d s before the next request.", wait) : "Preparing the selected town...";
        else if (entry->saveFailed || !cacheWritable) { status = "Data loaded, but the snapshot could not be saved. Cache folder must be writable."; statusColor = AMBER; }
        char today[11] = "";
        IrelandCalendarDate(time(NULL), today);
        if (today[0] && entry->snapshot.data.forecastValid && !ForecastContainsDate(&entry->snapshot.data, today)) {
            status = "Warning: saved forecast does not cover today. Displayed dates are for reference, not current data.";
            statusColor = AMBER;
        }
        if (noticeUntil > now) { status = notice; statusColor = noticeError ? AMBER : TEAL; }
        DrawCircle(30, 852, 4, statusColor); Text(status, 42, 840, 17, statusColor);
        Text(TextFormat("Weather: %s  |  PM2.5: %s  |  Europe/Dublin", entry->snapshot.data.weatherValid ? entry->snapshot.data.weatherTime : "--", entry->snapshot.data.airValid ? entry->snapshot.data.airTime : "--"), 26, 870, 15, MUTED);
        Text("Open-Meteo / CAMS  |  Map: CSO, Eurostat / geoBoundaries", 990, 871, 14, MUTED);
        if (mapReady && hovered >= 0 && !searchFocused) {
            float width = MeasureTextEx(font, COUNTIES[hovered].name, 15, 0).x + 22;
            float x = fminf(mouse.x + 15, 737 - width);
            Panel((Rectangle){x, mouse.y + 16, width, 29}, INK); Text(COUNTIES[hovered].name, x + 11, mouse.y + 21, 15, WHITE);
        }
        rlPopMatrix();
        EndDrawing();
    }
    if (sourceReady) DataSourceClose();
    UnloadIrelandMap();
    if (customFont) UnloadFont(font);
    CloseWindow();
    return 0;
}
