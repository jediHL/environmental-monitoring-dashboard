# Ireland map data

The map is a simplified teaching/visualisation asset, not a survey or navigation
map and not a map of the current 31 local-authority areas.

## Republic of Ireland

Derived from the county/city boundary GeoJSON in **Click That 'Hood**:

- https://github.com/codeforgermany/click_that_hood/blob/main/public/data/ireland-counties.geojson
- Metadata: https://github.com/codeforgermany/click_that_hood/blob/main/public/data/ireland-counties.metadata.json
- Original source named in that metadata: Central Statistics Office, Census 2011
  boundary files, http://www.cso.ie/en/census/census2011boundaryfiles/
- Upstream repository licence: MIT; retained in `click-that-hood-LICENSE.txt`.
- CSO attribution/copyright information: https://www.cso.ie/en/aboutus/whoweare/copyrightpolicy/

Changes made for this project on 24 October 2026: city/county polygons were
merged into 26 traditional counties; Dublin's four authority areas and
Tipperary's north/south areas were merged. Geometry was simplified by 0.003
degrees, tiny offshore polygons omitted, and triangulated for Raylib drawing.
Therefore the rendered boundaries are approximate historical outlines.

## Northern Ireland context outline

Eurostat boundary, distributed by **geoBoundaries gbOpen GBR ADM1**, CC BY 4.0:

- https://www.geoboundaries.org/api/current/gbOpen/GBR/ADM1/
- https://github.com/wmgeolab/geoBoundaries/blob/9469f09/releaseData/gbOpen/GBR/ADM1/geoBoundaries-GBR-ADM1_simplified.geojson
- Licence: https://creativecommons.org/licenses/by/4.0/

The Northern Ireland feature was extracted and simplified for context only; it
is not selectable. The weather catalogue covers the Republic of Ireland.

## Town coordinates and county-town conventions

Town points are from Open-Meteo Geocoding / GeoNames (CC BY 4.0), retrieved
24 October 2026: https://open-meteo.com/en/docs/geocoding-api

The catalogue uses county towns / administrative centres as representative
forecast locations. They are not country-style capitals, weather stations, or
county-wide averages. Tipperary offers both Clonmel and Nenagh:
https://www.tipperarycoco.ie/municipal-facilities/civic-offices-nenagh

Waterford city is used as the current corporate headquarters of the combined
city/county council; Dungarvan was the former county town:
https://waterfordcouncil.ie/about-the-council/
https://consult.waterfordcouncil.ie/en/consultation/waterford-city-county-development-plan-2022-%E2%80%93-2028/chapter/chapter-1-waterford-and-development-plan

## Runtime format

`ireland_counties.json` stores longitude/latitude polygon rings, precomputed
triangles and an interior test point per county. IDs match `COUNTIES` in
`counties.c`; ID -1 is the Northern Ireland outline. Inner rings are treated as
holes for hit testing. No Python, GIS package, map API key or map-tile service is
needed to run or build the application.
