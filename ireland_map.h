#ifndef IRELAND_MAP_H
#define IRELAND_MAP_H

#include "raylib.h"

bool LoadIrelandMap(const char *path);
void UnloadIrelandMap(void);
void DrawIrelandMap(Rectangle viewport, int selectedCounty, int hoveredCounty);
int PickMapCounty(Rectangle viewport, Vector2 screenPoint);
Vector2 MapTownPoint(Rectangle viewport, double latitude, double longitude);
Vector2 MapCountyInterior(Rectangle viewport, int countyId);

#endif
