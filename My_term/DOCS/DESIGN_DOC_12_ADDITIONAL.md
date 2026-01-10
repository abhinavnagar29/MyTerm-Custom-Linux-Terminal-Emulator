# DESIGN_DOC_ADDITIONAL: Additional Features Beyond Assignment Requirements

## Quick Index
1. Global Command History (shared across tabs)
2. Clipboard Copy/Paste (Ctrl+Shift+C / Ctrl+Shift+V)
3. Mouse Support (Tab switching, scrolling)
4. Quick Scrolling Keys (PageUp/PageDown, Home/End)
5. Tilde Expansion in cd
6. Graceful Job Tracking and Zombie Prevention
7. Unicode Rendering with Pango
8. Window Title and X11 Atoms Setup
9. Background Job Management (& suffix)
10. Status Banners (running jobs, multiWatch, search mode)

---

## 1) Global Command History (shared across tabs)

### What this provides
- All tabs share a single history buffer (10,000 entries).
- Commands executed in any tab are accessible from any other tab.
- Ctrl+R search works across all commands, regardless of which tab they were run in.

### Implementation
Handler: `myterm/history.c`.

- Global ring buffer `g_history[HISTORY_MAX]` stores all commands.
- `history_add()` is called from `myterm/shell.c: run_command()` after echoing the command.
- Avoids immediate duplicates: if the last entry matches the new command, it is skipped.
- `show_history()` displays the most recent 1,000 entries.
- `history_search()` implements Ctrl+R with exact match first, then LCS fallback.

References:
- `myterm/history.c` lines 1–62 (full implementation).
- `myterm/shell.c: run_command()` line 241 (call to history_add).
- `myterm/main.c` lines 101–102 (Ctrl+R key detection and search mode activation).

---

## 2) Clipboard Paste (Ctrl+Shift+V)

### What this provides
- Standard terminal paste functionality.
- Works with other desktop applications via X11 clipboard protocol.
- Ctrl+Shift+V pastes text from the clipboard at the cursor position.

### Implementation
Handler: `myterm/main.c`.
  
- **Paste (Ctrl+Shift+V)**:
  - Calls `XConvertSelection()` to request clipboard data from the current owner.
  - X11 delivers the data in a `SelectionNotify` event.
  - The text is inserted at the cursor position in `Tab.input_line`.

- **Selection Request Handling**:
  - When another app requests the clipboard, the `SelectionRequest` event is handled.
  - `XChangeProperty()` sends the clipboard data to the requesting app.

References:
- `myterm/main.c` lines 90–91 (Ctrl+Shift + V/C key handling).
- `myterm/main.c` lines 48–66 (SelectionRequest and SelectionNotify event handling).
- `myterm/utils.c` line 7 (clipboard_text global buffer).

---

## 3) Mouse Support (Tab switching, scrolling)

### What this provides
- Click on a tab to switch to it.
- Click on a tab's close box to close it.
- Click on the `+` area to create a new tab.
- Drag the scrollbar knob to navigate output history.
- Mouse wheel scrolling (Button4/Button5) to scroll output.

### Implementation
Handler: `myterm/input.c`.

- **Tab Clicks**: `handle_tab_click()` detects clicks in the tab bar.
  - Computes which tab was clicked based on x-coordinate.
  - Checks if the click is inside the close box or the `+` area.
  - Calls `switch_to_tab()`, `close_tab()`, or `create_new_tab()` accordingly.

- **Scrollbar Dragging**: 
  - `handle_tab_click()` detects clicks inside the scrollbar track.
  - Sets `dragging_scroll = 1` if the click is on the knob.
  - `handle_motion_drag()` updates `Tab.scroll` based on mouse y-position during drag.

- **Mouse Wheel**:
  - Button4 (wheel up) increases `Tab.scroll`.
  - Button5 (wheel down) decreases `Tab.scroll`.

References:
- `myterm/input.c: handle_tab_click()` lines 3–53 (tab and scrollbar click handling).
- `myterm/input.c: handle_motion_drag()` lines 55–68 (scrollbar drag handling).
- `myterm/main.c` lines 67–69 (ButtonPress and MotionNotify event routing).

---

## 4) Quick Scrolling Keys (PageUp/PageDown, Home/End)

### What this provides
- PageUp: scroll up by one page (visible_lines - 1).
- PageDown: scroll down by one page.
- Home: jump to the top of the output buffer (max scroll).
- End: jump to the bottom of the output buffer (scroll = 0).

### Implementation
Handler: `myterm/main.c`.

- Key handler detects `XK_Page_Up`, `XK_Page_Down`, `XK_Home`, `XK_End`.
- Computes the page size as `visible_lines - 1`.
- Updates `Tab.scroll` accordingly and clamps to valid range `[0, max_scroll]`.
- Calls `draw_terminal()` to repaint.

References:
- `myterm/main.c` lines 97–100 (PageUp/PageDown/Home/End key handling).

---

## 5) Enhanced cd with Quoted Paths and Tilde Expansion

### What this provides
- `cd "folder with spaces"` - supports directories with spaces using quotes
- `cd 'folder with spaces'` - supports single quotes
- `cd ~` expands to the user's home directory
- `cd ~/path` expands `~` to `$HOME/path`

### Implementation
Handler: `myterm/shell.c: run_command()` (cd builtin - handled early before quote parsing).

