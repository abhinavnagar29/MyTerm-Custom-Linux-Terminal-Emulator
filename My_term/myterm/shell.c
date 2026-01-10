#include "myterm.h"

static void strip_pair(char **argv, int pos) {
    int j = pos;
    while (argv[j + 2]) { argv[j] = argv[j + 2]; j++; }
    argv[j] = NULL;
}

static void parse_redirections(char **argv, char **infile, char **outfile) {
    *infile = NULL; *outfile = NULL;
    for (int i = 0; argv[i]; ++i) {
        if (strcmp(argv[i], "<") == 0 && argv[i + 1]) {
            *infile = argv[i + 1];
            strip_pair(argv, i);
            i--;
            continue;
        }
        if (strcmp(argv[i], ">") == 0 && argv[i + 1]) {
            *outfile = argv[i + 1];
            strip_pair(argv, i);
            i--;
            continue;
        }
    }
}

void execute_simple(Tab *t, char **argv, int background) {
    char *infile = NULL, *outfile = NULL; parse_redirections(argv, &infile, &outfile);
    int use_parent_pipe = (!background && outfile == NULL);
    int pipefd[2]; if (use_parent_pipe) { if (pipe(pipefd) == -1) { append_line(t, "pipe failed"); draw_terminal(); return; } }
    pid_t pid = fork();
    if (pid == 0) {
        setpgid(0, 0);
        if (use_parent_pipe) { close(pipefd[0]); dup2(pipefd[1], STDOUT_FILENO); dup2(pipefd[1], STDERR_FILENO); close(pipefd[1]); }
        if (infile) { int fd = open(infile, O_RDONLY); if (fd < 0) { perror("open infile"); _exit(126); } if (dup2(fd, STDIN_FILENO) < 0) { perror("dup2 infile"); _exit(126); } close(fd); }
        if (outfile) { int fd = open(outfile, O_WRONLY | O_CREAT | O_TRUNC, 0644); if (fd < 0) { perror("open outfile"); _exit(126); } if (dup2(fd, STDOUT_FILENO) < 0) { perror("dup2 stdout"); _exit(126); } if (dup2(fd, STDERR_FILENO) < 0) { perror("dup2 stderr"); _exit(126); } close(fd); }
        execvp(argv[0], argv); perror("execvp"); _exit(127);
    } else if (pid > 0) {
        setpgid(pid, pid);
        char cmd[256] = {0}; for (int i = 0; argv[i] && i < 20; i++) { if (i) strncat(cmd, " ", sizeof(cmd) - strlen(cmd) - 1); strncat(cmd, argv[i], sizeof(cmd) - strlen(cmd) - 1); }
        if (background) { t->procs[t->proc_count].pid = pid; strncpy(t->procs[t->proc_count].cmd, cmd, sizeof(t->procs[t->proc_count].cmd) - 1); t->proc_count++; char msg[320]; snprintf(msg, sizeof(msg), "Started background: %s [PID: %d]", cmd, (int)pid); append_line(t, msg); draw_terminal(); }
        else {
            current_fg_pgid = pid;
            if (use_parent_pipe) {
                close(pipefd[1]); int flags = fcntl(pipefd[0], F_GETFL, 0); if (flags != -1) fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);
                char out[BUFFER_SIZE]; size_t outlen = 0; char buf[1024]; ssize_t n; int stopped = 0;
                for (;;) {
                    n = read(pipefd[0], buf, sizeof(buf) - 1);
                    if (n > 0) { buf[n] = '\0'; size_t remain = sizeof(out) - 1 - outlen; if (remain) { size_t tc = (size_t)n < remain ? (size_t)n : remain; memcpy(out + outlen, buf, tc); outlen += tc; } }
                    while (XPending(display) > 0) {
                        XEvent ev; XNextEvent(display, &ev);
                        if (ev.type == KeyPress) {
                            KeySym ks; char kb[8]; XLookupString(&ev.xkey, kb, sizeof(kb), &ks, NULL);
                            if ((ev.xkey.state & ControlMask) && (ks == XK_c || ks == XK_C)) {
                                // Ctrl+C: interrupt the foreground job
                                kill(-pid, SIGINT);
                                append_line(t, "^C");
                                stopped = 1;
                                break;
                            }
                            if ((ev.xkey.state & ControlMask) && (ks == XK_z || ks == XK_Z)) {
                                kill(-pid, SIGTSTP);
                                if (t->proc_count < MAX_JOBS) { t->procs[t->proc_count].pid = pid; strncpy(t->procs[t->proc_count].cmd, cmd, sizeof(t->procs[t->proc_count].cmd)-1); t->proc_count++; }
                                kill(-pid, SIGCONT);
                                append_line(t, "[moved to background]"); draw_terminal();
                                stopped = 1; break;
                            }
                        }
                        if (ev.type == Expose) { draw_terminal(); }
                        else if (ev.type == ButtonPress) { handle_tab_click(ev.xbutton.x, ev.xbutton.y, ev.xbutton.button); }
                        else if (ev.type == MotionNotify) { handle_motion_drag(ev.xmotion.x, ev.xmotion.y); }
                    }
                    if (stopped) break; if (n == 0) break; if (n < 0 && errno != EINTR && errno != EAGAIN) break; if (n < 0) { struct timespec ts = {0, 5*1000*1000}; nanosleep(&ts, NULL); }
                }
                close(pipefd[0]); if (stopped) { current_fg_pgid = 0; return; }
                int status; waitpid(pid, &status, 0); out[outlen] = '\0'; char *start = out; int any = 0; for (char *p = out; ; ++p) { if (*p == '\n' || *p == '\0') { char save = *p; *p = '\0'; if (*start) { append_line(t, start); any = 1; } if (save == '\0') break; *p = save; start = p + 1; } }
                if (!any) { if (WIFEXITED(status) && WEXITSTATUS(status) == 127) { append_line(t, "error: command not found or failed to execute"); } else { append_line(t, "[no output]"); } }
                draw_terminal();
                current_fg_pgid = 0;
            } else {
                int status; int stopped = 0;
                for (;;) {
                    int r = waitpid(pid, &status, WNOHANG);
                    if (r == pid) break;
                    while (XPending(display) > 0) {
                        XEvent ev; XNextEvent(display, &ev);
                        if (ev.type == KeyPress) {
                            KeySym ks; char kb[8]; XLookupString(&ev.xkey, kb, sizeof(kb), &ks, NULL);
                            if ((ev.xkey.state & ControlMask) && (ks == XK_z || ks == XK_Z)) {
                                kill(-pid, SIGTSTP);
                                if (t->proc_count < MAX_JOBS) { t->procs[t->proc_count].pid = pid; strncpy(t->procs[t->proc_count].cmd, cmd, sizeof(t->procs[t->proc_count].cmd)-1); t->proc_count++; }
                                kill(-pid, SIGCONT);
                                append_line(t, "[moved to background]"); draw_terminal();
                                stopped = 1; break;
                            }
                        }
                        if (ev.type == Expose) { draw_terminal(); }
                        else if (ev.type == ButtonPress) { handle_tab_click(ev.xbutton.x, ev.xbutton.y, ev.xbutton.button); }
                        else if (ev.type == MotionNotify) { handle_motion_drag(ev.xmotion.x, ev.xmotion.y); }
                    }
                    if (stopped) { current_fg_pgid = 0; return; }
                    struct timespec ts = {0, 5*1000*1000}; nanosleep(&ts, NULL);
                }
                if (WIFEXITED(status) && WEXITSTATUS(status) == 127) append_line(t, "error: command not found or failed to execute");
                else append_line(t, "[completed]");
                draw_terminal();
                current_fg_pgid = 0;
            }
        }
    } else { append_line(t, "fork failed"); }
}

