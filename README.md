# Environmental Monitoring Dashboard — Ireland Weather Atlas

A desktop prototype written entirely in **C11 + Raylib**. Explore the Republic
of Ireland's **26 traditional counties**, select a county town on an interactive
map, and view current model weather, PM2.5 and a **7-day forecast**.

## Using the application

- Select a county in the left-hand list, or click its area on the map.
- The selected county is highlighted and its town is marked and labelled.
- Search by county or town, e.g. `Kerry`, `Tralee`, `Dublin` or `Nenagh`.
  Scroll the list to see the remaining counties.
- Tipperary has buttons for **Clonmel / Nenagh**; both have separate forecasts.
- The right-hand panel shows temperature, feels-like temperature, weather
  conditions, humidity, wind and PM2.5. Forecast rows show low/high temperature
  and rain probability. Hover a day for rain amount and maximum wind.
- Current-weather sun/moon icons follow Open-Meteo's `is_day` value for that
  observation: a clear night uses a moon, not a sun. This flag is saved with the
  weather snapshot. Older saved files without it use a neutral symbol until
  refreshed; the computer's present clock is not used to guess old data.
  Daily-row icons summarise whole days, not the current hour.
- Click **Refresh weather** or press **R** (outside the search field).
- Switch between **Temperature chart** (daily minimum/maximum lines) and
  **Forecast table**. Hover a chart point for exact temperatures and the date.
  The graph displays forecast data, not historical sensor readings.
- Click **Export CSV** to save all seven forecast days for the selected town.
  Files go directly to **`exports` in the project root**, beside `main.c`.
  Click **Open exports** to open that folder in Finder/File Explorer.
  A message confirms the filename. No spreadsheet software or additional
  library is needed by the application.
- Resize the window: the complete layout scales to fit, including on Retina.

The catalogue uses county towns / administrative centres, **not county-wide
averages**. Waterford city represents the present city/county headquarters;
Dungarvan is the former county town. Northern Ireland is drawn in grey for
geographical context but is not included in the selectable 26-county catalogue.
Boundaries are approximate historical outlines, not current local-authority
boundaries. See `assets/maps/ATTRIBUTION.md` for details and sources.

## Data and refresh behaviour

Weather and air quality are fetched from **Open-Meteo** over HTTPS. These are
model estimates/forecasts, not random numbers and not direct measurements from
a sensor in the selected town. All displayed data times use **Europe/Dublin**;
weather and air-quality timestamps can differ.

- Weather: Celsius, relative humidity %, wind km/h, WMO weather conditions.
- Forecast: seven daily low/high temperatures, maximum rain probability,
  total precipitation mm, maximum wind km/h and daily weather conditions.
- Air quality: PM2.5 in micrograms per cubic metre (`ug/m3`). No health/safety
  classification is made by this prototype.
- Only the selected town is requested; there is no 26-town download at startup.
- Requests run in one background worker. The interface remains responsive.
  If selection changes while a request is running, that response updates only
  its original town's cache; the latest selection is fetched next. Intermediate
  selections are not queued. Uncached towns show `--`, never another town's data.
- Successful values are cached **per town in memory and in local JSON files**.
  Re-selecting a cached town shows its values immediately; saved snapshots are
  restored at startup, before an internet request completes. These are the last
  available values, not a historical time series.
- Auto-refresh: five minutes. First manual refresh: five seconds after the first
  successful load for that town; subsequent updates: 30-second cooldown.
- Repeated refresh presses during a request/cooldown are ignored. Selection
  is debounced and requests have a short global gap to prevent rapid clicking
  from flooding the service. Weather-service failures also impose a global
  one-minute wait, so switching counties cannot bypass the failure delay.
- Failed updates preserve available values and display a warning. Per-town
  automatic retries wait 1, 2, 4, 8, then 15 minutes; manual input cannot bypass
  that backoff. Missing/invalid fields remain unavailable, not fake zeros.

## Saved data, offline use and CSV files

