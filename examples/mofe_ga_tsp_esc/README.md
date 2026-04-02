# M.O.F.E. Crawl Demo

This ESC demo renders a pub-crawl route in the terminal and scores it with:
- walking distance
- venue opening hours by weekday
- waiting / closed-venue penalties
- elevation-based fatigue, where uphill becomes more expensive later

Build and run through the helper script:

```sh
./examples/mofe_ga_tsp_esc/run_demo.sh Thu 13:00
```

Choose a fixed starting venue:

```sh
./examples/mofe_ga_tsp_esc/run_demo.sh Thu 13:00 Flipperikellari
```

Compact display with explicit alcohol settings:

```sh
./examples/mofe_ga_tsp_esc/run_demo.sh Thu 15:00 compact promille=0.25 drinks=2 avg=60
```

Lower-tolerance harder crawl:

```sh
./examples/mofe_ga_tsp_esc/run_demo.sh Thu 15:00 Flipperikellari 40 compact promille=0.20 drinks=1 avg=45
```

Arguments:
- `Mon|Tue|Wed|Thu|Fri|Sat|Sun`
- `HH:MM`
- optional start place name
- optional `tolerance` or `tolerance=0..100`
- optional `compact`
- optional `promille=0.00` or `start_promille=0.00`
- optional `drinks=N`
- optional `avg=MIN` or `stay=MIN`

Notes:
- `Flipperikellari` is modeled as open only on `Thu`.
- If the start place is omitted, the demo picks a random start venue.
- The header shows the current best score, walked meters, and route preview.
- `compact` mode shows only `path`, `schedule`, `alcohol plan`, and live `alcohol now`.
- `promille` sets the starting alcohol level.
- `drinks` sets drinks consumed at each venue.
- `avg` / `stay` sets the average minutes spent in one venue for the GA route calculation.
- By default `1 drink = 0.18 promille` equivalent, roughly 3 beers.
