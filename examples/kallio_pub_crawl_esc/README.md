# Kallio Pub Crawl Demo

This ESC demo renders a Kallio + Vaasankatu pub-crawl route in the terminal.

The GA objective is:
- maximize promille at the end of a fixed time budget

The score model still includes:
- walking distance from a map-derived GPS start point
- venue opening hours by weekday
- waiting / closed-venue penalties
- elevation-based fatigue, where uphill becomes more expensive later
- alcohol load, using `promille` and `drinks` parameters in the cost function

Build and run through the helper script:

```sh
./examples/kallio_pub_crawl_esc/run_demo.sh Thu 18:00 budget=180
```

Choose a fixed starting venue:

```sh
./examples/kallio_pub_crawl_esc/run_demo.sh Thu 18:00 Siltanen budget=180
```

Compact display with explicit alcohol settings:

```sh
./examples/kallio_pub_crawl_esc/run_demo.sh Sat 18:00 budget=240 start_lat=60.17875 start_lon=24.95062 stable compact promille=0.25 drinks=2 avg=45
```

Lower-tolerance harder crawl:

```sh
./examples/kallio_pub_crawl_esc/run_demo.sh Fri 19:00 Roskapankki budget=150 40 compact promille=0.20 drinks=1 avg=45
```

Arguments:
- `Mon|Tue|Wed|Thu|Fri|Sat|Sun`
- `HH:MM`
- optional start place name
- optional `budget=MIN` / `limit=MIN` / `time_limit=MIN`
- optional `start_lat=...`
- optional `start_lon=...`
- optional `start_elev=M`
- optional `tolerance` or `tolerance=0..100`
- optional `compact`
- optional `stable`
- optional `promille=0.00` or `start_promille=0.00`
- optional `drinks=N`
- optional `avg=MIN` or `stay=MIN`

Notes:
- Included crawl bars are `Rytmi`, `Kallion_Savel`, `Pub_Sirdie`, `Siltanen`, `Kuudes_Linja`, `Gate_H11`, `Musta_Kissa`, `Helsing_Bar`, `Roskapankki`, `Hilpea_Hauki`, `Solmu_Pub`, `Kustaa_Vaasa`, `Kalliohovi`, `Pub_Heinahattu`, and `Bar_Molotow`.
- If the start place is omitted, the demo chooses the first bar from the GPS start point and time budget.
- The header shows the current best score, walked meters, and route preview.
- `compact` mode shows only `path`, `schedule`, `alcohol plan`, and live `alcohol now`.
- `stable` starts the on-screen walker from the beginning of the route.
- `promille` sets the starting alcohol level.
- `drinks` sets the same drinks-per-bar value used at each venue.
- `avg` / `stay` sets the average minutes spent in one venue for the GA route calculation.
- Default start GPS is Hakaniemi metro: `60.17875, 24.95062`.
- By default `1 drink = 0.18 promille` equivalent, roughly 3 beers.
- Coordinates are hardcoded into the example so it runs fully offline after build.
