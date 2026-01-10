#include "myterm.h"

void handle_tab_click(int x, int y, unsigned int button) {
    if (y <= TAB_HEIGHT) {
        int shown = tab_count > 0 ? tab_count : 1; int tab_width = WIDTH / shown; int idx = x / tab_width;
        if (idx < tab_count) {
            int close_w = 12; int cx = idx * tab_width + tab_width - close_w - 6; int cy = (TAB_HEIGHT - 12) / 2;
            if (button == Button1 && x >= cx && x <= cx + close_w && y >= cy && y <= cy + 12) { if (tab_count > 1) { close_tab(idx); } return; }
            switch_to_tab(idx);
        } else if (idx == tab_count && tab_count < MAX_TABS) create_new_tab();
        return;
    }
    int sb_w = 10; int track_x = WIDTH - sb_w - 2; int track_y = TAB_HEIGHT + 2;
    int input_lines_click = 0; if (current_tab < tab_count) input_lines_click = count_lines(tabs[current_tab].input_line);
    int bottom_reserved_click_input = input_lines_click * (LINE_HEIGHT + LINE_GAP);
    int extra_ui_click = (LINE_GAP + 6) + 2;
    int bottom_reserved_click_total = bottom_reserved_click_input + extra_ui_click;
    int track_h = HEIGHT - track_y - bottom_reserved_click_total - 4;
    if (x >= track_x && x <= track_x + sb_w && y >= track_y && y <= track_y + track_h) {
        if (current_tab < tab_count) {
            Tab *t = &tabs[current_tab];
            int ystart = TAB_HEIGHT + LINE_HEIGHT; if (t->proc_count) ystart += (LINE_HEIGHT + LINE_GAP); if (t->watch.active) ystart += (LINE_HEIGHT + LINE_GAP); if (t->search_mode) ystart += (LINE_HEIGHT + LINE_GAP);
            int input_lines = count_lines(t->input_line);
            int bottom_reserved_input = input_lines * (LINE_HEIGHT + LINE_GAP);
            int extra_ui = (LINE_GAP + 6) + 2;
            int bottom_reserved_total = bottom_reserved_input + extra_ui;
            int avail_h = HEIGHT - ystart - bottom_reserved_total; int visible_lines = avail_h > 0 ? (avail_h / (LINE_HEIGHT + LINE_GAP)) : 0; int page = visible_lines > 0 ? visible_lines - 1 : 5; (void)page;
            if (button == Button1) {
                int knob_h = (visible_lines > 0 ? (track_h * visible_lines) / (t->line_count ? t->line_count : 1) : track_h); if (knob_h < 12) knob_h = 12; if (knob_h > track_h) knob_h = track_h;
                int max_scroll = t->line_count > visible_lines ? (t->line_count - visible_lines) : 0; int max_scroll_px = track_h - knob_h; int knob_y = track_y + (max_scroll ? (max_scroll_px * (max_scroll - t->scroll)) / max_scroll : 0);
                if (y >= knob_y && y <= knob_y + knob_h) { dragging_scroll = 1; drag_track_y = track_y; drag_track_h = track_h; drag_knob_h = knob_h; return; }
                if (y < knob_y) { t->scroll = (t->line_count > visible_lines ? (t->line_count - visible_lines) : 0); }
                else { t->scroll = 0; }
                draw_terminal(); return;
            }
        }
    }
    if (current_tab < tab_count) {
        Tab *t = &tabs[current_tab];
        int ystart = TAB_HEIGHT + LINE_HEIGHT;
        if (t->proc_count) ystart += (LINE_HEIGHT + LINE_GAP); if (t->watch.active) ystart += (LINE_HEIGHT + LINE_GAP); if (t->search_mode) ystart += (LINE_HEIGHT + LINE_GAP);
        int input_lines = count_lines(t->input_line);
        int bottom_reserved_input = input_lines * (LINE_HEIGHT + LINE_GAP);
        int extra_ui = (LINE_GAP + 6) + 2;
        int bottom_reserved_total = bottom_reserved_input + extra_ui;
        int avail_h = HEIGHT - ystart - bottom_reserved_total; int visible_lines = avail_h > 0 ? (avail_h / (LINE_HEIGHT + LINE_GAP)) : 0;
        int max_scroll = t->line_count > visible_lines ? (t->line_count - visible_lines) : 0;
        int step = 3;
        if (button == Button4) { t->scroll += step; if (t->scroll > max_scroll) t->scroll = max_scroll; }
        else if (button == Button5) { if (t->scroll >= step) t->scroll -= step; else t->scroll = 0; }
        draw_terminal();
    }
}

