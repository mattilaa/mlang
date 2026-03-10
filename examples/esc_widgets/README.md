# ESC Widgets Demo

Retro tracker-style terminal UI example built on `std::esc`.

Files:
- `widgets.mla`: reusable widget helpers (tracker bars/menus/table rendering) using `std::strbuf` padding APIs
- `tracker_ui_demo.mla`: ticker-driven event loop + keypress polling (`q` to quit)
- `run_demo.sh`: build + run helper

Run:

```sh
./examples/esc_widgets/run_demo.sh
```
