# ESC Widgets Demo

Retro tracker-style terminal UI example built on `std::esc`.

Files:
- `widgets.mla`: reusable widget helpers (tracker bars/menus/table rendering) using `std::strbuf` padding APIs
- `tracker_ui_demo.mla`: ticker-driven event loop + keypress polling (`q` to quit)
- `run_demo.sh`: build + run helper

Menu widget:
- Top menu tabs: `File`, `View`, `Edit`, `Help`
- Tracker-style blue dropdown panel opens under the active tab with highlighted selection row
- Function keys activate/open menus: `F1=File`, `F2=View`, `F3=Edit`, `F4=Help`
- Keys: `j`/`k` move selection, `ENTER` selects, `TAB` cycles focus, `ESC` closes open dropdown, `q` quits

Run:

```sh
./examples/esc_widgets/run_demo.sh
```

Reusable terminal safety:
- This demo runner applies a shared TUI-safe cleanup trap (`stty sane`, reset,
  show cursor, leave alt screen) on `EXIT/INT/TERM`.
- Reuse the same pattern in all TUI example scripts.
- Reference helper docs: `docs/stdlib_mlang_api.md` -> `std::esc` ->
  `TUI Safety Helper`.
