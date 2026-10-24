#include "ireland_map.h"
#include "counties.h"
#include "cJSON.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>

#define SHAPE_COUNT (COUNTY_COUNT + 1)
#define LONGITUDE_SCALE 0.5948228f

typedef struct { Vector2 *points; int count; } Ring;
typedef struct { Ring *rings; int count; } Polygon;
typedef struct {
    int id;
    Polygon *polygons;
    int polygonCount;
    Vector2 *vertices;
    int vertexCount;
    Vector2 interior;
} Shape;

static Shape shapes[SHAPE_COUNT];
static int shapeCount;
static Rectangle worldBounds;

static Vector2 Project(double longitude, double latitude)
{
    return (Vector2){(float)longitude * LONGITUDE_SCALE, -(float)latitude};
}

static Vector2 ToScreen(Rectangle view, Vector2 point)
{
    float scale = fminf(view.width / worldBounds.width, view.height / worldBounds.height);
    float left = view.x + (view.width - worldBounds.width * scale) / 2;
    float top = view.y + (view.height - worldBounds.height * scale) / 2;
    return (Vector2){left + (point.x - worldBounds.x) * scale,
                     top + (point.y - worldBounds.y) * scale};
}

Vector2 MapTownPoint(Rectangle viewport, double latitude, double longitude)
{
    if (!shapeCount) return (Vector2){-1, -1};
    return ToScreen(viewport, Project(longitude, latitude));
}

static bool ReadPoint(const cJSON *array, Vector2 *point)
{
    const cJSON *x = cJSON_GetArrayItem(array, 0);
    const cJSON *y = cJSON_GetArrayItem(array, 1);
    if (!cJSON_IsNumber(x) || !cJSON_IsNumber(y) ||
        !isfinite(x->valuedouble) || !isfinite(y->valuedouble) ||
        x->valuedouble < -12 || x->valuedouble > -5 ||
        y->valuedouble < 50 || y->valuedouble > 57) return false;
    *point = Project(x->valuedouble, y->valuedouble);
    return true;
}

void UnloadIrelandMap(void)
{
    for (int i = 0; i < SHAPE_COUNT; i++) {
        Shape *s = &shapes[i];
        for (int p = 0; s->polygons && p < s->polygonCount; p++) {
            for (int r = 0; s->polygons[p].rings && r < s->polygons[p].count; r++)
                free(s->polygons[p].rings[r].points);
            free(s->polygons[p].rings);
        }
        free(s->polygons);
        free(s->vertices);
        *s = (Shape){0};
    }
    shapeCount = 0;
}

bool LoadIrelandMap(const char *path)
{
    UnloadIrelandMap();
    char *text = LoadFileText(path);
    if (!text) return false;
    cJSON *root = cJSON_Parse(text);
    UnloadFileText(text);
    const cJSON *features = cJSON_GetObjectItemCaseSensitive(root, "counties");
    int count = cJSON_GetArraySize(features);
    bool valid = cJSON_IsArray(features) && count == SHAPE_COUNT;
    bool seen[COUNTY_COUNT] = {0};
    float minX = FLT_MAX, minY = FLT_MAX, maxX = -FLT_MAX, maxY = -FLT_MAX;
    for (int i = 0; i < count && valid; i++) {
        Shape *s = &shapes[i];
        const cJSON *feature = cJSON_GetArrayItem(features, i);
        const cJSON *id = cJSON_GetObjectItemCaseSensitive(feature, "id");
        const cJSON *polygons = cJSON_GetObjectItemCaseSensitive(feature, "polygons");
        const cJSON *triangles = cJSON_GetObjectItemCaseSensitive(feature, "triangles");
        if (!cJSON_IsNumber(id) || id->valueint < -1 || id->valueint >= COUNTY_COUNT ||
            !cJSON_IsArray(polygons) || !cJSON_IsArray(triangles)) { valid = false; break; }
        s->id = id->valueint;
        if (s->id >= 0) {
            if (seen[s->id]) { valid = false; break; }
            seen[s->id] = true;
            if (!ReadPoint(cJSON_GetObjectItemCaseSensitive(feature, "interior"), &s->interior)) {
                valid = false; break;
            }
        }
        s->polygonCount = cJSON_GetArraySize(polygons);
        if (s->polygonCount < 1 || s->polygonCount > 500) { valid = false; break; }
        s->polygons = calloc((size_t)s->polygonCount, sizeof(Polygon));
        if (!s->polygons) { s->polygonCount = 0; valid = false; break; }
        for (int p = 0; p < s->polygonCount && valid; p++) {
            Polygon *poly = &s->polygons[p];
            const cJSON *rings = cJSON_GetArrayItem(polygons, p);
            poly->count = cJSON_GetArraySize(rings);
            if (poly->count < 1 || poly->count > 100) { valid = false; break; }
            poly->rings = calloc((size_t)poly->count, sizeof(Ring));
            if (!poly->rings) { poly->count = 0; valid = false; break; }
            for (int r = 0; r < poly->count && valid; r++) {
                Ring *ring = &poly->rings[r];
                const cJSON *points = cJSON_GetArrayItem(rings, r);
                ring->count = cJSON_GetArraySize(points);
                if (ring->count < 4 || ring->count > 50000) { valid = false; break; }
                ring->points = calloc((size_t)ring->count, sizeof(Vector2));
                if (!ring->points) { valid = false; break; }
                for (int n = 0; n < ring->count; n++) {
                    if (!ReadPoint(cJSON_GetArrayItem(points, n), &ring->points[n])) { valid = false; break; }
                    minX = fminf(minX, ring->points[n].x); maxX = fmaxf(maxX, ring->points[n].x);
                    minY = fminf(minY, ring->points[n].y); maxY = fmaxf(maxY, ring->points[n].y);
                }
            }
        }
        int triangleCount = cJSON_GetArraySize(triangles);
        if (triangleCount < 1 || triangleCount > 50000) { valid = false; break; }
        s->vertexCount = triangleCount * 3;
        s->vertices = calloc((size_t)s->vertexCount, sizeof(Vector2));
        if (!s->vertices) { valid = false; break; }
        for (int t = 0; t < triangleCount && valid; t++) {
            const cJSON *triangle = cJSON_GetArrayItem(triangles, t);
            if (cJSON_GetArraySize(triangle) != 6) { valid = false; break; }
            for (int v = 0; v < 3; v++) {
                const cJSON *x = cJSON_GetArrayItem(triangle, v * 2);
                const cJSON *y = cJSON_GetArrayItem(triangle, v * 2 + 1);
                if (!cJSON_IsNumber(x) || !cJSON_IsNumber(y) ||
                    !isfinite(x->valuedouble) || !isfinite(y->valuedouble) ||
                    x->valuedouble < -12 || x->valuedouble > -5 ||
                    y->valuedouble < 50 || y->valuedouble > 57) { valid = false; break; }
                s->vertices[t * 3 + v] = Project(x->valuedouble, y->valuedouble);
            }
        }
    }
    for (int i = 0; i < COUNTY_COUNT; i++) valid = valid && seen[i];
    cJSON_Delete(root);
    if (!valid || maxX <= minX || maxY <= minY) { UnloadIrelandMap(); return false; }
    shapeCount = count;
    worldBounds = (Rectangle){minX, minY, maxX - minX, maxY - minY};
    return true;
}