The application creates **`exports` in the project root**, beside `main.c`
and `CMakeLists.txt`. CSV files are directly inside that one folder;
**Open exports** opens it in Finder/File Explorer. CMake supplies the checkout's
location, so CLion's working/build directory does not change where files go.
If a standalone executable is moved to a computer without that checkout, it
uses an `exports` folder beside the executable instead.

- `user_data/cache/town_XX.json` in the project root: the last valid snapshot for
  each visited town. This is internal application data, separate from CSV exports.
  Current weather, the forecast and air quality retain **separate UTC fetch
  times**. A partially failed refresh never relabels an old section as fresh.
  Keeping these files outside the build directory means rebuilding the project
  does not discard saved forecasts. Do not delete `user_data/cache` if you want
  to keep offline data.
- Saved sections are labelled **SAVED WEATHER**, **SAVED FORECAST** or
  **PM2.5 / SAVED**. Always check their dates: an old saved forecast is not
  a current forecast. Downloaded sections instead show their update time.
- **Automatic fallback is on during a normal run.** Startup immediately displays
  the latest saved values for the selected town, while checking online in a
  background worker. If the network or API is unavailable, those values remain
  visible and a clear status explains that saved data are being used. The retry
  policy automatically checks again later. No argument or manual mode switch is
  needed for normal offline use. Unvisited towns remain empty: the application
  cannot invent a current forecast without a previous download.
