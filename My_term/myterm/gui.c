#include "myterm.h"

void draw_text(int x, int y, const char *s) {
    if (!s || !cairo_cr) return;
    PangoLayout *layout = pango_cairo_create_layout(cairo_cr);
    pango_layout_set_text(layout, s, -1);
    if (font_desc) pango_layout_set_font_description(layout, font_desc);
    cairo_move_to(cairo_cr, x, y);
    cairo_set_source_rgb(cairo_cr, 0, 0, 0);
    pango_cairo_show_layout(cairo_cr, layout);
    g_object_unref(layout);
}

void get_text_size(const char *s, int *out_w, int *out_h) {
    if (out_w) {
        *out_w = 0;
    }
    if (out_h) {
        *out_h = LINE_HEIGHT;
    }
    if (!s || !cairo_cr) return;
    PangoLayout *pl = pango_cairo_create_layout(cairo_cr);
    pango_layout_set_text(pl, s, -1);
    if (font_desc) {
        pango_layout_set_font_description(pl, font_desc);
    }
    PangoRectangle ink, logical;
    pango_layout_get_pixel_extents(pl, &ink, &logical);
    if (out_w) {
        *out_w = logical.width;
    }
    if (out_h) {
        *out_h = logical.height > 0 ? logical.height : LINE_HEIGHT;
    }
    g_object_unref(pl);
}

void draw_tabs() {
    XSetForeground(display, gc, 0xCCCCCC); XFillRectangle(display, window, gc, 0, 0, WIDTH, TAB_HEIGHT);
    XSetForeground(display, gc, 0x000000); XDrawLine(display, window, gc, 0, TAB_HEIGHT, WIDTH, TAB_HEIGHT);
    int shown = tab_count > 0 ? tab_count : 1; int tab_width = WIDTH / shown; int x = 0;
    for (int i = 0; i < tab_count; i++) {
        if (tabs[i].is_active) { XSetForeground(display, gc, 0xFFFFFF); XFillRectangle(display, window, gc, x, 0, tab_width, TAB_HEIGHT); }
        XSetForeground(display, gc, 0x000000);
        char label[96]; snprintf(label, sizeof(label), "%s (%d)%s%s", tabs[i].name, tabs[i].id, tabs[i].proc_count ? " [*]" : "", tabs[i].watch.active ? " [MW]" : "");
        int tw=0, th=LINE_HEIGHT; get_text_size(label, &tw, &th);
        int ty = (TAB_HEIGHT - th) / 2; if (ty < 0) ty = 0;
        draw_text(x +6, ty, label);
        int close_w = 12; int close_h = 12; int cx = x + tab_width - close_w - 6; int cy = (TAB_HEIGHT - close_h) / 2;
        XDrawRectangle(display, window, gc, cx, cy, close_w, close_h);
        XDrawLine(display, window, gc, cx + 2, cy + 2, cx + close_w - 2, cy + close_h - 2);
        XDrawLine(display, window, gc, cx + close_w - 2, cy + 2, cx + 2, cy + close_h - 2);
        XDrawRectangle(display, window, gc, x, 0, tab_width, TAB_HEIGHT); x += tab_width;
    }
    if (tab_count < MAX_TABS) { XDrawString(display, window, gc, x +6, TAB_HEIGHT / 2 + 5, "+", 1); XDrawRectangle(display, window, gc, x, 0, 30, TAB_HEIGHT); }
}

void draw_input(Tab *t, int y) {
    // Render prompt + input as a single Pango layout so glyph metrics and caret align perfectly
    char prefix[512];
    get_prompt(prefix, sizeof(prefix));
    char combined[MAX_LINE_LEN + 512];
    combined[0] = '\0';
    strncat(combined, prefix, sizeof(combined) - 1);
    strncat(combined, t->input_line, sizeof(combined) - 1 - strlen(combined));

    int x = LEFT_MARGIN;
    PangoLayout *pl = pango_cairo_create_layout(cairo_cr);
    pango_layout_set_text(pl, combined, -1);
    if (font_desc) {
        pango_layout_set_font_description(pl, font_desc);
    }
    cairo_move_to(cairo_cr, x, y);
    pango_cairo_show_layout(cairo_cr, pl);

    // Caret position uses index within combined string
    size_t curi = t->cursor; if (curi > strlen(t->input_line)) curi = strlen(t->input_line);
    int prefix_len = (int)strlen(prefix);
    int caret_index = prefix_len + (int)curi;
    if (caret_index < 0) caret_index = 0;
    if (caret_index > (int)strlen(combined)) caret_index = (int)strlen(combined);

    PangoRectangle caret_pos; pango_layout_index_to_pos(pl, caret_index, &caret_pos);
    int caret_x = x + caret_pos.x / PANGO_SCALE;
    int caret_y = y + caret_pos.y / PANGO_SCALE;
    int caret_h = caret_pos.height > 0 ? caret_pos.height / PANGO_SCALE : LINE_HEIGHT;

    g_object_unref(pl);
    if (cairo_cr) {
        cairo_set_source_rgb(cairo_cr, 0, 0, 0);
        cairo_set_line_width(cairo_cr, 1.0);
        cairo_move_to(cairo_cr, caret_x, caret_y);
        cairo_line_to(cairo_cr, caret_x, caret_y + caret_h);
        cairo_stroke(cairo_cr);
    } else {
        XDrawLine(display, window, gc, caret_x, caret_y, caret_x, caret_y + caret_h);
    }
}

