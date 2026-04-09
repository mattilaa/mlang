# Kallio Pub Crawl Demo

This ESC demo renders a Kallio pub-crawl route in the terminal and scores it with:
- walking distance
- venue opening hours by weekday
- waiting / closed-venue penalties
- elevation-based fatigue, where uphill becomes more expensive later
- alcohol load, using `promille` and `drinks` parameters in the cost function

Build and run through the helper script:

```sh
./examples/kallio_pub_crawl_esc/run_demo.sh Thu 18:00
```

Choose a fixed starting venue:

```sh
./examples/kallio_pub_crawl_esc/run_demo.sh Thu 18:00 Siltanen
```

Compact display with explicit alcohol settings:

```sh
./examples/kallio_pub_crawl_esc/run_demo.sh Sat 18:00 Kustaa_Vaasa stable compact promille=0.25 drinks=2 avg=60
```

Lower-tolerance harder crawl:

```sh
./examples/kallio_pub_crawl_esc/run_demo.sh Fri 19:00 Roskapankki 40 compact promille=0.20 drinks=1 avg=45
```

Arguments:
- `Mon|Tue|Wed|Thu|Fri|Sat|Sun`
- `HH:MM`
- optional start place name
- optional `tolerance` or `tolerance=0..100`
- optional `compact`
- optional `stable`
- optional `promille=0.00` or `start_promille=0.00`
- optional `drinks=N`
- optional `avg=MIN` or `stay=MIN`

Notes:
- Available Kallio start venues are `Kallion_Savel`, `Pub_Sirdie`, `Siltanen`, `Kuudes_Linja`, `Roskapankki`, `Kustaa_Vaasa`, and `Bar_Molotow`.
- If the start place is omitted, the demo picks a random start venue.
- The header shows the current best score, walked meters, and route preview.
- `compact` mode shows only `path`, `schedule`, `alcohol plan`, and live `alcohol now`.
- `stable` disables the random start venue fallback and starts the on-screen walker from the beginning of the route.
- `promille` sets the starting alcohol level.
- `drinks` sets the same drinks-per-bar value used at each venue.
- `avg` / `stay` sets the average minutes spent in one venue for the GA route calculation.
- By default `1 drink = 0.18 promille` equivalent, roughly 3 beers.
- Coordinates are hardcoded into the example so it runs fully offline after build.