int split_pipeline(const char *cmd_line, char stages[][MAX_LINE_LEN], int *count) {
    *count = 0; if (!cmd_line) return 0; char tmp[MAX_LINE_LEN]; strncpy(tmp, cmd_line, sizeof(tmp)-1); tmp[sizeof(tmp)-1] = '\0'; char *saveptr=NULL; char *part=strtok_r(tmp, "|", &saveptr); while (part && *count < MAX_STAGES) { while (*part==' ') part++; size_t L=strlen(part); while (L && (part[L-1]==' '||part[L-1]=='\n'||part[L-1]=='\t')) part[--L]='\0'; strncpy(stages[*count], part, MAX_LINE_LEN-1); stages[*count][MAX_LINE_LEN-1]='\0'; (*count)++; part=strtok_r(NULL, "|", &saveptr);} return *count>0;
}

void execute_sh(Tab *t, const char *script) {
    if (!t || !script) return; int fd[2]; if (pipe(fd) != 0) { append_line(t, "error: pipe failed"); return; }
    pid_t pid = fork();
    if (pid == 0) {
        setpgid(0, 0);
        close(fd[0]); dup2(fd[1], STDOUT_FILENO); dup2(fd[1], STDERR_FILENO); close(fd[1]);
        execl("/bin/sh", "sh", "-c", script, (char*)NULL);
        _exit(127);
    } else if (pid > 0) {
        setpgid(pid, pid);
        current_fg_pgid = pid;
        close(fd[1]); int flags = fcntl(fd[0], F_GETFL, 0); if (flags != -1) fcntl(fd[0], F_SETFL, flags | O_NONBLOCK);
        char buf[1024]; ssize_t n; char line[1024]; size_t lp=0; int any=0; int stopped=0;
        for (;;) {
            n = read(fd[0], buf, sizeof(buf));
            if (n > 0) { for (ssize_t i = 0; i < n; i++) { if (buf[i] == '\n') { line[lp] = '\0'; append_line(t, line); any=1; lp = 0; draw_terminal(); } else if (lp + 1 < sizeof(line)) line[lp++] = buf[i]; } }
            while (XPending(display) > 0) { XEvent ev; XNextEvent(display, &ev); if (ev.type == KeyPress) { KeySym ks; char kb[8]; XLookupString(&ev.xkey, kb, sizeof(kb), &ks, NULL); if ((ev.xkey.state & ControlMask) && (ks == XK_c || ks == XK_C)) { kill(-pid, SIGINT); append_line(t, "^C"); stopped = 1; break; } if ((ev.xkey.state & ControlMask) && (ks == XK_z || ks == XK_Z)) { kill(-pid, SIGTSTP); if (t->proc_count < MAX_JOBS) { t->procs[t->proc_count].pid = pid; strncpy(t->procs[t->proc_count].cmd, script, sizeof(t->procs[t->proc_count].cmd)-1); t->proc_count++; } kill(-pid, SIGCONT); append_line(t, "[moved to background]"); draw_terminal(); stopped = 1; break; } } if (ev.type == Expose) { draw_terminal(); } else if (ev.type == ButtonPress) { handle_tab_click(ev.xbutton.x, ev.xbutton.y, ev.xbutton.button); } else if (ev.type == MotionNotify) { handle_motion_drag(ev.xmotion.x, ev.xmotion.y); } }
            if (stopped) break; if (n == 0) break; if (n < 0 && errno != EINTR && errno != EAGAIN) break; if (n < 0) { struct timespec ts = {0, 5*1000*1000}; nanosleep(&ts, NULL); }
        }
        if (lp && !stopped) { line[lp] = '\0'; append_line(t, line); any=1; }
        close(fd[0]); if (!stopped) { int status; waitpid(pid, &status, 0); if (!any && WIFEXITED(status) && WEXITSTATUS(status) == 127) { append_line(t, "error: command not found or failed to execute"); } }
        draw_terminal();
        current_fg_pgid = 0;
    } else { append_line(t, "error: fork failed"); }
}

