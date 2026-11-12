#define _GNU_SOURCE

#include "oshi.h"

void abAppend(struct abuf *ab, const char *s, int len) {
  if (ab->len + len > ab->cap) {
    int cap = ab->cap ? ab->cap * 2 : 4096;
    while (cap < ab->len + len) cap *= 2;
    ab->b = xrealloc(ab->b, cap);
    ab->cap = cap;
  }
  if (len > 0) memcpy(&ab->b[ab->len], s, len);
  ab->len += len;
}

void abFree(struct abuf *ab) {
  free(ab->b);
}

static int editorGutterWidth(void) {
  if (!E.ruler) return 0;
  int digits = 1;
  int n = E.numrows;
  while (n >= 10) {
    n /= 10;
    digits++;
  }
  int w = E.ruler_width > 0 ? E.ruler_width : digits;
  if (w < digits) w = digits;
  if (w > 64) w = 64;
  if (w + 2 > E.screencols) w = E.screencols - 2;
  if (w < 1) return 0;
  return w + 1;
}

void editorScroll(void) {
  int width = E.screencols - E.gutter;
  if (width < 1) width = 1;

  E.rx = 0;
  if (E.cy < E.numrows) {
    E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
  }

  if (E.cy < E.rowoff) {
    E.rowoff = E.cy;
  }
  if (E.cy >= E.rowoff + E.screenrows) {
    E.rowoff = E.cy - E.screenrows + 1;
  }
  int inner = width > 2 ? width - 2 : 1;
  if (E.rx < E.coloff + 1) {
    E.coloff = E.rx > 0 ? E.rx - 1 : 0;
  }
  if (E.rx > E.coloff + inner) {
    E.coloff = E.rx - inner;
  }
  if (E.coloff < 0) E.coloff = 0;
}

static void editorDrawCursorCell(struct abuf *ab, char ch) {
  abAppend(ab, "\x1b[7m", 4);
  abAppend(ab, &ch, 1);
  abAppend(ab, "\x1b[27m", 5);
}

static void editorDrawScrollMark(struct abuf *ab, char mark, int covered) {
  if (covered) {
    return;
  }
  abAppend(ab, E.col_str[COL_SCROLLMARK_BG],
           (int)strlen(E.col_str[COL_SCROLLMARK_BG]));
  abAppend(ab, E.col_str[COL_SCROLLMARK_FG],
           (int)strlen(E.col_str[COL_SCROLLMARK_FG]));
  abAppend(ab, &mark, 1);
  abAppend(ab, "\x1b[0m", 4);
  abAppend(ab, E.col_str[COL_TEXT_FG], (int)strlen(E.col_str[COL_TEXT_FG]));
}

static void editorDrawRowText(struct abuf *ab, erow *row, int filerow, int avail) {
  int sel_from = 0, sel_to = 0, sel_eol = 0;
  int sel_on = editorRowSelection(row, filerow, &sel_from, &sel_to);
  if (sel_on) {
    int sy, sx, ey, ex;
    editorSelectionRange(&sy, &sx, &ey, &ex);
    sel_eol = filerow < ey;
  }
  int block = filerow == E.cy && !E.in_prompt && E.cursor_style == 2;
  int qlen = (find_on && find_q[0]) ? (int)strlen(find_q) : 0;
  char *m = qlen ? editorIstrstr(row->chars, find_q) : NULL;

  int roww = row->size > 0 ? editorRowCxToRx(row, row->size) : 0;
  int has_right = avail > 0 && roww > E.coloff + avail;
  int has_left = E.coloff > 0 && roww > 0;
  int left = E.coloff + (has_left ? 1 : 0);
  int right = E.coloff + avail - (has_right ? 1 : 0);
  if (right < left) right = left;
  int col = 0, b = 0;
  int cur_cls = CL_PLAIN;
  const unsigned char *cls = editorHlClasses();
  int hit_right = 0;
  int firstvis_cx = editorRowRxToCx(row, E.coloff);

  if (has_left) editorDrawScrollMark(ab, '<',
    (filerow == E.cy && E.rx == E.coloff) ||
    (sel_on && sel_from <= firstvis_cx && sel_to > firstvis_cx));

  while (b < row->size) {
    int e = editorCharEnd(row, b);
    int w = editorCellWidth(row, b, col);
    if (col >= right) break;
    if (col + w > left) {
      int in_sel, on_cur, in_match, rev;
      while (m && (m - row->chars) + qlen <= b)
        m = editorIstrstr(m + qlen, find_q);
      in_sel = sel_on && b >= sel_from && b < sel_to;
      on_cur = block && b == E.cx;
      in_match = qlen && m && b >= (m - row->chars) &&
                 b < (m - row->chars) + qlen;
      rev = in_sel || on_cur;
      int hl = in_match && !rev;
      if (rev && col + w >= right) hit_right = 1;

      editorHlSwitch(ab, &cur_cls,
                     (!rev && !in_match && cls && b < row->size) ? cls[b] : CL_PLAIN);
      if (hl && E.col_str[COL_FIND][0])
        abAppend(ab, E.col_str[COL_FIND], (int)strlen(E.col_str[COL_FIND]));
      if (rev) abAppend(ab, "\x1b[7m", 4);

      if (row->chars[b] == '\t' || col < left || col + w > right) {
        int k;
        for (k = 0; k < w; k++)
          if (col + k >= left && col + k < right) abAppend(ab, " ", 1);
      } else if ((unsigned char)row->chars[b] < 32 ||
                 (unsigned char)row->chars[b] == 127) {
        abAppend(ab, "?", 1);
      } else {
        int len, cp = utf8Decode((const unsigned char *)row->chars + b,
                                 row->size - b, &len);
        if (cp >= 0x80 && cp < 0xA0)
          abAppend(ab, "?", 1);
        else if (cp == 0xFFFD && len == 1)
          abAppend(ab, "\xEF\xBF\xBD", 3);
        else
          abAppend(ab, row->chars + b, e - b);
      }
      if (rev) abAppend(ab, "\x1b[27m", 5);
      if (hl) abAppend(ab, "\x1b[49m", 5);
    }
    col += w;
    b = e;
  }
  editorHlSwitch(ab, &cur_cls, CL_PLAIN);
  if ((sel_eol || (block && E.cx >= row->size)) && col >= left && col < right)
    editorDrawCursorCell(ab, ' ');

  if (has_right) editorDrawScrollMark(ab, '>',
    hit_right || ((sel_eol || (block && E.cx >= row->size)) && col >= right - 1 && col < right));
}

