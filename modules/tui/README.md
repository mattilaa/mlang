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
- Spatial pane focus with accent-colored borders and Ctrl+Shift+H/J/K/L
  navigation. Menus temporarily suspend focus and restore the selected pane.
- A menu bar with keyboard navigation, disabled items, command IDs, scrolling
  selection in short popups, cascading submenus, and popup bounds constrained
  to the viewport.
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

### Cascading submenus

Add a submenu as a menu item with `MenuItem::submenu(label, children)`:

```mlang
MenuItem::submenu("Recent sessions", [
    MenuItem { label: "Blue hour", action: 8, enabled: true },
    MenuItem::submenu("Templates", [
        MenuItem { label: "Ambient", action: 9, enabled: true },
        MenuItem { label: "Dance", action: 10, enabled: true }
    ])
])
```

Rows with children display `>` at the right. `l`/Right or Enter opens the
selected item's submenu. `h`/Left closes one submenu and restores its parent's
selected row. `j`/Down and `k`/Up navigate only the deepest open menu, skipping
disabled items. On a leaf inside a submenu, `l` does nothing. At the root,
Left selects the previous menu-bar entry and Right selects the next entry when
the selected row has no submenu.

Children open beside their parent with their first row aligned to the invoking
row where space permits. They flip left at the right edge, then clamp to the
viewport when neither side fits. Parent menus stay visible underneath; each
popup has its own glyph-preserving shadow. Short popups scroll the selected row
into view at every depth.

Escape closes **all** menus at once, as do Tab and activating a leaf command.
The last selected pane becomes active again. `MenuBar::depth()` reports the
number of open child levels; `popup_rect_at(bounds, depth)` exposes each level's
geometry. `active` continues to identify the root menu-bar entry, while
`selected` identifies the deepest menu's row. Existing leaf item literals work
unchanged because `children` defaults to an empty list. An empty children list
is a leaf, not an openable submenu. To disable a branch, set `enabled: false`
on a `MenuItem` with nonempty `children`; a branch does not invoke its `action`.

### Pane focus and modified keys

`tui::focus::PaneFocus` remembers a stable, nonnegative application pane ID.
Describe the current layout as `list<Pane>` (`id` and `bounds`) and call
`reconcile(panes)` after layout changes. A selected visible pane keeps its ID
across resize/reordering; when it disappears or becomes too small for a border,
focus falls back to the first visible pane (or -1 when none remain).

Call `set_menu_active(menus.active >= 0)` before routing input and painting.
While the menu owns input, `is_active(id)` is false for every pane and
`navigate()` does nothing; the remembered selection is retained. Synchronizing
again after any menu dismissal restores that pane, including dismissal through
Escape, Tab, or command activation.

Paint focusable panels with `panel.paint_focused(surface, bounds, theme,
focus.is_active(id))`. The active border uses `theme.accent`; inactive borders
use `theme.border` and inactive titles use `theme.muted`. Route pane commands to
`focus.navigate(Key::Left/Down/Up/Right, panes)`. Navigation does not wrap; it
prefers neighbors overlapping on the perpendicular axis, then the nearest edge
and center. Ties use pane declaration order.

`tui::input::InputDecoder` handles input a byte at a time and emits `InputEvent`
values with kinds `None`, `Menu`, `Pane`, or `Quit`. In the demo, Ctrl+Shift+H/J/K/L
maps to left/down/up/right pane navigation; plain `hjkl` remains menu navigation.
Pass bytes to `feed()`. If `pending()` stays true with no new input for about
50ms, call `expire()` to resolve a bare Escape and discard incomplete packets.

The shortcut needs a terminal that reports the modifiers distinctly.
`Terminal::begin()` pushes the disambiguation flag from the
[kitty keyboard protocol](https://sw.kovidgoyal.net/kitty/keyboard-protocol/), and
`finish()` pops it. The decoder accepts CSI-u/kitty sequences such as
`ESC [ 104 ; 6 u` and already-configured xterm modifyOtherKeys sequences such as
`ESC [ 27 ; 6 ; 104 ~`. Releases are ignored, while presses and repeats work.
Terminal-owned shortcuts may need rebinding to pass these combinations through.
Legacy terminals can collapse Ctrl+Shift+H/J to Backspace/Enter; those ambiguous
bytes are deliberately not treated as pane navigation. The menu still works
with legacy arrow keys and unmodified `hjkl`.

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
shadows. There is no mouse support, focus traversal inside a pane, or
differential repainting yet.

From the repository root:

```sh
build/mlang examples/tui_demo.mla -L build -lmlang_std -o /tmp/mlang_tui_demo
/tmp/mlang_tui_demo
build/mlang --tests tests/tui_tests.mla -L build -lmlang_std
python3 tests/tui_terminal_smoke.py /tmp/mlang_tui_demo
```

The demo is an interaction/layout showcase; its menu commands report selection
rather than implementing session editing.