void execute_pipeline(Tab *t, const char *cmd_line, int background) {
    // Manual pipeline implementation using pipe()/fork()/execvp()
    char stages[MAX_STAGES][MAX_LINE_LEN]; int nst = 0;
    if (!split_pipeline(cmd_line, stages, &nst) || nst <= 0) { return; }

    int pipes[MAX_STAGES-1][2];
    for (int i=0; i<nst-1; ++i) if (pipe(pipes[i]) == -1) { append_line(t, "pipe failed"); return; }

    pid_t pids[MAX_STAGES]; memset(pids, 0, sizeof(pids));
    pid_t pgid = 0;
    int capture_pipe[2] = {-1,-1};
    if (!background) {
        if (pipe(capture_pipe) == -1) { append_line(t, "pipe failed"); return; }
    }

    for (int i=0; i<nst; ++i) {
        pid_t pid = fork();
        if (pid == 0) {
            // child
            if (pgid == 0) setpgid(0, 0); else setpgid(0, pgid);
            // connect stdin/stdout
            if (i > 0) { dup2(pipes[i-1][0], STDIN_FILENO); }
            if (i < nst-1) { dup2(pipes[i][1], STDOUT_FILENO); dup2(pipes[i][1], STDERR_FILENO); }
            else if (!background) { dup2(capture_pipe[1], STDOUT_FILENO); dup2(capture_pipe[1], STDERR_FILENO); }
            // close all fds
            for (int k=0;k<nst-1;k++){ if (pipes[k][0]!=-1) close(pipes[k][0]); if (pipes[k][1]!=-1) close(pipes[k][1]); }
            if (!background) { if (capture_pipe[0]!=-1) close(capture_pipe[0]); if (capture_pipe[1]!=-1) close(capture_pipe[1]); }
            // exec stage
            char tmp[MAX_LINE_LEN]; strncpy(tmp, stages[i], sizeof(tmp)-1); tmp[sizeof(tmp)-1]='\0';
            char *argv[MAX_ARGS]; int ac=0; char *tok=strtok(tmp," "); while (tok && ac<MAX_ARGS-1){ argv[ac++]=tok; tok=strtok(NULL," "); } argv[ac]=NULL;
            execvp(argv[0], argv); _exit(127);
        } else if (pid > 0) {
            if (pgid == 0) { pgid = pid; setpgid(pid, pid); } else setpgid(pid, pgid);
            pids[i] = pid;
        } else { append_line(t, "fork failed"); }
        if (i > 0) { close(pipes[i-1][0]); pipes[i-1][0] = -1; }
        if (i < nst-1) { close(pipes[i][1]); }
    }

    // close remaining pipe ends in parent
    for (int k=0;k<nst-1;k++){ if (pipes[k][0]!=-1) close(pipes[k][0]); if (pipes[k][1]!=-1) close(pipes[k][1]); }

    if (background) {
        // record the group as a job by tracking the group leader pid
        if (t->proc_count < MAX_JOBS) { t->procs[t->proc_count].pid = pgid; strncpy(t->procs[t->proc_count].cmd, cmd_line, sizeof(t->procs[t->proc_count].cmd)-1); t->proc_count++; }
        char msg[256]; snprintf(msg, sizeof(msg), "Started background pipeline [PGID: %d]", (int)pgid); append_line(t, msg); draw_terminal();
        return;
    }

    // Foreground: capture last stage output and wait for all
    current_fg_pgid = pgid;
    close(capture_pipe[1]);
    int flags = fcntl(capture_pipe[0], F_GETFL, 0); if (flags != -1) fcntl(capture_pipe[0], F_SETFL, flags | O_NONBLOCK);
    char buf[1024]; ssize_t n; char line[1024]; size_t lp=0; int any=0; int stopped=0;
    for (;;) {
        n = read(capture_pipe[0], buf, sizeof(buf));
        if (n > 0) { for (ssize_t i = 0; i < n; i++) { if (buf[i] == '\n') { line[lp] = '\0'; append_line(t, line); any=1; lp = 0; draw_terminal(); } else if (lp + 1 < sizeof(line)) line[lp++] = buf[i]; } }
        while (XPending(display) > 0) {
            XEvent ev; XNextEvent(display, &ev);
            if (ev.type == KeyPress) {
                KeySym ks; char kb[8]; XLookupString(&ev.xkey, kb, sizeof(kb), &ks, NULL);
                if ((ev.xkey.state & ControlMask) && (ks == XK_c || ks == XK_C)) {
                    kill(-pgid, SIGINT);
                    append_line(t, "^C");
                    stopped = 1;
                    break;
                }
                if ((ev.xkey.state & ControlMask) && (ks == XK_z || ks == XK_Z)) {
                    kill(-pgid, SIGTSTP);
                    if (t->proc_count < MAX_JOBS) { t->procs[t->proc_count].pid = pgid; strncpy(t->procs[t->proc_count].cmd, cmd_line, sizeof(t->procs[t->proc_count].cmd)-1); t->proc_count++; }
                    kill(-pgid, SIGCONT);
                    append_line(t, "[moved to background]"); draw_terminal();
                    stopped = 1; break;
                }
            }
            if (ev.type == Expose) { draw_terminal(); }
            else if (ev.type == ButtonPress) { handle_tab_click(ev.xbutton.x, ev.xbutton.y, ev.xbutton.button); }
            else if (ev.type == MotionNotify) { handle_motion_drag(ev.xmotion.x, ev.xmotion.y); }
        }
        if (stopped) break;
        // exit when pipe EOF
        if (n == 0) break; if (n < 0 && errno != EINTR && errno != EAGAIN) break; if (n < 0) { struct timespec ts = {0, 5*1000*1000}; nanosleep(&ts, NULL); }
    }
    if (lp && !stopped) { line[lp] = '\0'; append_line(t, line); any=1; }
    close(capture_pipe[0]);
    if (!stopped) {
        // wait all children
        for (int i=0;i<nst;i++) waitpid(pids[i], NULL, 0);
    }
    current_fg_pgid = 0;
    draw_terminal();
}

