#define _GNU_SOURCE

#include "oshi.h"

typedef struct editorRec {
  int at;
  char **old;
  int *osize;
  int nold;
  int nnew;
  int cx0, cy0;
  int cx1, cy1;
} editorRec;

static editorRec *undo_stack;
static int undo_count, undo_cap;
static long undo_dropped;
static editorRec *redo_stack;
static int redo_count, redo_cap;
static editorRec undo_pend;
static int undo_pending;
static int undo_rows_before;
static int undo_run;
static int undo_run_cx, undo_run_cy;
static char *saved_text;
static int saved_len;

static void editorFreeRec(editorRec *r) {
  int i;
  for (i = 0; i < r->nold; i++) free(r->old[i]);
  free(r->old);
  free(r->osize);
  r->old = NULL;
  r->osize = NULL;
  r->nold = 0;
}

static void editorCaptureRows(editorRec *r, int at, int n) {
  int i;
  r->at = at;
  r->nold = n;
  r->old = xmalloc(sizeof(char *) * (n > 0 ? n : 1));
  r->osize = xmalloc(sizeof(int) * (n > 0 ? n : 1));
  for (i = 0; i < n; i++) {
    r->osize[i] = E.row[at + i].size;
    r->old[i] = xmalloc(E.row[at + i].size + 1);
    memcpy(r->old[i], E.row[at + i].chars, E.row[at + i].size);
    r->old[i][E.row[at + i].size] = '\0';
  }
}

void editorSnapshot(void) {
  free(saved_text);
  saved_text = editorRowsToString(&saved_len);
}

void editorUpdateDirty(void) {
  long total = 0;
  int j, len;
  char *cur;
  for (j = 0; j < E.numrows; j++)
    total += E.row[j].size + (E.crlf ? 2 : 1);
  if (total != saved_len) {
    E.dirty = 1;
    return;
  }
  cur = editorRowsToString(&len);
  E.dirty = saved_text == NULL || len != saved_len ||
            memcmp(cur, saved_text, len) != 0;
  free(cur);
}

static void editorPushUndo(editorRec r) {
  if (undo_count == OSHI_UNDO_MAX) {
    editorFreeRec(&undo_stack[0]);
    memmove(undo_stack, undo_stack + 1, sizeof(editorRec) * (undo_count - 1));
    undo_count--;
    undo_dropped++;
  }
  if (undo_count == undo_cap) {
    undo_cap = undo_cap ? undo_cap * 2 : 64;
    undo_stack = xrealloc(undo_stack, sizeof(editorRec) * undo_cap);
  }
  undo_stack[undo_count++] = r;
}

static void editorPushRedo(editorRec r) {
  if (redo_count == redo_cap) {
    redo_cap = redo_cap ? redo_cap * 2 : 64;
    redo_stack = xrealloc(redo_stack, sizeof(editorRec) * redo_cap);
  }
  redo_stack[redo_count++] = r;
}

void editorUndoBegin(int at, int nold) {
  if (at < 0) at = 0;
  if (at > E.numrows) at = E.numrows;
  if (nold < 0) nold = 0;
  if (at + nold > E.numrows) nold = E.numrows - at;
  editorCaptureRows(&undo_pend, at, nold);
  undo_pend.cx0 = E.cx;
  undo_pend.cy0 = E.cy;
  undo_rows_before = E.numrows;
  undo_pending = 1;
}

void editorUndoCommit(void) {
  if (!undo_pending) return;
  undo_pending = 0;
  undo_pend.nnew = undo_pend.nold + (E.numrows - undo_rows_before);
  undo_pend.cx1 = E.cx;
  undo_pend.cy1 = E.cy;
  if (redo_count > 0) {
    while (redo_count > 0) editorFreeRec(&redo_stack[--redo_count]);
  }
  editorPushUndo(undo_pend);
  undo_run = 0;
  editorUpdateDirty();
}

static void editorRecApply(editorRec *r) {
  editorRec cur;
  int oldn = r->nold, t;
  if (r->at > E.numrows) r->at = E.numrows;
  if (r->at + r->nnew > E.numrows) r->nnew = E.numrows - r->at;
  editorCaptureRows(&cur, r->at, r->nnew);
  editorReplaceRows(r->at, r->nnew, r->old, r->osize, r->nold);
  editorFreeRec(r);
  r->old = cur.old;
  r->osize = cur.osize;
  r->nold = cur.nold;
  r->nnew = oldn;
  E.cx = r->cx0;
  E.cy = r->cy0;
  t = r->cx0; r->cx0 = r->cx1; r->cx1 = t;
  t = r->cy0; r->cy0 = r->cy1; r->cy1 = t;
  editorClampCursor();
}

void editorUndo(void) {
  if (undo_count == 0) {
    editorSetStatusMessage("Nothing to undo");
    return;
  }
  editorRec r = undo_stack[--undo_count];
  editorRecApply(&r);
  editorPushRedo(r);
  undo_run = 0;
  editorUpdateDirty();
}

void editorRedo(void) {
  if (redo_count == 0) {
    editorSetStatusMessage("Nothing to redo");
    return;
  }
  editorRec r = redo_stack[--redo_count];
  editorRecApply(&r);
  editorPushUndo(r);
  undo_run = 0;
  editorUpdateDirty();
}

void editorUndoClear(void) {
  while (undo_count > 0) editorFreeRec(&undo_stack[--undo_count]);
  while (redo_count > 0) editorFreeRec(&redo_stack[--redo_count]);
  undo_dropped = 0;
  undo_run = 0;
  undo_pending = 0;
}

void editorUndoFreeAll(void) {
  editorUndoClear();
  free(undo_stack);
  undo_stack = NULL;
  free(redo_stack);
  redo_stack = NULL;
  free(saved_text);
  saved_text = NULL;
  saved_len = 0;
}

int editorUndoRunContinues(void) {
  return undo_run && undo_count > 0 && E.cy == undo_run_cy && E.cx == undo_run_cx;
}

void editorUndoRunExtend(void) {
  undo_stack[undo_count - 1].cx1 = E.cx;
  undo_stack[undo_count - 1].cy1 = E.cy;
}

void editorUndoRunStart(void) {
  undo_run = 1;
}

void editorUndoRunEnd(void) {
  undo_run = 0;
}

void editorUndoRunMark(void) {
  undo_run_cy = E.cy;
  undo_run_cx = E.cx;
}