- A saved forecast can be the **latest available file**, but that does not make
  it freshly updated or accurate now. The displayed fetch timestamp is retained.
  The interface checks whether the saved dates cover today's Irish calendar
  date. If not, it warns that the forecast is for reference, not current data;
  fully expired date ranges are labelled **expired**. Dublin's modern seasonal
  UTC offset is applied without changing the process-wide timezone (rules:
  https://data.iana.org/time-zones/tzdb/europe).
- Only to explicitly demonstrate offline use without changing network settings, run
  `dashboard --offline` (CLion: add `--offline` to the run configuration's
  program arguments). This mode makes **no API requests**. Restart normally to
  re-enable live requests.
- Cache files are versioned, bounded in size, validated against the town ID,
  name and coordinates, and checked with the same weather validator used for
  API data. Invalid files/sections are ignored. Valid replacement files are
  written before replacing the old cache.
- `exports/forecast_Town_UTC-time_sequence.csv`: a new file per export,
  never overwriting an existing file. CSV contains the county/town, coordinates,
  dates, min/max Celsius, rain probability, rain amount, wind, conditions, source,
  timezone and forecast-fetch time. `data_status` distinguishes `saved_snapshot`
  from `downloaded_this_session`; neither means a direct sensor measurement.
- CSV columns have unit-labelled headers and quoted text fields. They can be
  opened in Excel/Numbers or imported with comma separation.
- Old `user_data/exports` CSV files beside the executable are copied into the
  project's `exports` folder when missing; originals are retained and existing exports are
  never overwritten. Old `user_data/cache` snapshots are also imported, choosing
  the newer valid section independently for weather, forecast and air quality.
- The project's export and cache folders must be writable for saving. A failure is reported;
  the dashboard still displays available in-memory/legacy saved data.
- CSV exports are reports for the user; JSON cache files are read automatically
  by the application. They serve different purposes. Neither should be committed
  to Git. Both `exports` and `user_data` are ignored.

To check automatic offline fallback in CLion:

1. Run normally with internet and select a town. Wait until its weather and
   seven-day forecast have loaded successfully.
2. Stop the application, turn Wi-Fi off, then run it again and select that town.
3. Its saved readings/forecast should appear with **SAVED** labels and their
   original timestamps. A network attempt may take several seconds before the
   connection warning appears. Towns never downloaded will have no saved data.
4. Stop, turn Wi-Fi back on and run normally again for live updates.

Alternatively, use `--offline` in the run configuration's program arguments to
test saved files without disconnecting Wi-Fi. Remove that argument afterwards.
CSV files are not used to restore the dashboard; the JSON snapshots are.

## Build and run

Requirements: a C11 compiler, CMake 3.25+, and internet for the first CMake
configuration and live API requests. Raylib 6.0 and cJSON 1.7.19 are downloaded
automatically into the build directory. No API key, GIS software or Python is
required. A separate Homebrew Raylib installation is not used by this build.

### macOS / CLion

1. Open this repository in CLion as a CMake project.
2. If prompted after pulling changes, choose **Reload CMake Project**.
3. Select the **dashboard** target, then **Run**.

HTTPS uses system libcurl; requests use a background pthread.

### Windows / Visual Studio

Install **Desktop development with C++**, including **C++ CMake tools for
Windows** and a Windows SDK. This workload supplies the compiler; the project's
application code is still C, not C++.

1. Clone the repository and choose **File > Open > Folder** in Visual Studio.
2. Wait for CMake configuration.
3. Select **dashboard.exe** as the startup target and run it.

HTTPS uses built-in WinHTTP; no separate curl installation is required. The
Windows path has not yet been runtime-tested on Windows.

### Command line

```sh
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

Run `./build/dashboard` on macOS/Linux or `build/Debug/dashboard.exe` with a
Visual Studio multi-configuration build. Linux also needs the usual Raylib
desktop development packages and libcurl headers.

Headless diagnostics (no graphical window):

```sh
./build/dashboard --check-data Dublin
./build/dashboard --check-data "Carrick-on-Shannon"
./build/dashboard --check-data Nenagh
./build/dashboard --check-map
```

`--check-data` defaults to Carlow and returns nonzero if weather, forecast or
air quality fails validation. Unknown towns return an error without a request.
`--check-map` checks interior hit detection for all 26 counties and excludes
ocean/Northern Ireland. CTest also checks the catalogue, forecast parsing,
missing/null data, per-location cache isolation and refresh cooldown/backoff.
Storage tests also cover round trips, independent timestamps, invalid/truncated
or oversized files, wrong-town files, partial snapshots, negative/zero values,
CSV rows/metadata, write failures and refusing to overwrite an export.

For the graphical application using saved files only: `./build/dashboard --offline`.

## Project structure

- `main.c`: Raylib UI, selection, filtering and per-town cache/refresh scheduling.
- `counties.c/.h`: county catalogue and geographical town coordinates.
- `ireland_map.c/.h`: polygon drawing, projection and point-in-polygon selection.
- `data_source.c/.h`: platform HTTPS client and asynchronous request worker.
- `forecast.c`: JSON validation, weather descriptions and safe cache merging.
- `weather_storage.c/.h`: validated persistent snapshots, UTC fetch metadata
  and CSV export with exclusive file creation.
- `app_support.c/.h`: project export-folder paths/opening, non-overwriting file
  copying, Irish calendar dates and forecast-date coverage checks.
- `refresh_policy.h`: cooldown, auto-refresh and retry policy.
- `tests/test_core.c`: deterministic offline tests; synthetic inputs are never
  displayed by the running dashboard.
- `tests/test_storage.c`: offline file-storage and CSV regression tests.
- `tests/test_app_support.c`: project paths, copying and Irish-date/coverage tests.
- `assets/maps`: local geometry and source/licence notices.

## Attribution

- [Open-Meteo weather API](https://open-meteo.com/en/docs) and
  [air-quality API](https://open-meteo.com/en/docs/air-quality-api), with
  CAMS / Copernicus air-quality data, [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).
- Town coordinates: [Open-Meteo Geocoding / GeoNames](https://open-meteo.com/en/docs/geocoding-api), CC BY 4.0.
- County geometry: CSO Census 2011 via Click That 'Hood; context outline:
  Eurostat via geoBoundaries. Full provenance and changes in
  `assets/maps/ATTRIBUTION.md`; upstream MIT notice is retained alongside it.
- Font: [Noto Sans](https://fonts.google.com/noto/specimen/Noto+Sans), SIL Open
  Font License, retained as `assets/fonts/OFL.txt`.

CMake copies **all assets beside the executable**. When sharing a built app,
keep its `assets` folder beside the executable; do not share only the binary.