void run_command(const char *cmd_line, Tab *t) {
    if (!cmd_line || !t) return; if (!*cmd_line) return;
    char prefix[512];
    get_prompt(prefix, sizeof(prefix));
    const char *p = cmd_line; int first = 1;
    while (p && *p) {
        const char *nl = strchr(p, '\n');
        if (!nl) { char line[MAX_LINE_LEN + 32]; if (first) snprintf(line, sizeof(line), "%s%s", prefix, p); else snprintf(line, sizeof(line), "%*s%s", (int)strlen(prefix), "", p); append_line(t, line); break; }
        else { char seg[MAX_LINE_LEN]; size_t sl = (size_t)(nl - p); if (sl > sizeof(seg)-1) sl = sizeof(seg)-1; memcpy(seg, p, sl); seg[sl] = '\0'; char line[MAX_LINE_LEN + 32]; if (first) snprintf(line, sizeof(line), "%s%s", prefix, seg); else snprintf(line, sizeof(line), "%*s%s", (int)strlen(prefix), "", seg); append_line(t, line); first = 0; p = nl + 1; }
    }
    history_add(t, cmd_line);

    if (strncmp(cmd_line, "multiWatch", 10) == 0) { char cmds[MAX_WATCH_CMDS][256]; int n=0; if (!parse_multiwatch(cmd_line, cmds, &n) || n<=0) { append_line(t, "Usage: multiWatch [\"cmd1\", \"cmd2\", ...]"); return; } start_multiwatch(t, cmds, n); return; }

    // Handle cd specially to support quoted paths with spaces
    if (strncmp(cmd_line, "cd ", 3) == 0 || strcmp(cmd_line, "cd") == 0) {
        const char *arg_start = cmd_line + 2;
        while (*arg_start == ' ' || *arg_start == '\t') arg_start++;
        
        char path[PATH_MAX];
        if (*arg_start == '\0') {
            // cd with no argument - go to HOME
            const char *home = getenv("HOME");
            if (!home) home = ".";
            strncpy(path, home, sizeof(path)-1);
            path[sizeof(path)-1] = '\0';
        } else if (*arg_start == '"' || *arg_start == '\'') {
            // Quoted path
            char quote = *arg_start++;
            size_t i = 0;
            while (*arg_start && *arg_start != quote && i < sizeof(path)-1) {
                path[i++] = *arg_start++;
            }
            path[i] = '\0';
        } else {
            // Unquoted path (may still have spaces if using shell escapes)
            strncpy(path, arg_start, sizeof(path)-1);
            path[sizeof(path)-1] = '\0';
            // Trim trailing whitespace
            size_t len = strlen(path);
            while (len > 0 && (path[len-1] == ' ' || path[len-1] == '\t' || path[len-1] == '\n')) {
                path[--len] = '\0';
            }
        }
        
        // Handle tilde expansion
        char expanded[PATH_MAX];
        const char *final_path = path;
        if (path[0] == '~') {
            const char *home = getenv("HOME");
            if (home && (path[1] == '\0' || path[1] == '/')) {
                snprintf(expanded, sizeof(expanded), "%s%s", home, path + 1);
                final_path = expanded;
            }
        }
        
        if (chdir(final_path) != 0) {
            char err[MAX_LINE_LEN];
            snprintf(err, sizeof(err), "cd: %s: %s", final_path, strerror(errno));
            append_line(t, err);
        }
        return;
    }

    {
        const char *end = cmd_line + strlen(cmd_line);
        while (end > cmd_line && (end[-1] == ' ' || end[-1] == '\t')) end--;
        int bg = 0; if (end > cmd_line && end[-1] == '&') { bg = 1; }
        if (bg) {
            char stripped[MAX_LINE_LEN]; size_t L = (size_t)(end - cmd_line);
            while (L && (cmd_line[L-1] == '&' || cmd_line[L-1] == ' ' || cmd_line[L-1] == '\t')) L--;
            while (L && (cmd_line[L-1] == ' ' || cmd_line[L-1] == '\t')) L--;
            if (L >= sizeof(stripped)) L = sizeof(stripped) - 1;
            memcpy(stripped, cmd_line, L); stripped[L] = '\0';
            execute_pipeline(t, stripped, 1);
            return;
        }
    }

    if (strpbrk(cmd_line, "\"'|<>;\n\t")) { execute_sh(t, cmd_line); return; }

    char tmp[MAX_LINE_LEN]; strncpy(tmp, cmd_line, sizeof(tmp)-1); tmp[sizeof(tmp)-1]='\0'; char *argv[MAX_ARGS]; int ac=0; int background=0; char *tok=strtok(tmp," "); while (tok && ac<MAX_ARGS-1){ argv[ac++]=tok; tok=strtok(NULL," "); } argv[ac]=NULL; if (ac==0) return; if (strcmp(argv[ac-1],"&")==0){ background=1; argv[ac-1]=NULL; ac--; }

    if (strcmp(argv[0], "pwd") == 0) { char cwd[1024]; if (getcwd(cwd, sizeof(cwd))) append_line(t, cwd); else append_line(t, "pwd: error"); return; }
    if (strcmp(argv[0], "jobs") == 0) { list_jobs(t); return; }
    if (strcmp(argv[0], "kill") == 0) { if (ac < 2) { append_line(t, "kill: usage: kill <pid>"); return; } pid_t k = (pid_t)atoi(argv[1]); if (k > 0 && kill(k, SIGTERM) == 0) { remove_proc(t, k); append_line(t, "Sent SIGTERM"); } else { append_line(t, "Failed to kill or not found"); } return; }
    if (strcmp(argv[0], "bg") == 0) {
        if (ac < 2) { append_line(t, "bg: usage: bg <pid>"); return; }
        pid_t k = (pid_t)atoi(argv[1]);
        if (k > 0) { if (kill(-k, SIGCONT) == 0 || kill(k, SIGCONT) == 0) { char msg[128]; snprintf(msg, sizeof(msg), "Continued PID %d in background", (int)k); append_line(t, msg); } else { append_line(t, "bg: failed to continue"); } }
        return;
    }
    if (strcmp(argv[0], "fg") == 0) {
        if (ac < 2) { append_line(t, "fg: usage: fg <pid>"); return; }
        pid_t k = (pid_t)atoi(argv[1]); if (k <= 0) { append_line(t, "fg: invalid pid"); return; }
        if (kill(-k, SIGCONT) != 0) kill(k, SIGCONT);
        int status; for (;;) {
            int r = waitpid(k, &status, WNOHANG); if (r == k) { remove_proc(t, k); break; }
            while (XPending(display) > 0) { XEvent ev; XNextEvent(display, &ev); if (ev.type == KeyPress) { KeySym ks; char kb[8]; XLookupString(&ev.xkey, kb, sizeof(kb), &ks, NULL); if ((ev.xkey.state & ControlMask) && (ks == XK_z || ks == XK_Z)) { kill(-k, SIGTSTP); if (t->proc_count < MAX_JOBS) { t->procs[t->proc_count].pid = k; snprintf(t->procs[t->proc_count].cmd, sizeof(t->procs[t->proc_count].cmd), "[fg job] %d", (int)k); t->proc_count++; } kill(-k, SIGCONT); append_line(t, "[moved to background]"); draw_terminal(); return; } } if (ev.type == Expose) { draw_terminal(); } else if (ev.type == ButtonPress) { handle_tab_click(ev.xbutton.x, ev.xbutton.y, ev.xbutton.button); } else if (ev.type == MotionNotify) { handle_motion_drag(ev.xmotion.x, ev.xmotion.y); } }
            struct timespec ts = {0, 5*1000*1000}; nanosleep(&ts, NULL);
        }
        if (WIFEXITED(status) && WEXITSTATUS(status) == 127) append_line(t, "error: command not found or failed to execute"); else append_line(t, "[completed]"); draw_terminal(); return;
    }
    if (strcmp(argv[0], "history") == 0) { show_history(t); return; }
    if (strcmp(argv[0], "clear") == 0) { t->line_count = 0; t->scroll = 0; draw_terminal(); return; }
    if (strcmp(argv[0], "exit") == 0) { app_running = 0; return; }
    if (strcmp(argv[0], "help") == 0) {
        append_line(t, "=== MyTerm Help ===");
        append_line(t, "");
        append_line(t, "BUILTIN COMMANDS:");
        append_line(t, "  cd <dir>       - Change directory (supports ~, spaces with quotes)");
        append_line(t, "  pwd            - Print working directory");
        append_line(t, "  jobs           - List background jobs");
        append_line(t, "  bg <pid>       - Resume stopped job in background");
        append_line(t, "  fg <pid>       - Bring background job to foreground");
        append_line(t, "  kill <pid>     - Terminate a process");
        append_line(t, "  history        - Show command history (numbered list)");
        append_line(t, "  clear          - Clear screen");
        append_line(t, "  help           - Show this help message");
        append_line(t, "  exit           - Exit terminal");
        append_line(t, "");
        append_line(t, "FEATURES:");
        append_line(t, "  Redirection    - Use < for input, > for output (e.g., cat < file.txt > out.txt)");
        append_line(t, "  Pipes          - Chain commands with | (e.g., ls | grep txt | wc -l)");
        append_line(t, "  Background     - Add & to run in background (e.g., sleep 10 &)");
        append_line(t, "  multiWatch     - Run commands in parallel: multiWatch [\"cmd1\", \"cmd2\", ...]");
        append_line(t, "");
        append_line(t, "KEYBOARD SHORTCUTS:");
        append_line(t, "  Ctrl+C         - Interrupt/stop foreground job");
        append_line(t, "  Ctrl+Z         - Suspend job and move to background");
        append_line(t, "  Ctrl+R         - Search command history (type to search)");
        append_line(t, "  Ctrl+A         - Jump to start of line");
        append_line(t, "  Ctrl+E         - Jump to end of line");
        append_line(t, "  Ctrl+L         - Clear screen (same as 'clear' command)");
        append_line(t, "  Ctrl+T         - Create new tab");
        append_line(t, "  Ctrl+W         - Close current tab");
        append_line(t, "  Ctrl+Q         - Quit terminal");
        append_line(t, "  Tab            - Autocomplete file/directory names");
        append_line(t, "  Shift+Enter    - Insert newline (multiline input)");
        append_line(t, "  Ctrl+Shift+V   - Paste from clipboard");
        append_line(t, "  Ctrl+Shift+C   - Copy to clipboard");
        append_line(t, "");
        append_line(t, "INTERFACE:");
        append_line(t, "  Click '+'      - Create new tab (or Ctrl+T)");
        append_line(t, "  Click 'X'      - Close current tab (or Ctrl+W)");
        append_line(t, "  Mouse Wheel    - Scroll output");
        append_line(t, "  PageUp/PageDown- Scroll by page");
        append_line(t, "  Home/End       - Jump to top/bottom of output");
        append_line(t, "");
        append_line(t, "EXAMPLES:");
        append_line(t, "  ls -la | grep txt > results.txt");
        append_line(t, "  multiWatch [\"date\", \"whoami\", \"pwd\"]");
        append_line(t, "  cat < input.txt | sort | uniq > output.txt");
        append_line(t, "  sleep 30 &  # then use 'jobs', 'fg', 'bg' to manage");
        return;
    }

    if (strchr(cmd_line, '|')) { execute_pipeline(t, cmd_line, background); return; }
    execute_simple(t, argv, background);
}
