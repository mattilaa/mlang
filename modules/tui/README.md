# tui

A standalone MLang terminal widget library, built on `std::esc`, `std::term`,
`std::bytes`, and `std::strbuf`. Import `mod tui;` or individual `tui::*` modules.
It lives outside `std` and uses the existing module search/install mechanism.

This first version provides:

- Horizontal and vertical layouts with fixed sizes, weighted flexible sizes,
  and gaps. Nest layouts by splitting a child rectangle again.
- A viewport whose optional menu row is reserved above its content. Opening a
  menu never changes the layout's available height.
- Box-drawn panels and labels, using a customizable dark blue/gray RGB theme.
- A menu bar with keyboard navigation, disabled items, command IDs, scrolling
  selection in short popups, and popup bounds constrained to the viewport.
- Cell compositing: draw content first, then the menu bar. Popup shadows retain
  existing glyphs and darken both foreground and background colors.
- Explicit alternate-screen/raw-input setup and restoration, terminal size
  queries, and plain-text snapshots when output is redirected.

```mlang
mod tui;
use tui::geometry::*;
use tui::layout::*;
use tui::menu::*;
use tui::surface::*;
use tui::theme::*;
use tui::widgets::*;

fn main() -> i32 {
    let theme: Theme = Theme::dark_blue();
    let surface: Surface = Surface::new(80, 24);
    if !surface.is_valid() { return 1; }
    var menus: MenuBar = MenuBar::new([
        Menu { title: "File", items: [
            MenuItem { label: "Open", action: 1, enabled: true },
            MenuItem { label: "Quit", action: 2, enabled: true }
        ] }
    ]);
    let viewport: Viewport = menus.viewport(surface.bounds());
    let layout: Layout = Layout { axis: Axis::Horizontal, gap: 1,
        sizes: [Size::fixed(24), Size::flex(1)] };
    let panes: list<Rect> = layout.split(viewport.content());
    surface.fill(surface.bounds(), Cell { glyph: 32,
        foreground: theme.foreground, background: theme.background });
    let panel: Panel = Panel { title: " Browser " };
    panel.paint(surface, panes[0], theme);
    menus.open(0);
    menus.paint(surface, surface.bounds(), theme); // top layer, painted last
    surface.release();
    return 0;
}
```

`MenuBar::new([])` creates a viewport with no menu row. `on_key(Key::...)` returns
zero for navigation and a positive application-defined command ID on Enter.
Handle the command in the application; the library does not invoke callbacks.
The demo maps arrows, Tab, Escape, Enter, and `hjkl` to these semantic keys.

Repaint the underlying content before compositing menus each frame. This removes
old shadows when a menu closes or moves; repeatedly shading an existing frame
would otherwise darken it again. `Surface::cell` lets tests and custom widgets
inspect the final glyph and RGB colors independently of terminal escape output.

Rectangles use zero-based cell coordinates and exclusive right/bottom bounds.
Fixed layout sizes take priority over flexible sizes; if space is insufficient,
fixed sizes are truncated in declaration order. Gaps and all children stay within
the input extent. Use `Size::fixed` or `Size::flex` to construct constraints.

Surface copies share storage. Call `release()` exactly once after painting and
presentation, and do not use aliases afterward. `encode()` returns an owned
string; release it using `std::strbuf::free`. `Terminal::present` handles this for
you. Pair `Terminal::begin` with `finish` on every normal exit path. Applications
need their own abnormal-exit/signal policy. The demo handles `q`, Ctrl-C, and EOF,
redraws after resize, and exits after one plain snapshot outside a suitable TTY.

The initial text repertoire is single-cell printable ASCII, Latin characters,
and Unicode box drawing. Unsupported wide/combining characters and controls are
replaced with `?`; this is not a full Unicode grapheme/width engine. Rendering uses
truecolor escape sequences, so RGB-capable terminals give the intended theme and
shadows. There is no mouse support, focus system for content widgets, nested
submenus, or differential repainting yet.

From the repository root:

```sh
build/mlang examples/tui_demo.mla -L build -lmlang_std -o /tmp/mlang_tui_demo
/tmp/mlang_tui_demo
build/mlang --tests tests/tui_tests.mla -L build -lmlang_std
python3 tests/tui_terminal_smoke.py /tmp/mlang_tui_demo
```

The demo is an interaction/layout showcase; its menu commands report selection
rather than implementing session editing.