static void editorDrawRows(struct abuf *ab) {
  static erow blank = { 0, "" };
  int y;
  int avail = E.screencols - E.gutter;
  if (avail < 0) avail = 0;
  int nvis = E.numrows > 0 ? E.numrows : 1;
  int hstate = editorHlBegin();
  for (y = 0; y < E.screenrows; y++) {
    int filerow = y + E.rowoff;
    if (filerow > nvis) {
      abAppend(ab, "\x1b[K", 3);
      abAppend(ab, "\r\n", 2);
      continue;
    }
    if (filerow == nvis) {
      abAppend(ab, E.col_str[COL_TILDE_FG],
               (int)strlen(E.col_str[COL_TILDE_FG]));
      abAppend(ab, "~", 1);
      abAppend(ab, "\x1b[0m", 4);
      abAppend(ab, "\x1b[K", 3);
      abAppend(ab, "\r\n", 2);
      continue;
    }
    if (E.gutter > 0) {
      char num[80];
      int n = snprintf(num, sizeof(num), "%*d", E.gutter - 1, filerow + 1);
      int cur = filerow == E.cy;
      const char *bg = cur ? E.col_str[COL_GUTTER_CURSOR_BG] : E.col_str[COL_GUTTER_BG];
      const char *fg = cur ? E.col_str[COL_GUTTER_CURSOR_FG] : E.col_str[COL_GUTTER_FG];
      if (bg[0]) abAppend(ab, bg, (int)strlen(bg));
      abAppend(ab, fg, (int)strlen(fg));
      abAppend(ab, num, n);
      abAppend(ab, " ", 1);
      abAppend(ab, "\x1b[0m", 4);
    }
    abAppend(ab, E.col_str[COL_TEXT_FG], (int)strlen(E.col_str[COL_TEXT_FG]));
    if (editorHlActive() && filerow < E.numrows)
      editorHlRow(&E.row[filerow], &hstate);
    editorDrawRowText(ab, filerow < E.numrows ? &E.row[filerow] : &blank,
                      filerow, avail);
    editorHlEnd();
    abAppend(ab, "\x1b[0m", 4);
    abAppend(ab, "\x1b[K", 3);
    abAppend(ab, "\r\n", 2);
  }
}

