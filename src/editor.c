#define _GNU_SOURCE

#include "oshi.h"

struct editorConfig E;

static void editorRawDeleteSelection(void) {
  int sy, sx, ey, ex;
  editorSelectionRange(&sy, &sx, &ey, &ex);
  E.sel = 0;
  if (E.numrows == 0) return;

  erow *first = &E.row[sy];
  if (sy == ey) {
    if (ex > sx) editorRowDeleteRange(first, sx, ex);
  } else {
    erow *last = &E.row[ey];
    int tail = last->size - ex, y;
    first->chars = xrealloc(first->chars, sx + tail + 1);
    memcpy(&first->chars[sx], &last->chars[ex], tail);
    first->size = sx + tail;
    first->chars[first->size] = '\0';
    for (y = ey; y > sy; y--) editorDelRow(y);
  }
  E.cy = sy;
  E.cx = sx;
}

static void editorRawInsertBytes(const char *s, int n) {
  while (E.numrows <= E.cy) editorInsertRow(E.numrows, "", 0);
  if (E.cx > E.row[E.cy].size) E.cx = E.row[E.cy].size;
  editorRowInsertBytes(&E.row[E.cy], E.cx, s, n);
  E.cx += n;
}

static void editorRawNewline(void) {
  while (E.numrows <= E.cy) editorInsertRow(E.numrows, "", 0);
  erow *row = &E.row[E.cy];
  if (E.cx > row->size) E.cx = row->size;
  editorInsertRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx);
  row = &E.row[E.cy];
  row->size = E.cx;
  row->chars[row->size] = '\0';
  E.cy++;
  E.cx = 0;
}

void editorDeleteSelection(void) {
  int sy, sx, ey, ex;
  if (E.numrows == 0) { E.sel = 0; return; }
  editorSelectionRange(&sy, &sx, &ey, &ex);
  editorUndoBegin(sy, ey - sy + 1);
  editorRawDeleteSelection();
  editorUndoCommit();
}

void editorTypeBytes(const char *s, int n) {
  int sy, sx, ey, ex;
  if (E.sel && E.numrows > 0) {
    editorSelectionRange(&sy, &sx, &ey, &ex);
    editorUndoBegin(sy, ey - sy + 1);
    editorRawDeleteSelection();
    editorRawInsertBytes(s, n);
    editorUndoCommit();
    return;
  }
  E.sel = 0;
  if (editorUndoRunContinues()) {
    editorRawInsertBytes(s, n);
    editorUndoRunExtend();
  } else {
    editorUndoBegin(E.cy, E.cy < E.numrows ? 1 : 0);
    editorRawInsertBytes(s, n);
    editorUndoCommit();
    editorUndoRunStart();
  }
  editorUndoRunMark();
}

void editorInsertNewline(void) {
  int sy, sx, ey, ex;
  if (E.sel && E.numrows > 0) {
    editorSelectionRange(&sy, &sx, &ey, &ex);
    editorUndoBegin(sy, ey - sy + 1);
    editorRawDeleteSelection();
  } else {
    E.sel = 0;
    editorUndoBegin(E.cy, E.cy < E.numrows ? 1 : 0);
  }
  editorRawNewline();
  editorUndoCommit();
}

void editorDeleteChar(int forward) {
  if (E.numrows == 0) return;
  if (E.cy >= E.numrows) E.cy = E.numrows - 1;
  erow *row = &E.row[E.cy];
  if (E.cx > row->size) E.cx = row->size;
  if (!forward) {
    if (E.cx > 0) {
      int p = editorPrevChar(row, E.cx);
      editorUndoBegin(E.cy, 1);
      editorRowDeleteRange(row, p, E.cx);
      E.cx = p;
      editorUndoCommit();
    } else if (E.cy > 0) {
      editorUndoBegin(E.cy - 1, 2);
      E.cx = E.row[E.cy - 1].size;
      editorRowAppendString(&E.row[E.cy - 1], row->chars, row->size);
      editorDelRow(E.cy);
      E.cy--;
      editorUndoCommit();
    }
  } else {
    if (E.cx < row->size) {
      int e = editorCharEnd(row, E.cx);
      editorUndoBegin(E.cy, 1);
      editorRowDeleteRange(row, E.cx, e);
      editorUndoCommit();
    } else if (E.cy + 1 < E.numrows) {
      editorUndoBegin(E.cy, 2);
      editorRowAppendString(row, E.row[E.cy + 1].chars, E.row[E.cy + 1].size);
      editorDelRow(E.cy + 1);
      editorUndoCommit();
    }
  }
}

void editorDeleteWordBackward(void) {
  if (E.numrows == 0) return;
  if (E.cy >= E.numrows) E.cy = E.numrows - 1;
  erow *row = &E.row[E.cy];
  if (E.cx > row->size) E.cx = row->size;

  if (E.cx == 0) {
    if (E.cy == 0) return;
    editorUndoBegin(E.cy - 1, 2);
    E.cx = E.row[E.cy - 1].size;
    editorRowAppendString(&E.row[E.cy - 1], row->chars, row->size);
    editorDelRow(E.cy);
    E.cy--;
    editorUndoCommit();
    return;
  }

  int from = E.cx;
  while (from > 0 && isspace((unsigned char)row->chars[from - 1])) from--;
  if (from == E.cx) {
    while (from > 0 && !isspace((unsigned char)row->chars[from - 1])) from--;
  }
  if (from == E.cx) return;

  editorUndoBegin(E.cy, 1);
  editorRowDeleteRange(row, from, E.cx);
  E.cx = from;
  editorUndoCommit();
}

void editorDeleteLine(void) {
  if (E.numrows == 0) return;
  if (E.cy >= E.numrows) E.cy = E.numrows - 1;
  editorUndoBegin(E.cy, 1);
  editorDelRow(E.cy);
  if (E.numrows == 0) {
    E.cx = E.cy = 0;
  } else {
    if (E.cy >= E.numrows) E.cy = E.numrows - 1;
    E.cx = editorRowRxToCx(&E.row[E.cy], E.goal_rx);
  }
  editorUndoCommit();
}

int editorInsertText(const char *s, int n) {
  int i, m = 0, start = 0, sy, sx, ey, ex;
  char *clean;
  if (n <= 0) return 0;
  clean = xmalloc(n + 1);
  for (i = 0; i < n; i++) {
    unsigned char c = (unsigned char)s[i];
    if (c == '\r') {
      clean[m++] = '\n';
      if (i + 1 < n && s[i + 1] == '\n') i++;
    } else if (c == '\n' || c == '\t' || (c >= 32 && c != 127)) {
      clean[m++] = (char)c;
    }
  }
  if (m == 0) { free(clean); return 0; }

  if (E.sel && E.numrows > 0) {
    editorSelectionRange(&sy, &sx, &ey, &ex);
    editorUndoBegin(sy, ey - sy + 1);
    editorRawDeleteSelection();
  } else {
    E.sel = 0;
    editorUndoBegin(E.cy, E.cy < E.numrows ? 1 : 0);
  }
  for (i = 0; i <= m; i++) {
    if (i == m || clean[i] == '\n') {
      if (i > start) editorRawInsertBytes(clean + start, i - start);
      if (i < m) editorRawNewline();
      start = i + 1;
    }
  }
  editorUndoCommit();
  int nlines = editorLineCount(clean, m);
  free(clean);
  return nlines;
}
