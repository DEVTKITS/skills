/* ------------------------------------------------------------------ */
/* Ausgabepuffer & Terminal                                            */
/* ------------------------------------------------------------------ */

static char ob[1 << 18];
static size_t olen;

static void o(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    if (olen < sizeof ob - 1)
        olen += (size_t)vsnprintf(ob + olen, sizeof ob - olen, fmt, ap);
    va_end(ap);
}

static void oflush(void) {
    if (olen) { ssize_t r = write(STDOUT_FILENO, ob, olen); (void)r; }
    olen = 0;
}

static void term_size(void) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        A.cols = ws.ws_col; A.rows = ws.ws_row;
    } else { A.cols = 80; A.rows = 24; }
    if (A.cols > 200) A.cols = 200;
}

static void raw_off(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_tio);
    const char *s = "\x1b[?2026l\x1b[?25h\x1b[0m\x1b[?1049l";
    ssize_t r = write(STDOUT_FILENO, s, strlen(s)); (void)r;
}

static void raw_enter(void) {
    struct termios t = orig_tio;
    t.c_lflag &= (tcflag_t)~(ECHO | ICANON | ISIG);
    t.c_iflag &= (tcflag_t)~(IXON | ICRNL);
    t.c_cc[VMIN] = 1; t.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &t);
    const char *s = "\x1b[?1049h\x1b[?25l";
    ssize_t r = write(STDOUT_FILENO, s, strlen(s)); (void)r;
}

static void raw_on(void) {
    tcgetattr(STDIN_FILENO, &orig_tio);
    atexit(raw_off);
    raw_enter();
}

/* TUI fuer interaktive Kindprozesse (ssh, Shell) sauber verlassen/wiederholen */
static void tui_suspend(void) {
    const char *s = "\x1b[?2026l\x1b[0m\x1b[?25h\x1b[?1049l";
    ssize_t r = write(STDOUT_FILENO, s, strlen(s)); (void)r;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_tio);
}

static void tui_resume(void) { raw_enter(); }

static int run_interactive(const char *cmd) {
    tui_suspend();
    int rc = system(cmd);
    fprintf(stderr, "\n[baukasten] Enter = zurueck / back ");
    char tmp[8]; if (!fgets(tmp, sizeof tmp, stdin)) { /* EOF ok */ }
    tui_resume();
    return rc;
}

static int in_waiting(int ms) {
    fd_set f; struct timeval tv = { 0, ms * 1000 };
    FD_ZERO(&f); FD_SET(STDIN_FILENO, &f);
    return select(STDIN_FILENO + 1, &f, NULL, NULL, &tv) > 0;
}

static int readkey(void) {
    unsigned char c;
    if (read(STDIN_FILENO, &c, 1) != 1) return -1;
    if (c != 27) return c;
    if (!in_waiting(30)) return K_ESC;
    unsigned char b;
    if (read(STDIN_FILENO, &b, 1) != 1) return K_ESC;
    if (b != '[' && b != 'O') return K_ESC;
    if (!in_waiting(30)) return K_ESC;
    unsigned char d;
    if (read(STDIN_FILENO, &d, 1) != 1) return K_ESC;
    switch (d) {
        case 'A': return K_UP; case 'B': return K_DN;
        case 'C': return K_RT; case 'D': return K_LT;
        case '5': case '6': {
            unsigned char t2 = 0;
            if (in_waiting(30)) { if (read(STDIN_FILENO, &t2, 1) != 1) t2 = 0; }
            if (t2 == '~') return d == '5' ? K_PGUP : K_PGDN;
            return 0;
        }
        default:
            /* Rest einer längeren Sequenz (z. B. "3~") verwerfen */
            while (in_waiting(5)) {
                unsigned char x;
                if (read(STDIN_FILENO, &x, 1) != 1) break;
                if (x == '~' || (x >= 'A' && x <= 'Z')) break;
            }
            return 0;
    }
}