static void editorDrawStatusBar(struct abuf *ab) {
  abAppend(ab, E.col_str[COL_STATUS_BG], (int)strlen(E.col_str[COL_STATUS_BG]));
  abAppend(ab, E.col_str[COL_STATUS_FG], (int)strlen(E.col_str[COL_STATUS_FG]));
  char name[512], left[1024], right[512];
  char suffix[300];
  suffix[0] = '\0';
  const char *fn = E.filename ? E.filename : "[No Name]";
  int fnlen = (int)strlen(fn);

  int total = E.numrows > 0 ? E.numrows : 1;
  int cur = E.cy + 1 > total ? total : E.cy + 1;
  int prompt = E.in_prompt;
  long now = editorNowMs();
  int quit_shown = E.quit_confirm && now - E.quit_time < OSHI_STATUS_MS;
  int find_shown = find_on && find_q[0] && now - find_time < OSHI_STATUS_MS;
  int cfg_shown = E.config_err[0] && now - E.config_err_time < OSHI_STATUS_MS;
  int hasmsg = !quit_shown && !find_shown && !cfg_shown && E.statusmsg[0] &&
               (now - E.statusmsg_time < OSHI_STATUS_MS);
  long notice_t = -1;
  int len = 0, rlen = 0, slen = 0;
  if (prompt) {
    int cap = E.screencols - 24;
    if (cap > 127) cap = 127;
    if (cap < 9) cap = 9;
    int skip = editorFitTail(fn, fnlen, cap - 1);
    snprintf(name, sizeof(name), "%s%s", skip > 0 ? "<" : "", fn + skip);
    rlen = snprintf(right, sizeof(right), "%s", name);
    len = snprintf(left, sizeof(left), "%s", E.statusmsg);
  } else if (quit_shown) {
    char keyname[16];
    editorKeyName(E.binds[CMD_QUIT], keyname, sizeof(keyname));
    slen = snprintf(suffix, sizeof(suffix), "  Modified press %s again", keyname);
    rlen = snprintf(right, sizeof(right), "%d/%d", cur, E.rx + 1);
    notice_t = E.quit_time;
  } else if (find_shown) {
    int current, found = editorFindCount(&current);
    slen = snprintf(suffix, sizeof(suffix), "%s", E.dirty ? " (modified)" : "");
    rlen = snprintf(right, sizeof(right), "%d/%d", current, found);
    notice_t = find_time;
  } else if (cfg_shown) {
    slen = snprintf(suffix, sizeof(suffix), "  %s", E.config_err);
    rlen = snprintf(right, sizeof(right), "%d/%d", cur, E.rx + 1);
    notice_t = E.config_err_time;
  } else if (hasmsg) {
    slen = snprintf(suffix, sizeof(suffix), "%s  %s", E.dirty ? " (modified)" : "",
                    E.statusmsg);
    rlen = snprintf(right, sizeof(right), "%d/%d", cur, E.rx + 1);
    notice_t = E.statusmsg_time;
  } else {
    slen = snprintf(suffix, sizeof(suffix), "%s", E.dirty ? " (modified)" : "");
    rlen = snprintf(right, sizeof(right), "%d/%d", cur, E.rx + 1);
  }
  if (notice_t >= 0) {
    E.notice_pending = 1;
    E.notice_deadline = notice_t + OSHI_STATUS_MS;
  } else {
    E.notice_pending = 0;
  }
  if (slen >= (int)sizeof(suffix)) slen = (int)sizeof(suffix) - 1;
  if (len >= (int)sizeof(left)) len = (int)sizeof(left) - 1;
  if (rlen >= (int)sizeof(right)) rlen = (int)sizeof(right) - 1;

  int rw = editorStrWidth(right, rlen);
  if (rw > E.screencols) {
    rlen = editorFitHead(right, rlen, E.screencols);
    rw = editorStrWidth(right, rlen);
  }
  int block = prompt && E.cursor_style == 2;
  int avail = E.screencols - rw - (block ? 1 : 0);
  if (avail < 0) avail = 0;
  if (!prompt) {
    int budget = avail - editorStrWidth(suffix, slen);
    if (budget < 16) budget = 16;
    if (budget > 127) budget = 127;
    int skip = editorFitTail(fn, fnlen, budget - 1);
    snprintf(name, sizeof(name), "%s%s", skip > 0 ? "<" : "", fn + skip);
    len = snprintf(left, sizeof(left), "%s%s", name, suffix);
    if (len >= (int)sizeof(left)) len = (int)sizeof(left) - 1;
  }
  const char *lp = left;
  if (editorStrWidth(left, len) > avail) {
    if (prompt) {
      int s = editorFitTail(left, len, avail);
      lp = left + s;
      len -= s;
    } else {
      len = editorFitHead(left, len, avail);
    }
  }
  int lw = editorStrWidth(lp, len);
  abAppend(ab, lp, len);
  int used = lw;
  if (block) {
    editorDrawCursorCell(ab, ' ');
    used++;
  }
  E.prompt_col = lw + 1;
  for (; used < E.screencols - rw; used++)
    abAppend(ab, " ", 1);
  abAppend(ab, right, rlen);
  abAppend(ab, "\x1b[0m", 4);
}

void editorRefreshScreen(void) {
  E.gutter = editorGutterWidth();
  editorScroll();

  struct abuf ab = ABUF_INIT;

  abAppend(&ab, "\x1b[?25l", 6);
  if (E.redraw) {
    abAppend(&ab, "\x1b[2J", 4);
    E.redraw = 0;
  }
  abAppend(&ab, "\x1b[H", 3);

  editorDrawRows(&ab);
  editorDrawStatusBar(&ab);

  if (E.cursor_style == 6) {
    char buf[32];
    int row, col;
    if (E.in_prompt) {
      row = E.screenrows + 1;
      col = E.prompt_col;
      if (col > E.screencols) col = E.screencols;
    } else {
      row = E.cy - E.rowoff + 1;
      col = E.rx - E.coloff + E.gutter + 1;
      if (col < 1) col = 1;
    }
    snprintf(buf, sizeof(buf), "\x1b[?25h\x1b[%d;%dH", row, col);
    abAppend(&ab, buf, (int)strlen(buf));
  }

  ssize_t r = write(STDOUT_FILENO, ab.b, ab.len);
  (void)r;
  abFree(&ab);
}

void editorSetStatusMessage(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
  va_end(ap);
  E.statusmsg_time = editorNowMs();
}
