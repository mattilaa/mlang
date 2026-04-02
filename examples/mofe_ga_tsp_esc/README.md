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

Arguments:
- `Mon|Tue|Wed|Thu|Fri|Sat|Sun`
- `HH:MM`
- optional start place name

Notes:
- `Flipperikellari` is modeled as open only on `Thu`.
- If the start place is omitted, the demo picks a random start venue.
- The header shows the current best score, walked meters, and route preview.
