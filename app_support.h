#ifndef APP_SUPPORT_H
#define APP_SUPPORT_H

#include "data_source.h"
#include <stddef.h>
#include <time.h>

bool BuildExportFolderPath(const char *projectDirectory, char *path, size_t capacity);
bool OpenExportFolder(const char *path);
bool CopyFileNoReplace(const char *source, const char *destination);
bool IrelandCalendarDate(time_t now, char date[11]);
bool ForecastContainsDate(const EnvironmentData *data, const char *date);

#endif