- Detects `cd` command before general quote processing
- Handles quoted paths (both single and double quotes)
- Strips quotes and extracts the actual path
- Performs tilde expansion if path starts with `~`
- Calls `chdir()` with the final expanded path

References:
- `myterm/shell.c: run_command()` lines 245–293 (cd builtin with quote handling and tilde expansion).

---

## 6) Graceful Job Tracking and Zombie Prevention

### What this provides
- Background jobs are tracked in `Tab.procs[]`.
- Finished background jobs are automatically reaped to prevent zombie processes.
- `jobs` builtin lists tracked background processes.
- `kill <pid>` removes a job from the list after sending SIGTERM.

### Implementation
Handler: `myterm/utils.c: reaper()` and `myterm/shell.c`.

- **SIGCHLD Handler**: `reaper()` is installed at startup.
  - Calls `waitpid(-1, &status, WNOHANG)` in a loop to reap finished children.
  - Removes reaped PIDs from all tabs' job lists via `remove_proc()`.

- **Job Tracking**:
  - Background jobs are added to `Tab.procs[]` when started (in `execute_simple()` or `execute_pipeline()`).
  - `list_jobs()` prints the tracked jobs.
  - `remove_proc()` removes a job from the list.

References:
- `myterm/utils.c: reaper()` lines 70–72 (SIGCHLD handler).
- `myterm/utils.c: remove_proc()` lines 60–62 (remove job from list).
- `myterm/utils.c: list_jobs()` lines 64–68 (print job list).
- `myterm/main.c` line 6 (signal handler installation).
- `myterm/shell.c: execute_simple()` line 41 (add background job to list).

---

## 7) Unicode Rendering with Pango

### What this provides
- Correct rendering of complex scripts (e.g., Devanagari, Arabic, CJK).
- Accurate text measurement for cursor positioning.
- Proper glyph shaping and ligatures.

### Implementation
Handler: `myterm/gui.c`.

- Uses Pango for text layout instead of basic X11 text rendering.
- `pango_layout_set_text()` sets the text to render.
- `pango_cairo_show_layout()` renders the text with Cairo.
- `pango_layout_index_to_pos()` provides accurate cursor position for any Unicode string.

References:
- `myterm/gui.c` lines 5–20 (Pango initialization).
- `myterm/gui.c: draw_input()` lines 180–220 (Pango layout and rendering).
- `myterm/gui.c: draw_text()` lines 230–250 (helper for drawing text with Pango).

---

## 8) Window Title and X11 Atoms Setup

### What this provides
- Sets the window title to "MyTermw".
- Registers X11 atoms for clipboard operations (UTF8_STRING, CLIPBOARD, TARGETS, etc.).

### Implementation
Handler: `myterm/main.c`.

- `XStoreName()` sets the window title.
- `XInternAtom()` registers atoms for clipboard protocol.

References:
- `myterm/main.c` lines 30–40 (window title and atom setup).
- `myterm/utils.c` lines 6–7 (atom globals).

---

## 9) Background Job Management (& suffix)

### What this provides
- Commands ending with `&` run in the background.
- The shell remains responsive while background jobs run.
- Background jobs are tracked and can be managed with `jobs`, `bg`, `fg`, `kill`.

### Implementation
Handler: `myterm/shell.c: run_command()`.

- Detects trailing `&` in the command line.
- Strips the `&` and calls `execute_pipeline()` with `background=1`.
- Background jobs do not capture output; they are tracked in `Tab.procs[]`.

References:
- `myterm/shell.c: run_command()` lines 295–308 (background job detection and routing).
- `myterm/shell.c: execute_simple()` lines 38–41 (background job tracking).
- `myterm/shell.c: execute_pipeline()` lines 183–187 (background pipeline tracking).

---

## 10) Status Banners (running jobs, multiWatch, search mode)

### What this provides
- Visual feedback for active features:
  - "Running jobs: N" when background jobs are active.
  - "multiWatch active" when multiWatch is running.
  - "Search mode (Ctrl+R): <term>" when history search is active.
- Banners are drawn above the output area and do not collide with text.

### Implementation
Handler: `myterm/gui.c: draw_terminal()`.

- Checks `Tab.proc_count`, `Tab.watch.active`, and `Tab.search_mode`.
- Draws status banners as extra rows above the output area.
- Adjusts the output area start position (`ystart`) to account for banners.

References:
- `myterm/gui.c: draw_terminal()` lines 50–80 (status banner rendering).
- `myterm/gui.c: draw_terminal()` lines 90–100 (ystart adjustment for banners).

---

## Summary

These additional features enhance the usability and polish of MyTermw beyond the core assignment requirements:

1. **Global History** - Shared across tabs with Ctrl+R search.
2. **Clipboard** - Standard copy/paste with X11 protocol.
3. **Mouse Support** - Tab switching, scrolling, and dragging.
4. **Quick Scrolling** - PageUp/PageDown/Home/End keys.
5. **Tilde Expansion** - `cd ~` and `cd ~/path` support.
6. **Job Tracking** - Automatic zombie prevention with SIGCHLD handler.
7. **Unicode Rendering** - Pango for complex scripts and accurate cursor positioning.
8. **Window Title** - Sets "MyTermw" title and registers X11 atoms.
9. **Background Jobs** - `&` suffix for background execution.
10. **Status Banners** - Visual feedback for running jobs, multiWatch, and search mode.

All features are implemented with proper error handling, references to exact code locations, and integration with the existing modular architecture.
