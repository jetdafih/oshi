#define _GNU_SOURCE

#include "oshi.h"

int editorRowCxToRx(erow *row, int cx) {
  int rx = 0, b = 0;
  if (cx > row->size) cx = row->size;
  while (b < cx) {
    rx += editorCellWidth(row, b, rx);
    b = editorCharEnd(row, b);
  }
  return rx;
}

int editorRowRxToCx(erow *row, int rx) {
  int cur = 0, b = 0;
  while (b < row->size) {
    int w = editorCellWidth(row, b, cur);
    if (cur + w > rx) return b;
    cur += w;
    b = editorCharEnd(row, b);
  }
  return row->size;
}

void editorSyncGoal(void) {
  E.goal_rx = E.cy < E.numrows ? editorRowCxToRx(&E.row[E.cy], E.cx) : 0;
}

void editorClampCursor(void) {
  if (E.numrows == 0) { E.cx = 0; E.cy = 0; return; }
  if (E.cy >= E.numrows) E.cy = E.numrows - 1;
  if (E.cy < 0) E.cy = 0;
  if (E.cx > E.row[E.cy].size) E.cx = E.row[E.cy].size;
  if (E.cx < 0) E.cx = 0;
}

static void editorRowsReserve(int n) {
  int cap;
  if (n <= E.rowcap) return;
  cap = E.rowcap ? E.rowcap : 64;
  while (cap < n) cap *= 2;
  E.row = xrealloc(E.row, sizeof(erow) * cap);
  E.rowcap = cap;
}

void editorInsertRow(int at, const char *s, size_t len) {
  if (at < 0 || at > E.numrows) return;
  editorRowsReserve(E.numrows + 1);
  memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at));

  E.row[at].size = (int)len;
  E.row[at].chars = xmalloc(len + 1);
  memcpy(E.row[at].chars, s, len);
  E.row[at].chars[len] = '\0';
  E.numrows++;
}

void editorFreeRow(erow *row) {
  free(row->chars);
}

void editorDelRow(int at) {
  if (at < 0 || at >= E.numrows) return;
  editorFreeRow(&E.row[at]);
  memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numrows - at - 1));
  E.numrows--;
}

void editorReplaceRows(int at, int ndel, char **s, int *sz, int nins) {
  int i, tail = E.numrows - at - ndel;
  for (i = 0; i < ndel; i++) editorFreeRow(&E.row[at + i]);
  editorRowsReserve(E.numrows - ndel + nins);
  if (tail > 0)
    memmove(&E.row[at + nins], &E.row[at + ndel], sizeof(erow) * tail);
  for (i = 0; i < nins; i++) {
    E.row[at + i].size = sz[i];
    E.row[at + i].chars = xmalloc(sz[i] + 1);
    memcpy(E.row[at + i].chars, s[i], sz[i]);
    E.row[at + i].chars[sz[i]] = '\0';
  }
  E.numrows += nins - ndel;
}

void editorRowInsertBytes(erow *row, int at, const char *s, int n) {
  if (at < 0 || at > row->size) at = row->size;
  row->chars = xrealloc(row->chars, row->size + n + 1);
  memmove(&row->chars[at + n], &row->chars[at], row->size - at + 1);
  memcpy(&row->chars[at], s, n);
  row->size += n;
}

void editorRowAppendString(erow *row, const char *s, size_t len) {
  row->chars = xrealloc(row->chars, row->size + len + 1);
  memcpy(&row->chars[row->size], s, len);
  row->size += (int)len;
  row->chars[row->size] = '\0';
}

void editorRowDeleteRange(erow *row, int from, int to) {
  if (from < 0) from = 0;
  if (to > row->size) to = row->size;
  if (from >= to) return;
  memmove(&row->chars[from], &row->chars[to], row->size - to + 1);
  row->size -= (to - from);
}