void handle_motion_drag(int x, int y) {
    if (!dragging_scroll || current_tab >= tab_count) return; Tab *t = &tabs[current_tab];
    int ystart = TAB_HEIGHT + LINE_HEIGHT; if (t->proc_count) ystart += LINE_HEIGHT; if (t->watch.active) ystart += LINE_HEIGHT; if (t->search_mode) ystart += LINE_HEIGHT;
    int input_lines = count_lines(t->input_line);
    int bottom_reserved_input = input_lines * (LINE_HEIGHT + LINE_GAP);
    int extra_ui = (LINE_GAP + 6) + 2;
    int bottom_reserved_total = bottom_reserved_input + extra_ui;
    int avail_h = HEIGHT - ystart - bottom_reserved_total; int visible_lines = avail_h > 0 ? (avail_h / (LINE_HEIGHT + LINE_GAP)) : 0; int max_scroll = t->line_count > visible_lines ? (t->line_count - visible_lines) : 0; if (max_scroll <= 0) { t->scroll = 0; draw_terminal(); return; }
    int track_y = drag_track_y, track_h = drag_track_h, knob_h = drag_knob_h; int max_scroll_px = track_h - knob_h; if (max_scroll_px <= 0) { t->scroll = 0; draw_terminal(); return; }
    int rel = y - track_y - (knob_h / 2); if (rel < 0) rel = 0; if (rel > max_scroll_px) rel = max_scroll_px;
    int new_scroll = max_scroll - (max_scroll * rel) / max_scroll_px;
    if (new_scroll < 0) new_scroll = 0; if (new_scroll > max_scroll) new_scroll = max_scroll;
    t->scroll = new_scroll; draw_terminal();
}

void autocomplete(Tab *t) {
    const char *buf = t->input_line; size_t len = strlen(buf); const char *start = buf; const char *tok = start; for (const char *q = buf + len; q > start; ) { q--; if (*q == ' ' || *q == '\t') { tok = q + 1; break; } tok = start; }
    char prefix[NAME_MAX+1]; size_t plen = (size_t)((buf + len) - tok); if (plen > NAME_MAX) plen = NAME_MAX; memcpy(prefix, tok, plen); prefix[plen] = '\0';
    DIR *d = opendir("."); if (!d) return; struct dirent *de; char names[256][NAME_MAX+1]; int is_dir[256]; int cnt=0; 
    while ((de=readdir(d))) { 
        const char *nm=de->d_name; 
        if (nm[0]=='.' && prefix[0] != '.') continue; 
        if (strncmp(nm, prefix, plen)==0) { 
            if (cnt<256){ 
                strncpy(names[cnt], nm, NAME_MAX); 
                names[cnt][NAME_MAX]='\0'; 
                // Check if directory using stat
                struct stat st;
                is_dir[cnt] = (stat(nm, &st) == 0 && S_ISDIR(st.st_mode));
                cnt++; 
            } 
        } 
    } 
    closedir(d);
    if (cnt==0) return;
    
    // Single match - complete with space or slash
    if (cnt==1){ 
        size_t keep = (size_t)(tok - start); 
        char newbuf[MAX_LINE_LEN]; 
        memcpy(newbuf, start, keep); 
        newbuf[keep]='\0'; 
        strncat(newbuf, names[0], sizeof(newbuf)-strlen(newbuf)-1);
        // Add trailing slash for directories, space for files
        if (strlen(newbuf) + 2 < sizeof(newbuf)) {
            if (is_dir[0]) {
                strcat(newbuf, "/");
            } else {
                strcat(newbuf, " ");
            }
        }
        strncpy(t->input_line, newbuf, sizeof(t->input_line)-1); 
        t->input_line[sizeof(t->input_line)-1]='\0'; 
        t->cursor = strlen(t->input_line); 
        return; 
    }
    
    // Multiple matches - find common prefix
    size_t minlen=strlen(names[0]); for (int i=1;i<cnt;i++){ size_t nl=strlen(names[i]); if (nl<minlen) minlen=nl; }
    size_t i=0; for (; i<minlen; i++){ char c=names[0][i]; int ok=1; for (int j=1;j<cnt;j++){ if (names[j][i]!=c){ ok=0; break; } } if (!ok) break; }
    
    // If we can complete more, do it
    if (i>plen){ 
        size_t keep=(size_t)(tok-start); 
        char newbuf[MAX_LINE_LEN]; 
        memcpy(newbuf,start,keep); 
        newbuf[keep]='\0'; 
        size_t tocopy=i; 
        if (tocopy > sizeof(newbuf)-1 - strlen(newbuf)) tocopy = sizeof(newbuf)-1 - strlen(newbuf); 
        strncat(newbuf, names[0], tocopy); 
        strncpy(t->input_line,newbuf,sizeof(t->input_line)-1); 
        t->input_line[sizeof(t->input_line)-1]='\0'; 
        t->cursor=strlen(t->input_line); 
    }
    
    // Show choices
    t->ac_count = cnt; t->ac_tok_start = (int)(tok - start);
    for (int k=0;k<cnt;k++){ strncpy(t->ac_choices[k], names[k], NAME_MAX); t->ac_choices[k][NAME_MAX] = '\0'; }
    append_line(t, "Choices:"); char line[MAX_LINE_LEN]; for (int k=0;k<cnt;k++){ snprintf(line,sizeof(line),"%d. %s", k+1, names[k]); append_line(t, line);} append_line(t, "Type number (1-9) to choose.");
}
