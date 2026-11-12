#define _GNU_SOURCE

#include "oshi.h"

static int editorIcmp(const char *s1, const char *s2, size_t n) {
  size_t i;
  for (i = 0; i < n; i++)
    if (tolower((unsigned char)s1[i]) != tolower((unsigned char)s2[i])) return 0;
  return 1;
}

char *editorIstrstr(const char *hay, const char *needle) {
  size_t hl = strlen(hay), nl = strlen(needle);
  size_t i;
  for (i = 0; i + nl <= hl; i++) {
    size_t j;
    for (j = 0; j < nl; j++)
      if (tolower((unsigned char)hay[i + j]) !=
          tolower((unsigned char)needle[j])) break;
    if (j == nl) return (char *)hay + i;
  }
  return NULL;
}

char find_q[256];
int find_on;
static int find_row = -1;
static int find_col;
long find_time;

static void editorFindCallback(char *query, int key) {
  static int last_match = -1;
  static int direction = 1;

  snprintf(find_q, sizeof(find_q), "%s", query);
  find_on = query[0] != '\0';

  if (key == '\r' || key == '\x1b') {
    last_match = -1;
    direction = 1;
    find_on = 0;
    return;
  } else if (key == ARROW_RIGHT || key == ARROW_DOWN) {
    direction = 1;
  } else if (key == ARROW_LEFT || key == ARROW_UP) {
    direction = -1;
  } else {
    last_match = -1;
    direction = 1;
  }
  if (last_match == -1) direction = 1;
  int current = last_match;
  int i;
  if (!query[0]) {
    last_match = -1;
    direction = 1;
    return;
  }
  find_row = -1;
  for (i = 0; i < E.numrows; i++) {
    current += direction;
    if (current == -1) current = E.numrows - 1;
    else if (current == E.numrows) current = 0;
    erow *row = &E.row[current];
    char *match = editorIstrstr(row->chars, query);
    if (match) {
      last_match = current;
      E.cy = current;
      E.cx = (int)(match - row->chars);
      find_row = current;
      find_col = E.cx;
      break;
    }
  }
}

static void editorFindStatus(char *query) {
  (void)query;
}

int editorFindCount(int *cur) {
  int total = 0, before = 0, y;
  *cur = 0;
  if (!find_on || !find_q[0] || E.numrows == 0) return 0;
  size_t qlen = strlen(find_q);
  for (y = 0; y < E.numrows; y++) {
    erow *row = &E.row[y];
    char *p = row->chars;
    char *m;
    while ((m = editorIstrstr(p, find_q)) != NULL) {
      total++;
      if (y < find_row || (y == find_row && (m - row->chars) < find_col)) before++;
      p = m + qlen;
    }
  }
  if (find_row >= 0) *cur = before + 1;
  return total;
}

void editorFind(void) {
  int saved_cx = E.cx;
  int saved_cy = E.cy;
  int saved_coloff = E.coloff;
  int saved_rowoff = E.rowoff;
  char *query = editorPrompt("Find: %s", editorFindCallback, editorFindStatus);
  if (query) {
    find_on = 1;
    find_time = editorNowMs();
    free(query);
  } else {
    find_on = 0;
    find_q[0] = '\0';
    find_row = -1;
    E.cx = saved_cx;
    E.cy = saved_cy;
    E.coloff = saved_coloff;
    E.rowoff = saved_rowoff;
  }
}

int editorFindNextMatch(int dir) {
  if (!find_on || !find_q[0] || E.numrows == 0) return 0;
  find_time = editorNowMs();
  size_t qlen = strlen(find_q);
  int fy = -1, fcol = -1;
  int ly = -1, lcol = -1;
  int ay = -1, acol = -1;
  int by = -1, bcol = -1;
  int y;
  for (y = 0; y < E.numrows; y++) {
    erow *row = &E.row[y];
    char *p = row->chars;
    char *m;
    while ((m = editorIstrstr(p, find_q)) != NULL) {
      int col = (int)(m - row->chars);
      if (fy < 0) { fy = y; fcol = col; }
      ly = y; lcol = col;
      if (y > find_row || (y == find_row && col > find_col)) {
        if (ay < 0) { ay = y; acol = col; }
      }
      if (y < find_row || (y == find_row && col < find_col)) {
        by = y; bcol = col;
      }
      p = m + qlen;
    }
  }
  int ny, nx;
  if (dir > 0) {
    if (ay >= 0) { ny = ay; nx = acol; }
    else if (fy >= 0) { ny = fy; nx = fcol; }
    else return 0;
  } else {
    if (by >= 0) { ny = by; nx = bcol; }
    else if (ly >= 0) { ny = ly; nx = lcol; }
    else return 0;
  }
  find_row = ny;
  find_col = nx;
  E.cy = ny;
  E.cx = nx;
  editorScroll();
  editorRefreshScreen();
  return 1;
}

