# Environmental Monitoring Dashboard

A desktop application written in C11 using Raylib. The current version displays
temperature, relative humidity and PM2.5 for **Carlow, Ireland**
(52.84083, -6.92611).

## Data source

The application requests current model estimates from Open-Meteo's weather and
air quality APIs. These are external model data, not random values and not direct
measurements from a station or a sensor in Carlow. The model grid cell can differ
slightly from the requested town coordinates.

- Weather: temperature in degrees Celsius and relative humidity in percent.
- Air quality: PM2.5 in micrograms per cubic metre (`ug/m3` in the interface).
- Each card shows the data timestamp in Europe/Dublin time. Weather and air
  quality have different update intervals.
- Requests run in a background thread. Refresh is automatic every five minutes;
  press **R** to request an update when the displayed cooldown has finished.
- The first manual refresh becomes available five seconds after the first
  successful load. Subsequent successful updates have a 30-second cooldown.
  Repeated presses during a request or cooldown are ignored and never queued.
- After a failed update, automatic retries wait 1, 2, 4, 8 and then 15 minutes
  between attempts. Manual input cannot bypass this delay. A successful update
  resets the retry schedule. The interface displays the remaining wait time.
- Missing readings display `--`. Failed refreshes keep previous readings and
  display a warning. The application never substitutes random data.
- The old simulated CO2 readings and T-key alert have been removed. There is no
  health/safety classification of the PM2.5 reading in this prototype.

## Build and run

Requirements: a C11 compiler, CMake 3.25 or newer, and internet access during the
first configuration and when fetching data. Raylib 6.0 and cJSON 1.7.19 are
downloaded automatically into the build directory.

### macOS / CLion

1. Open the repository as a CMake project and reload CMake.
2. Select the `dashboard` target and run it.

HTTPS uses macOS's system libcurl. Raylib installed separately through Homebrew
is not needed by this build.

### Windows / Visual Studio

Install the **Desktop development with C++** workload with **C++ CMake tools for
Windows** and the Windows SDK. The application's source files are still C.

1. Clone the repository and choose **File > Open > Folder** in Visual Studio.
2. Wait for CMake configuration, then select and run `dashboard`.

HTTPS uses Windows WinHTTP, so installing curl separately is not required.
The Windows code path requires verification on Windows.

### Command line

```sh
cmake -S . -B build
cmake --build build --config Debug
```

On macOS, run `./build/dashboard`. With a Visual Studio multi-configuration
generator, run `build/Debug/dashboard.exe`.

Pass `--check-data` to fetch and print the same API data without opening a window.
It returns a nonzero exit code if either request or its data validation fails.

## Attribution

- Interface font: [Noto Sans](https://fonts.google.com/noto/specimen/Noto+Sans),
  bundled in `assets/fonts` under the SIL Open Font License (see `OFL.txt`).
  CMake copies the assets next to the executable during the build. Keep the
  `assets` folder beside the executable when sharing a built application.
- Weather: [Open-Meteo](https://open-meteo.com/),
  [weather API documentation](https://open-meteo.com/en/docs).
- Air quality: [Open-Meteo air quality API](https://open-meteo.com/en/docs/air-quality-api),
  using CAMS / Copernicus data.
- Data licence: [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).
- Town coordinates: [GeoNames](https://www.geonames.org/2965768/carlow.html).

## Next steps

- Historical data and line charts.
- Additional locations.
