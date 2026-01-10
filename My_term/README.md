# MyTerm

A minimal tabbed terminal/shell UI built with X11 + Cairo + Pango, refactored from a single-file prototype into a modular project. It supports job control, shell execution via `/bin/sh`, pipelines and redirections, history with search, autocomplete, a simple multi-command watcher, and clipboard copy/paste.

## Build (single Makefile)

Prerequisites (Debian/Ubuntu):
- build-essential
- libx11-dev
- libcairo2-dev
- libpango1.0-dev
- pkg-config

Build from project root using the single top-level Makefile:
```bash
make
```
This discovers `x11`, `cairo`, and `pangocairo` via `pkg-config` and produces `myterm/myterm`.

## Run
```bash
# Preferred

make run

# Or directly

./myterm/myterm
```



## Directory layout


- **Code**: under `myterm/`
  
  myterm/
  myterm.h
  utils.c
  gui.c
  input.c
  history.c
  multiwatch.c
  tabs.c
  shell.c
  main.c
```

- **README**: this file (build/run + features)
- **DESIGNDOC**: `DESIGNDOC.md` with per-feature design



## Features

- **1. Graphical User Interface (X11 tabs UI)**
  - Xlib window, event loop; Cairo+Pango text rendering; multiple tabs as independent shell instances.
  - Input/output routed through the X11 window.
  - Files: `myterm/gui.c`, `myterm/tabs.c`, `myterm/main.c`, `myterm/myterm.h`.

- **2. Run external commands**
  - `fork()` + `execvp()` for executables, with `/bin/sh -c` fallback for complex shell syntax.
  - Files: `myterm/shell.c`, `myterm/utils.c`.

- **3. Multiline Unicode input**
  - UTF-8 handling with Pango; multi-line input buffer; newline insertion on Shift+Enter.
  - Files: `myterm/input.c`, `myterm/gui.c`.

- **4. Input redirection (<)**
  - Use `dup2()` to redirect stdin from a file in child process.
  - Files: `myterm/shell.c`.

- **5. Output redirection (>) and combined < >**
  - Use `dup2()` to redirect stdout to a file; supports combined `< infile > outfile`.
  - Files: `myterm/shell.c`.

- **6. Pipes (|)**
  - N-stage pipelines via `pipe()`, `fork()`, `dup2()` with N−1 pipes; output shown in GUI.
  - Files: `myterm/shell.c`.

- **7. multiWatch command**
  - `multiWatch ["cmd1", "cmd2", ...]` runs commands in parallel; outputs multiplexed with timestamp and command name; stop with Ctrl+C.
  - Files: `myterm/multiwatch.c`.

- **8. Line navigation Ctrl+A / Ctrl+E**
  - Cursor jumps to start/end of input buffer using raw key handling.
  - Files: `myterm/input.c`.

- **9. Interrupting and backgrounding (signals)**
  - Ctrl+C sends SIGINT to foreground job (shell survives). Ctrl+Z backgrounds job; `jobs/bg/fg` supported.
  - Files: `myterm/shell.c`, `myterm/utils.c`.

- **10. Searchable history**
  - Persist last 10,000 commands in-session; `history` prints recent 1,000; Ctrl+R provides exact or longest-substring (>2) search.
  - Files: `myterm/history.c`.

- **11. Auto-complete for file names (Tab)**
  - Single match inserts; multiple matches extend to longest common prefix or show numbered choices.
  - Files: `myterm/input.c`.

## Keybindings (primary)
- **Ctrl+A / Ctrl+E**: Move cursor to start/end of input
- **Ctrl+R**: History search mode
- **Tab**: Autocomplete
- **Shift+Enter**: Insert newline in input
- **Enter**: Execute
- **Ctrl+Z**: Background current foreground job
- **Ctrl+C**: Interrupt foreground job / stop multiWatch


