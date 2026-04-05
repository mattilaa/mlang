# Package Manager Pinball Example

Terminal pinball prototype built in MLang with `std::esc`.

This example is intentionally small and self-contained:

- fullscreen terminal rendering with `std::esc`
- a portrait fixed playfield with a scrolling terminal viewport
- raw keyboard input
- one moving ball
- right-side shooter lane
- left and right flippers
- bumpers, slingshots, score, and lives
- an Avengers-inspired blue, gold, orange, and red color theme

It is a project starter, not a finished simulation. The current goal is to
prove the package/example layout and give you a clean place to keep iterating.
The current version keeps a taller portrait playfield than the visible terminal
area and scrolls horizontally and vertically as needed so the ball and active
part of the table stay in view. It also uses glyphs and truecolor terminal
styling from the existing stdlib instead of hardcoded ASCII-only shapes.

## Run

From this directory:

```sh
../../build/mlang pkg build
./build/package_manager_pinball_example
```

Or directly with the package runner:

```sh
../../build/mlang pkg run package_manager_pinball_example
```

## Controls

- `space`: charge and launch the ball from the shooter lane
- `a`: left flipper
- `d`: right flipper
- `r`: reset score, lives, and the current ball
- `q`: quit

## Next Steps

Good follow-up extensions for this starter:

- slingshots and kickers
- multiple balls
- richer Stern-style table geometry and orbit rules
- drop targets and lanes
- start screen and game-over screen
- sound hooks
- configurable table data instead of hardcoded coordinates