static int editorFindNextReplace(char *query, int srow, int scol, int *fr, int *fc) {
  int qlen = (int)strlen(query);
  int y, c = scol;
  if (qlen == 0 || srow < 0) return 0;
  for (y = srow; y < E.numrows; y++, c = 0) {
    erow *row = &E.row[y];
    int cc;
    for (cc = c; cc + qlen <= row->size; cc++) {
      if (editorIcmp(row->chars + cc, query, qlen)) {
        *fr = y;
        *fc = cc;
        return 1;
      }
    }
  }
  return 0;
}

void editorReplace(void) {
  int scx = E.cx, scy = E.cy;
  char *find = editorPrompt("Replace find: %s", editorFindCallback, editorFindStatus);
  find_on = 0;
  if (!find) { E.cx = scx; E.cy = scy; return; }
  if (!find[0]) { free(find); editorSetStatusMessage(""); return; }

  prompt_empty_ok = 1;
  char *repl = editorPrompt("Replace: %s", NULL, NULL);
  prompt_empty_ok = 0;
  if (!repl) {
    E.cx = scx; E.cy = scy;
    editorSetStatusMessage("Replace aborted");
    free(find);
    return;
  }

  int qlen = (int)strlen(find);
  int rlen = (int)strlen(repl);
  int replace_all = 0;
  int replaced = 0;
  int begun = 0;
  int row = E.cy, col = E.cx;

  for (;;) {
    int fr, fc;
    if (!editorFindNextReplace(find, row, col, &fr, &fc)) {
      if (replaced == 0)
        editorSetStatusMessage("Replace: no matches found");
      else
        editorSetStatusMessage("Replace: %d occurrence(s) swapped", replaced);
      break;
    }
    E.cy = fr;
    E.cx = fc;
    if (!replace_all) {
      int k;
      editorSetStatusMessage("Replace? (y)es (n)o (a)ll (r)est (esc): ");
      do {
        editorRefreshScreen();
        k = editorReadKey();
        if (k == PASTE_KEY) editorSkipPaste();
      } while (k == RESIZE_KEY || k == MOUSE_KEY || k == NOKEY || k == PASTE_KEY);
      if (k == 'a' || k == 'A' || k == 'r' || k == 'R') {
        replace_all = 1;
      } else if (k == 'n' || k == 'N') {
        row = fr;
        col = fc + qlen;
        continue;
      } else if (k != 'y' && k != 'Y') {
        editorSetStatusMessage("Replace canceled");
        break;
      }
    }

    if (!begun) {
      editorUndoBegin(0, E.numrows);
      begun = 1;
    }
    editorRowDeleteRange(&E.row[fr], fc, fc + qlen);
    editorRowInsertBytes(&E.row[fr], fc, repl, rlen);
    row = fr;
    col = fc + rlen;
    E.cx = col;
    replaced++;
  }

  if (begun) editorUndoCommit();
  free(find);
  free(repl);
  editorClampCursor();
  editorRefreshScreen();
}

void editorGoto(void) {
  char *q = editorPrompt("Goto: %s", NULL, NULL);
  if (!q) return;
  char *end;
  long n = strtol(q, &end, 10);
  if (end == q) {
    editorSetStatusMessage("Not a line number");
    free(q);
    return;
  }
  int col = 0;
  if (*end == ':') col = atoi(end + 1);
  if (q[0] == '+' || q[0] == '-') n = E.cy + 1 + n;
  free(q);
  if (E.numrows == 0) return;
  if (n < 1) n = 1;
  if (n > E.numrows) n = E.numrows;
  E.cy = (int)n - 1;
  E.cx = col > 1 ? editorRowRxToCx(&E.row[E.cy], col - 1) : 0;
  E.rowoff = E.cy - E.screenrows / 2;
  if (E.rowoff > E.numrows + 1 - E.screenrows)
    E.rowoff = E.numrows + 1 - E.screenrows;
  if (E.rowoff < 0) E.rowoff = 0;
  editorSetStatusMessage("Line %ld of %d", n, E.numrows);
}

void editorFindClear(void) {
  find_on = 0;
  find_q[0] = '\0';
  find_row = -1;
}
