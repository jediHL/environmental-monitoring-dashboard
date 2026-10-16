#include "raylib.h"
#include "data_source.h"
#include "refresh_policy.h"

#include <stdio.h>
#include <string.h>

static Font uiFont;

static void DrawUiText(const char *text, int x, int y, int size, Color color)
{
    DrawTextEx(uiFont, text, (Vector2){(float)x, (float)y}, (float)size, 0.0f, color);
}

static void DrawSensorCard(
    Rectangle area,
    const char *title,
    const char *value,
    const char *time,
    Color accent)
{
    DrawRectangleRounded(area, 0.12f, 8, WHITE);

    DrawRectangle(
        (int)area.x + 20,
        (int)area.y + 25,
        5, 25,
        accent
    );

    DrawUiText(
        title,
        (int)area.x + 40,
        (int)area.y + 27,
        20, GRAY
    );

    DrawUiText(
        value,
        (int)area.x + 25,
        (int)area.y + 75,
        32, accent
    );

    DrawUiText(time, (int)area.x + 25, (int)area.y + 133, 15, GRAY);
}

static int CheckData(void)
{
    if (!DataSourceInit()) return 1;
    EnvironmentData data;
    FetchEnvironmentData(&data);
    if (data.weatherValid) {
        printf("Carlow weather (%s, Europe/Dublin): %.1f C, %.1f %%\n",
               data.weatherTime, data.temperature, data.humidity);
    } else {
        fprintf(stderr, "Weather: %s\n", data.weatherError);
    }
    if (data.airValid) {
        printf("Carlow PM2.5 (%s, Europe/Dublin): %.1f ug/m3\n",
               data.airTime, data.pm25);
    } else {
        fprintf(stderr, "Air quality: %s\n", data.airError);
    }
    DataSourceClose();
    return data.weatherValid && data.airValid ? 0 : 1;
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--check-data") == 0) return CheckData();

    SetConfigFlags(FLAG_WINDOW_HIGHDPI | FLAG_MSAA_4X_HINT);
    InitWindow(1100, 650, "Carlow | Environmental Monitoring Dashboard");
    SetTargetFPS(60);

    char fontPath[4096];
    snprintf(fontPath, sizeof(fontPath), "%s/assets/fonts/NotoSans.ttf",
             GetApplicationDirectory());
    uiFont = LoadFontEx(fontPath, 64, NULL, 0);
    bool customFontLoaded = uiFont.texture.id != GetFontDefault().texture.id;
    if (customFontLoaded) {
        GenTextureMipmaps(&uiFont.texture);
        SetTextureFilter(uiFont.texture, TEXTURE_FILTER_TRILINEAR);
    } else {
        TraceLog(LOG_WARNING, "UI font not found; using the default font");
    }

    EnvironmentData data = {0};
    bool sourceReady = DataSourceInit();
    bool fetching = sourceReady && StartDataRefresh();
    bool weatherFailed = false;
    bool airFailed = false;
    bool refreshFailed = sourceReady && !fetching;
    RefreshPolicy refresh = RefreshPolicyInit(GetTime());
    if (sourceReady) {
        RefreshPolicyStarted(&refresh, GetTime());
        if (!fetching) RefreshPolicyFinished(&refresh, GetTime(), false);
    }

    const Color background = {235, 240, 246, 255};
    const Color heading = {30, 45, 65, 255};
    const Color green = {35, 135, 90, 255};

    while (!WindowShouldClose())
    {
        EnvironmentData incoming;
        if (fetching && PollDataRefresh(&incoming)) {
            fetching = false;
            weatherFailed = !incoming.weatherValid;
            airFailed = !incoming.airValid;
            if (incoming.weatherValid) {
                data.temperature = incoming.temperature;
                data.humidity = incoming.humidity;
                data.weatherValid = true;
                snprintf(data.weatherTime, sizeof(data.weatherTime),
                         "%s", incoming.weatherTime);
            }
            if (incoming.airValid) {
                data.pm25 = incoming.pm25;
                data.airValid = true;
                snprintf(data.airTime, sizeof(data.airTime), "%s", incoming.airTime);
            }
            if (weatherFailed) TraceLog(LOG_WARNING, "Weather: %s", incoming.weatherError);
            if (airFailed) TraceLog(LOG_WARNING, "Air quality: %s", incoming.airError);
            RefreshPolicyFinished(&refresh, GetTime(),
                                  !weatherFailed && !airFailed);
        }

        if (sourceReady && RefreshPolicyDue(&refresh, GetTime(),
                                            IsKeyPressed(KEY_R), fetching)) {
            fetching = StartDataRefresh();
            refreshFailed = !fetching;
            RefreshPolicyStarted(&refresh, GetTime());
            if (!fetching) RefreshPolicyFinished(&refresh, GetTime(), false);
        }

        bool failed = !sourceReady || refreshFailed || weatherFailed || airFailed;
        Color statusColor = fetching ? BLUE : (failed ? ORANGE : green);
        const char *status = "Latest available model data loaded";
        if (fetching) status = "Fetching Carlow data...";
        else if (!sourceReady) status = "Cannot initialise network client";
        else if (refreshFailed) status = "Cannot start refresh. Automatic retry scheduled";
        else if (weatherFailed && airFailed) status = "Data update failed. Automatic retry scheduled";
        else if (weatherFailed) status = "Weather update failed. Automatic retry scheduled";
        else if (airFailed) status = "Air quality update failed. Automatic retry scheduled";

        BeginDrawing();
        ClearBackground(background);

        DrawUiText(
            "Environmental Monitoring Dashboard",
            40, 35, 30, heading
        );

        DrawUiText(
            "Carlow, Ireland | Open-Meteo model data",
            40, 80, 20, GRAY
        );

        DrawSensorCard(
            (Rectangle){40, 140, 320, 180},
            "TEMPERATURE",
            data.weatherValid ? TextFormat("%.1f C", data.temperature) : "--",
            data.weatherValid ? data.weatherTime : "Awaiting data",
            ORANGE
        );

        DrawSensorCard(
            (Rectangle){390, 140, 320, 180},
            "HUMIDITY",
            data.weatherValid ? TextFormat("%.1f %%", data.humidity) : "--",
            data.weatherValid ? data.weatherTime : "Awaiting data",
            BLUE
        );

        DrawSensorCard(
            (Rectangle){740, 140, 320, 180},
            "PM2.5",
            data.airValid ? TextFormat("%.1f ug/m3", data.pm25) : "--",
            data.airValid ? data.airTime : "Awaiting data",
            green
        );

        DrawRectangleRounded(
            (Rectangle){40, 350, 1020, 85},
            0.12f, 8, WHITE
        );

        DrawCircle(70, 392, 9, statusColor);

        DrawUiText(
            status,
            95, 381, 19, statusColor
        );

        DrawUiText(
            "Data times above use Europe/Dublin. Weather and air updates differ.",
            40, 470, 17, heading
        );

        int manualWait = RefreshSecondsLeft(refresh.manualAllowedAt, GetTime());
        int autoWait = RefreshSecondsLeft(refresh.nextRefresh, GetTime());
        if (!sourceReady) {
            DrawUiText("Restart the application to initialise the network client.",
                       40, 510, 20, GRAY);
        } else if (fetching) {
            DrawUiText("Updating Carlow data. Please wait...", 40, 510, 20, BLUE);
        } else if (manualWait > 0) {
            DrawUiText(TextFormat("Next %s: %d s | Manual refresh available in %d s",
                           failed ? "retry" : "update", autoWait, manualWait),
                       40, 510, 20, GRAY);
        } else {
            DrawUiText(TextFormat("Next update: %d s | Press R to refresh", autoWait),
                       40, 510, 20, GRAY);
        }

        DrawUiText(
            failed && (data.weatherValid || data.airValid)
                ? "Showing last available values until the next successful update."
                : "Model estimates for Carlow, not direct station measurements.",
            40, 550, 17, failed ? ORANGE : GRAY
        );

        DrawUiText("Source: Open-Meteo | Air quality: CAMS / Copernicus (CC BY 4.0)",
                 40, 590, 15, GRAY);

        EndDrawing();
    }

    if (customFontLoaded) UnloadFont(uiFont);
    CloseWindow();
    if (sourceReady) DataSourceClose();
    return 0;
}