/* s auf Breite w kürzen/auffüllen (byte-sicher für UTF-8) */
static void fitpad(char *dst, size_t cap, const char *s, int w) {
    size_t di = 0; int vis = 0;
    for (const char *p = s; *p && di + 4 < cap; p++) {
        if (((unsigned char)*p & 0xC0) != 0x80) {
            if (vis >= w) break;
            vis++;
        }
        dst[di++] = *p;
    }
    while (vis < w && di + 1 < cap) { dst[di++] = ' '; vis++; }
    dst[di] = 0;
}

/* ------------------------------------------------------------------ */
/* Theme                                                               */
/* ------------------------------------------------------------------ */

static void theme_colors(int theme, int contrast, Theme *t) {
    if (contrast) {
        /* Kontrast an: immer tiefschwarz, Text grau, DkZ-Neonrot #fa1e4e */
        t->bg   = "\x1b[48;2;0;0;0m";
        t->fg   = "\x1b[38;2;170;170;170m";
        t->dim  = "\x1b[38;2;105;105;105m";
        t->head = "\x1b[1;38;2;250;30;78m";
        t->hi   = "\x1b[38;2;250;30;78m";
        t->sel  = "\x1b[48;2;250;30;78m\x1b[38;2;0;0;0m\x1b[1m";
        t->box  = "\x1b[38;2;120;120;120m";
        return;
    }
    switch (theme) {
    case TH_MATRIX: /* DkZ-Gruen #00D084 auf Schwarz */
        t->bg   = "\x1b[48;2;0;0;0m";
        t->fg   = "\x1b[38;2;0;208;132m";
        t->dim  = "\x1b[38;2;0;110;70m";
        t->head = "\x1b[1;38;2;140;255;190m";
        t->hi   = "\x1b[38;2;0;208;132m";
        t->sel  = "\x1b[48;2;0;208;132m\x1b[38;2;0;0;0m";
        t->box  = "\x1b[38;2;0;90;60m";
        break;
    case TH_COSMO: /* Soft White auf Blauschwarz, DkZ-Blau + Pink */
        t->bg   = "\x1b[48;2;5;10;20m";
        t->fg   = "\x1b[38;2;246;246;247m";
        t->dim  = "\x1b[38;2;120;140;170m";
        t->head = "\x1b[1;38;2;85;172;238m";
        t->hi   = "\x1b[38;2;236;72;153m";
        t->sel  = "\x1b[48;2;85;172;238m\x1b[38;2;5;10;20m";
        t->box  = "\x1b[38;2;60;80;110m";
        break;
    case TH_HELL:
        t->bg   = "\x1b[48;2;246;246;243m";
        t->fg   = "\x1b[38;2;42;44;50m";
        t->dim  = "\x1b[38;2;125;128;136m";
        t->head = "\x1b[1;38;2;28;82;178m";
        t->hi   = "\x1b[38;2;148;42;148m";
        t->sel  = "\x1b[48;2;198;214;244m\x1b[38;2;12;14;40m";
        t->box  = "\x1b[38;2;165;168;176m";
        break;
    default: /* TH_DUNKEL */
        t->bg   = "\x1b[48;2;24;26;32m";
        t->fg   = "\x1b[38;2;220;223;228m";
        t->dim  = "\x1b[38;2;130;136;146m";
        t->head = "\x1b[1;38;2;255;198;110m";
        t->hi   = "\x1b[38;2;110;220;220m";
        t->sel  = "\x1b[48;2;72;84;118m\x1b[38;2;255;255;255m";
        t->box  = "\x1b[38;2;95;100;112m";
    }
}

static void apply_theme(void) { theme_colors(A.theme, A.contrast, &th); }

/* eine Bildschirmzeile: Hintergrund setzen, Inhalt, bis Zeilenende füllen */
static void ln(const char *fmt, ...) {
    o("%s%s", th.bg, th.fg);
    va_list ap; va_start(ap, fmt);
    if (olen < sizeof ob - 1)
        olen += (size_t)vsnprintf(ob + olen, sizeof ob - olen, fmt, ap);
    va_end(ap);
    o("\x1b[K\r\n");
}