void draw_terminal() {
    if (cairo_cr) { cairo_set_source_rgb(cairo_cr, 1, 1, 1); cairo_paint(cairo_cr); cairo_set_source_rgb(cairo_cr, 0, 0, 0); }
    else { XClearWindow(display, window); }
    draw_tabs(); if (current_tab >= tab_count) return; Tab *t = &tabs[current_tab]; int y = TAB_HEIGHT + LINE_HEIGHT;
    if (t->proc_count) { XSetForeground(display, gc, 0xFF0000); char s[64]; snprintf(s, sizeof(s), "Running processes: %d", t->proc_count); draw_text(LEFT_MARGIN, y, s); y += (LINE_HEIGHT + LINE_GAP); XSetForeground(display, gc, 0x000000); }
    if (t->watch.active) { XSetForeground(display, gc, 0x0000AA); const char *mw = "multiWatch active (Ctrl+C to stop)"; draw_text(LEFT_MARGIN, y, mw); y += (LINE_HEIGHT + LINE_GAP); XSetForeground(display, gc, 0x000000); }
    if (t->search_mode) { XSetForeground(display, gc, 0x006600); char s[320]; snprintf(s, sizeof(s), "Enter search term: %s", t->search_term); draw_text(LEFT_MARGIN, y, s); y += (LINE_HEIGHT + LINE_GAP); XSetForeground(display, gc, 0x000000); }
    int input_lines = count_lines(t->input_line);
    int bottom_reserved_input = input_lines * (LINE_HEIGHT + LINE_GAP);
    int extra_ui = (LINE_GAP + 6) + 2;
    int bottom_reserved_total = bottom_reserved_input + extra_ui;
    int avail_h = HEIGHT - y - bottom_reserved_total; if (avail_h < 0) avail_h = 0;
    int visible_lines = avail_h / (LINE_HEIGHT + LINE_GAP); if (visible_lines < 0) visible_lines = 0;
    int max_scroll = t->line_count > visible_lines ? (t->line_count - visible_lines) : 0;
    if (t->scroll > max_scroll) t->scroll = max_scroll; if (t->scroll < 0) t->scroll = 0;
    int start = 0;
    if (t->line_count > visible_lines) start = t->line_count - visible_lines - t->scroll;
    for (int i = 0; i < visible_lines; i++) { int idx = start + i; if (idx < 0 || idx >= t->line_count) break; draw_text(LEFT_MARGIN, y, t->buffer[idx]); y += (LINE_HEIGHT + LINE_GAP); }
    XSetForeground(display, gc, 0xCCCCCC);
    XDrawLine(display, window, gc, 0, y, WIDTH, y);
    XDrawLine(display, window, gc, 0, y + 1, WIDTH, y + 1);
    XSetForeground(display, gc, 0x000000);
    int input_y = y + LINE_GAP + 6;
    if (cairo_cr) { cairo_set_source_rgb(cairo_cr, 1, 1, 1); cairo_rectangle(cairo_cr, 0, input_y, WIDTH, bottom_reserved_input); cairo_fill(cairo_cr); cairo_set_source_rgb(cairo_cr, 0, 0, 0); }
    draw_input(t, input_y + 2);
    int sb_w = 10; int track_x = WIDTH - sb_w - 2; int track_y = TAB_HEIGHT + 2; int track_h = HEIGHT - track_y - bottom_reserved_total - 4;
    XDrawRectangle(display, window, gc, track_x, track_y, sb_w, track_h);
    if (t->line_count > 0) {
        int knob_h = visible_lines > 0 ? (track_h * visible_lines) / (t->line_count) : track_h;
        if (knob_h < 12) knob_h = 12; if (knob_h > track_h) knob_h = track_h;
        int max_scroll_px = (track_h - knob_h);
        int knob_y = track_y + (max_scroll ? (max_scroll_px * (max_scroll - t->scroll)) / max_scroll : 0);
        XFillRectangle(display, window, gc, track_x + 1, knob_y + 1, sb_w - 2, knob_h - 2);
        drag_track_y = track_y; drag_track_h = track_h; drag_knob_h = knob_h;
    }
}