static bool InsideRing(Vector2 point, const Ring *ring)
{
    bool inside = false;
    for (int i = 0, j = ring->count - 1; i < ring->count; j = i++) {
        Vector2 a = ring->points[i], b = ring->points[j];
        if ((a.y > point.y) != (b.y > point.y) &&
            point.x < (b.x - a.x) * (point.y - a.y) / (b.y - a.y) + a.x) inside = !inside;
    }
    return inside;
}

int PickMapCounty(Rectangle viewport, Vector2 screenPoint)
{
    if (!shapeCount || !CheckCollisionPointRec(screenPoint, viewport)) return -1;
    float scale = fminf(viewport.width / worldBounds.width, viewport.height / worldBounds.height);
    Vector2 origin = ToScreen(viewport, (Vector2){worldBounds.x, worldBounds.y});
    Vector2 point = {worldBounds.x + (screenPoint.x - origin.x) / scale,
                     worldBounds.y + (screenPoint.y - origin.y) / scale};
    for (int i = 0; i < shapeCount; i++) {
        const Shape *s = &shapes[i];
        if (s->id < 0) continue;
        for (int p = 0; p < s->polygonCount; p++) {
            const Polygon *poly = &s->polygons[p];
            if (!InsideRing(point, &poly->rings[0])) continue;
            bool hole = false;
            for (int r = 1; r < poly->count; r++) hole = hole || InsideRing(point, &poly->rings[r]);
            if (!hole) return s->id;
        }
    }
    return -1;
}

Vector2 MapCountyInterior(Rectangle viewport, int countyId)
{
    for (int i = 0; i < shapeCount; i++) {
        if (shapes[i].id == countyId) return ToScreen(viewport, shapes[i].interior);
    }
    return (Vector2){-1, -1};
}

static void Outline(const Shape *s, Rectangle viewport, Color color, float thickness)
{
    for (int p = 0; p < s->polygonCount; p++) {
        for (int r = 0; r < s->polygons[p].count; r++) {
            const Ring *ring = &s->polygons[p].rings[r];
            for (int n = 1; n < ring->count; n++) {
                DrawLineEx(ToScreen(viewport, ring->points[n - 1]),
                           ToScreen(viewport, ring->points[n]), thickness, color);
            }
        }
    }
}

void DrawIrelandMap(Rectangle viewport, int selectedCounty, int hoveredCounty)
{
    for (int i = 0; i < shapeCount; i++) {
        const Shape *s = &shapes[i];
        Color fill = s->id < 0 ? (Color){226, 232, 237, 255} : (Color){214, 231, 222, 255};
        if (s->id >= 0 && s->id == hoveredCounty) fill = (Color){178, 217, 205, 255};
        if (s->id == selectedCounty) fill = (Color){39, 115, 111, 255};
        for (int v = 0; v < s->vertexCount; v += 3) {
            Vector2 a = ToScreen(viewport, s->vertices[v]);
            Vector2 b = ToScreen(viewport, s->vertices[v + 1]);
            Vector2 c = ToScreen(viewport, s->vertices[v + 2]);
            float cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
            if (cross > 0) DrawTriangle(a, c, b, fill);
            else DrawTriangle(a, b, c, fill);
        }
    }
    for (int i = 0; i < shapeCount; i++) {
        Outline(&shapes[i], viewport, (Color){255, 255, 255, 255}, 1.1f);
    }
    for (int i = 0; i < shapeCount; i++) {
        if (shapes[i].id == selectedCounty) Outline(&shapes[i], viewport, (Color){24, 89, 87, 255}, 2.0f);
    }
}
