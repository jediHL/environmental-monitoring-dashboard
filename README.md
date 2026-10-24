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
- Click **Refresh weather** or press **R** (outside the search field).
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
- Successful values are cached **in memory per town**, until the app closes.
  Re-selecting a cached town shows its values immediately.
- Auto-refresh: five minutes. First manual refresh: five seconds after the first
  successful load for that town; subsequent updates: 30-second cooldown.
- Repeated refresh presses during a request/cooldown are ignored. Selection
  is debounced and requests have a short global gap to prevent rapid clicking
  from flooding the service. Weather-service failures also impose a global
  one-minute wait, so switching counties cannot bypass the failure delay.
- Failed updates preserve available values and display a warning. Per-town
  automatic retries wait 1, 2, 4, 8, then 15 minutes; manual input cannot bypass
  that backoff. Missing/invalid fields remain unavailable, not fake zeros.

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

## Project structure

- `main.c`: Raylib UI, selection, filtering and per-town cache/refresh scheduling.
- `counties.c/.h`: county catalogue and geographical town coordinates.
- `ireland_map.c/.h`: polygon drawing, projection and point-in-polygon selection.
- `data_source.c/.h`: platform HTTPS client and asynchronous request worker.
- `forecast.c`: JSON validation, weather descriptions and safe cache merging.
- `refresh_policy.h`: cooldown, auto-refresh and retry policy.
- `tests/test_core.c`: deterministic offline tests; synthetic inputs are never
  displayed by the running dashboard.
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
