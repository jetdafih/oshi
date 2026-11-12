#define _GNU_SOURCE

#include "oshi.h"

static void editorClampPos(int *y, int *x) {
  if (E.numrows == 0) { *y = 0; *x = 0; return; }
  if (*y < 0) *y = 0;
  if (*y >= E.numrows) { *y = E.numrows - 1; *x = E.row[*y].size; }
  if (*x > E.row[*y].size) *x = E.row[*y].size;
  if (*x < 0) *x = 0;
}

void editorSelectionRange(int *sy, int *sx, int *ey, int *ex) {
  int forward = E.sel_cy < E.cy || (E.sel_cy == E.cy && E.sel_cx <= E.cx);
  *sy = forward ? E.sel_cy : E.cy;
  *sx = forward ? E.sel_cx : E.cx;
  *ey = forward ? E.cy : E.sel_cy;
  *ex = forward ? E.cx : E.sel_cx;
  editorClampPos(sy, sx);
  editorClampPos(ey, ex);
}

int editorRowSelection(erow *row, int filerow, int *from, int *to) {
  int sy, sx, ey, ex;
  if (!E.sel) return 0;
  editorSelectionRange(&sy, &sx, &ey, &ex);
  if (filerow < sy || filerow > ey) return 0;
  *from = filerow == sy ? sx : 0;
  *to = filerow == ey ? ex : row->size;
  return 1;
}

void editorMoveToEnd(int dir) {
  if (dir > 0) {
    E.cy = E.numrows > 0 ? E.numrows - 1 : 0;
    E.cx = E.numrows > 0 ? E.row[E.cy].size : 0;
  } else {
    E.cy = 0;
    E.cx = 0;
  }
}

int editorAllSelected(void) {
  int sy, sx, ey, ex;
  if (!E.sel) return 0;
  if (E.numrows == 0) return 1;
  editorSelectionRange(&sy, &sx, &ey, &ex);
  return sy == 0 && sx == 0 && ey == E.numrows - 1 && ex == E.row[ey].size;
}

void editorSelectAll(void) {
  E.sel_cy = 0;
  E.sel_cx = 0;
  E.sel = 1;
  editorMoveToEnd(1);
}

char *editorSelectionToString(int *len) {
  int sy, sx, ey, ex, y, total = 0, at = 0;
  char *buf;
  if (E.numrows == 0) { *len = 0; return xmalloc(1); }
  editorSelectionRange(&sy, &sx, &ey, &ex);

  for (y = sy; y <= ey; y++) {
    erow *row = &E.row[y];
    int from = y == sy ? sx : 0;
    int to = y == ey ? ex : row->size;
    if (to > from) total += to - from;
    if (y < ey) total++;
  }

  buf = xmalloc(total + 1);
  for (y = sy; y <= ey; y++) {
    erow *row = &E.row[y];
    int from = y == sy ? sx : 0;
    int to = y == ey ? ex : row->size;
    if (to > from) {
      memcpy(buf + at, row->chars + from, to - from);
      at += to - from;
    }
    if (y < ey) buf[at++] = '\n';
  }

  *len = at;
  return buf;
}
