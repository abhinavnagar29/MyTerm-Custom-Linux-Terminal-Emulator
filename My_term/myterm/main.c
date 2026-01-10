#include "myterm.h"

int main() {
    setlocale(LC_ALL, "");
    if (!XSupportsLocale()) { fprintf(stderr, "Warning: X locale not supported; Unicode rendering may be degraded.\n"); }
    signal(SIGCHLD, (void(*)(int))reaper);
    display = XOpenDisplay(NULL); if (!display) { fprintf(stderr, "Cannot open X display\n"); return 1; }
    int screen = DefaultScreen(display);
    setlocale(LC_CTYPE, ""); if (!XSupportsLocale()) fprintf(stderr, "Warning: X locale not supported, UTF-8 may not render correctly\n");
    window = XCreateSimpleWindow(display, RootWindow(display, screen), 100, 100, WIDTH, HEIGHT, 1, BlackPixel(display, screen), WhitePixel(display, screen));
    XSelectInput(display, window, ExposureMask | KeyPressMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | SelectionNotify | SelectionRequest);
    XMapWindow(display, window);
    gc = XCreateGC(display, window, 0, NULL);
    XStoreName(display, window, "MyTerm");

    Visual *visual = DefaultVisual(display, screen);
    cairo_surface = cairo_xlib_surface_create(display, window, visual, WIDTH, HEIGHT);
    cairo_cr = cairo_create(cairo_surface);
    {
        PangoLayout *tmp = pango_cairo_create_layout(cairo_cr);
        if (!font_desc) font_desc = pango_font_description_from_string("Noto Sans Devanagari 14, Noto Sans 14, DejaVu Sans 14");
        pango_layout_set_font_description(tmp, font_desc);
        pango_layout_set_text(tmp, "Ag", -1);
        PangoRectangle ink, logical; pango_layout_get_pixel_extents(tmp, &ink, &logical);
        int measured = logical.height > 0 ? logical.height : FONT_HEIGHT;
        LINE_HEIGHT = measured; if (LINE_HEIGHT < FONT_HEIGHT) LINE_HEIGHT = FONT_HEIGHT;
        LINE_GAP = 8;
        g_object_unref(tmp);
    }

    if (!font_desc) font_desc = pango_font_description_from_string("Noto Sans Devanagari 14, Noto Sans 14, DejaVu Sans 14");

    char **missing; int nmissing; char *defstr; fontset = XCreateFontSet(display, "*-*-*-*-*-*-*-*-*-*-*-*-*-*,*", &missing, &nmissing, &defstr); if (!fontset) fontset = NULL;

    XA_UTF8_STRING_ATOM = XInternAtom(display, "UTF8_STRING", False);
    XA_CLIPBOARD_ATOM = XInternAtom(display, "CLIPBOARD", False);
    XA_TARGETS_ATOM = XInternAtom(display, "TARGETS", False);
    XA_TEXT_ATOM = XInternAtom(display, "TEXT", False);
    XA_STRING_ATOM = XA_STRING;
    XA_PASTE_PROP_ATOM = XInternAtom(display, "MYTERMW_PASTE", False);

    create_new_tab();

    XEvent event; KeySym keysym; char keybuf[32];
    for (; app_running; ) {
        XNextEvent(display, &event);
        if (event.type == Expose) draw_terminal();
        else if (event.type == SelectionNotify) {
            if (event.xselection.property == XA_PASTE_PROP_ATOM) {
                Atom type; int format; unsigned long nitems, bytes_after; unsigned char *data = NULL;
                if (Success == XGetWindowProperty(display, window, XA_PASTE_PROP_ATOM, 0, 1000000, True, AnyPropertyType, &type, &format, &nitems, &bytes_after, &data)) {
                    if (data && current_tab < tab_count) { Tab *t = &tabs[current_tab]; size_t L = strlen(t->input_line); size_t ins = (size_t)nitems; if (L + ins < sizeof(t->input_line)) { memmove(t->input_line + t->cursor + ins, t->input_line + t->cursor, L - t->cursor + 1); memcpy(t->input_line + t->cursor, data, ins); t->cursor += ins; } XFree(data); draw_terminal(); }
                }
            }
        }
        else if (event.type == SelectionRequest) {
            XSelectionRequestEvent *req = &event.xselectionrequest;
            XSelectionEvent sev; memset(&sev, 0, sizeof(sev)); sev.type = SelectionNotify; sev.display = req->display; sev.requestor = req->requestor; sev.selection = req->selection; sev.time = req->time; sev.target = req->target; sev.property = None;
            if (req->selection == XA_CLIPBOARD_ATOM) {
                Atom targets[] = { XA_TARGETS_ATOM, XA_UTF8_STRING_ATOM, XA_TEXT_ATOM, XA_STRING_ATOM };
                if (req->target == XA_TARGETS_ATOM) { XChangeProperty(display, req->requestor, req->property, XA_ATOM, 32, PropModeReplace, (unsigned char*)targets, (int)(sizeof(targets)/sizeof(targets[0]))); sev.property = req->property; }
                else if (req->target == XA_UTF8_STRING_ATOM || req->target == XA_TEXT_ATOM || req->target == XA_STRING_ATOM) { const char *src = clipboard_text[0] ? clipboard_text : ""; XChangeProperty(display, req->requestor, req->property, req->target, 8, PropModeReplace, (const unsigned char*)src, (int)strlen(src)); sev.property = req->property; }
            }
            XSendEvent(display, req->requestor, True, 0, (XEvent*)&sev);
            XFlush(display);
        }
        else if (event.type == ButtonPress) handle_tab_click(event.xbutton.x, event.xbutton.y, event.xbutton.button);
        else if (event.type == MotionNotify) handle_motion_drag(event.xmotion.x, event.xmotion.y);
        else if (event.type == ButtonRelease) { dragging_scroll = 0; }
        else if (event.type == KeyPress) {
            int len = XLookupString(&event.xkey, keybuf, sizeof(keybuf), &keysym, NULL);
            if (current_tab < tab_count) {
                Tab *t = &tabs[current_tab];
                // Treat both keysym Ctrl+C (c or C) and ASCII ETX (0x03) as Ctrl+C
                if (((event.xkey.state & ControlMask) && (keysym == XK_c || keysym == XK_C)) || (len == 1 && (unsigned char)keybuf[0] == 3)) {
                    if (t->watch.active) {
                        // stop multiWatch immediately on Ctrl+C
                        stop_multiwatch(t);
                        draw_terminal();
                    } else if (current_fg_pgid > 0) {
                        // send SIGINT to the whole foreground process group
                        kill(-current_fg_pgid, SIGINT);
                    } else {
                        // No foreground job - clear input line and show new prompt
                        t->input_line[0] = '\0';
                        t->cursor = 0;
                        append_line(t, "^C");
                        draw_terminal();
                    }
                }
                
                else if ((event.xkey.state & ControlMask) && (keysym == XK_z || keysym == XK_Z)) {
                    if (t->watch.active) {
                        // Move multiWatch to background on Ctrl+Z
                        for (int i = 0; i < t->watch.n; i++) {
                            if (t->watch.pids[i] > 0) {
                                kill(-t->watch.pids[i], SIGTSTP);
                                kill(-t->watch.pids[i], SIGCONT);
                                // Add to job list if space available
                                if (t->proc_count < MAX_JOBS) {
                                    t->procs[t->proc_count].pid = t->watch.pids[i];
                                    snprintf(t->procs[t->proc_count].cmd, sizeof(t->procs[t->proc_count].cmd), 
                                             "multiWatch[%d]: %s", i, t->watch.cmds[i]);
                                    t->proc_count++;
                                }
                            }
                        }
                        // Clean up multiWatch state without killing processes
                        for (int i = 0; i < t->watch.n; i++) {
                            if (t->watch.pipes[i][0] != -1) { close(t->watch.pipes[i][0]); t->watch.pipes[i][0] = -1; }
                            if (t->watch.pipes[i][1] != -1) { close(t->watch.pipes[i][1]); t->watch.pipes[i][1] = -1; }
                            if (t->watch.tmpfds[i] != -1) { close(t->watch.tmpfds[i]); t->watch.tmpfds[i] = -1; }
                            if (t->watch.tmppaths[i][0]) { unlink(t->watch.tmppaths[i]); t->watch.tmppaths[i][0] = '\0'; }
                        }
                        t->watch.active = 0;
                        t->watch.n = 0;
                        append_line(t, "[multiWatch moved to background]");
                        draw_terminal();
                    } else if (current_fg_pgid > 0) {
                        // Move foreground process to background
                        pid_t pgid = current_fg_pgid;
                        kill(-pgid, SIGTSTP);
                        // Add to job list if space available
                        if (t->proc_count < MAX_JOBS) {
                            t->procs[t->proc_count].pid = pgid;
                            snprintf(t->procs[t->proc_count].cmd, sizeof(t->procs[t->proc_count].cmd), 
                                     "[PID: %d]", (int)pgid);
                            t->proc_count++;
                        }
                        kill(-pgid, SIGCONT);
                        current_fg_pgid = 0;
                        append_line(t, "[moved to background]");
                        draw_terminal();
                    }
                }
                
                else if ((event.xkey.state & ControlMask) && keysym == XK_t) {
                            create_new_tab();
                 }

                else if ((event.xkey.state & ControlMask) && (event.xkey.state & ShiftMask) && (keysym == XK_V || keysym == XK_v)) { XConvertSelection(display, XA_CLIPBOARD_ATOM, XA_UTF8_STRING_ATOM, XA_PASTE_PROP_ATOM, window, CurrentTime); }
                else if ((event.xkey.state & ControlMask) && (event.xkey.state & ShiftMask) && (keysym == XK_C || keysym == XK_c)) { strncpy(clipboard_text, t->input_line, sizeof(clipboard_text)-1); clipboard_text[sizeof(clipboard_text)-1] = '\0'; XSetSelectionOwner(display, XA_CLIPBOARD_ATOM, window, CurrentTime); }
                else if ((event.xkey.state & ControlMask) && keysym == XK_a) { t->cursor = 0; }
                else if ((event.xkey.state & ControlMask) && keysym == XK_e) { t->cursor = strlen(t->input_line); }
                else if ((event.xkey.state & ControlMask) && keysym == XK_l) { t->line_count = 0; t->scroll = 0; draw_terminal(); }
                else if ((event.xkey.state & ControlMask) && keysym == XK_w) { if (tab_count > 1) close_tab(current_tab); else { tabs[current_tab].line_count = 0; tabs[current_tab].scroll = 0; draw_terminal(); } }
                else if ((event.xkey.state & ControlMask) && keysym == XK_q) { app_running = 0; }
                else if (keysym == XK_Page_Up) { int step=5; t->scroll += step; draw_terminal(); }
                else if (keysym == XK_Page_Down) { int step=5; if (t->scroll>=step) t->scroll -= step; else t->scroll=0; draw_terminal(); }
                else if (keysym == XK_Home) { int ystart = TAB_HEIGHT + FONT_HEIGHT; if (t->proc_count) ystart += FONT_HEIGHT; if (t->watch.active) ystart += FONT_HEIGHT; if (t->search_mode) ystart += FONT_HEIGHT; int avail_h = HEIGHT - ystart - FONT_HEIGHT; int visible_lines = avail_h>0 ? (avail_h/FONT_HEIGHT) : 0; int max_scroll = t->line_count > visible_lines ? (t->line_count - visible_lines) : 0; t->scroll = max_scroll; draw_terminal(); }
                else if (keysym == XK_End) { t->scroll = 0; draw_terminal(); }
                else if ((event.xkey.state & ControlMask) && keysym == XK_r) { t->search_mode = 1; t->search_term[0]='\0'; t->search_len=0; }
                else if (t->search_mode) { if (keysym == XK_Return) { t->search_mode = 0; history_search(t); } else if (keysym == XK_BackSpace) { if (t->search_len) { t->search_term[--t->search_len] = '\0'; } } else if (len > 0 && isprint((unsigned char)keybuf[0])) { if (t->search_len + 1 < sizeof(t->search_term)) { t->search_term[t->search_len++] = keybuf[0]; t->search_term[t->search_len] = '\0'; } } }
                else if (keysym == XK_Tab) { autocomplete(t); t->cursor = strlen(t->input_line); }
                else if ((event.xkey.state & ShiftMask) && keysym == XK_Return) { size_t L = strlen(t->input_line); if (L + 1 < sizeof(t->input_line)) { memmove(t->input_line + t->cursor + 1, t->input_line + t->cursor, L - t->cursor + 1); t->input_line[t->cursor] = '\n'; t->cursor++; } }
                else if (keysym == XK_Return) { if (has_unquoted_newline(t->input_line)) { char buf[MAX_LINE_LEN+1]; strncpy(buf, t->input_line, sizeof(buf)-1); buf[sizeof(buf)-1] = '\0'; t->input_line[0] = '\0'; t->cursor = 0; run_lines_split_unquoted(t, buf); } else { char buf[MAX_LINE_LEN+1]; strncpy(buf, t->input_line, sizeof(buf)-1); buf[sizeof(buf)-1] = '\0'; t->input_line[0] = '\0'; t->cursor = 0; run_command(buf, t); } }
                else if (keysym == XK_BackSpace) { if (t->cursor > 0) { memmove(t->input_line + t->cursor - 1, t->input_line + t->cursor, strlen(t->input_line) - t->cursor + 1); t->cursor--; } }
                else if (keysym == XK_Left) { if (t->cursor > 0) t->cursor--; }
                else if (keysym == XK_Right) { if (t->cursor < strlen(t->input_line)) t->cursor++; }
                else if (len > 0 && isprint((unsigned char)keybuf[0])) {
                    if (t->ac_count > 0 && keybuf[0] >= '1' && keybuf[0] <= '9') { int sel = (keybuf[0] - '1'); if (sel >= 0 && sel < t->ac_count) { int start = t->ac_tok_start; if (start < 0) start = 0; if (start > (int)strlen(t->input_line)) start = (int)strlen(t->input_line); int end = start; while (t->input_line[end] && t->input_line[end] != ' ' && t->input_line[end] != '\t' && t->input_line[end] != '\n') end++; char newbuf[MAX_LINE_LEN]; int prefix = start; if (prefix > (int)sizeof(newbuf)-1) prefix = (int)sizeof(newbuf)-1; memcpy(newbuf, t->input_line, (size_t)prefix); newbuf[prefix] = '\0'; const char *choice = t->ac_choices[sel]; strncat(newbuf, choice, sizeof(newbuf) - 1 - strlen(newbuf)); strncat(newbuf, t->input_line + end, sizeof(newbuf) - 1 - strlen(newbuf)); strncpy(t->input_line, newbuf, sizeof(t->input_line) - 1); t->input_line[sizeof(t->input_line) - 1] = '\0'; t->cursor = strlen(t->input_line); t->ac_count = 0; } }
                    else { size_t L = strlen(t->input_line); if (L + 1 < sizeof(t->input_line)) { memmove(t->input_line + t->cursor + 1, t->input_line + t->cursor, L - t->cursor + 1); t->input_line[t->cursor] = keybuf[0]; t->cursor++; } }
                }
                draw_terminal();
            }
        }
        if (current_tab < tab_count) process_watch(&tabs[current_tab]);
    }
    for (int i = 0; i < tab_count; i++) { if (tabs[i].watch.active) stop_multiwatch(&tabs[i]); for (int j = 0; j < tabs[i].proc_count; j++) kill(tabs[i].procs[j].pid, SIGTERM); for (int j = 0; j < tabs[i].proc_count; j++) waitpid(tabs[i].procs[j].pid, NULL, 0); }
    if (cairo_cr) { cairo_destroy(cairo_cr); cairo_cr = NULL; }
    if (cairo_surface) { cairo_surface_destroy(cairo_surface); cairo_surface = NULL; }
    XCloseDisplay(display); return 0;
}
