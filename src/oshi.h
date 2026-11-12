#ifndef OSHI_H
#define OSHI_H

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define CTRL_KEY(k) ((k) & 0x1f)
#define ALT_KEY(k) (2000 + (k))
#define OSHI_KEY_HELP 0x1f
#ifndef OSHI_VERSION
#define OSHI_VERSION "1.0.0"
#endif
#define OSHI_TAB_STOP 8
#define OSHI_CLICK_MS 400
#define OSHI_CLICK_SLOP 2
#define OSHI_UNDO_MAX 1000
#define OSHI_OSC52_MAX 74994
#define OSHI_STATUS_MS 1500
#define OSHI_PASTE_MAX (64 * 1024 * 1024)
#define OSHI_FILE_MAX (1024L * 1024 * 1024)
#define OSHI_IMG_TIMEOUT 5
#define OSHI_PROMPT_MAX 200
#define OSHI_CLIP_TIMEOUT 1
#define OSHI_CLIP_SETTLE_US 20000

enum editorKey {
  ESC_AGAIN = -1,
  BACKSPACE = 127,
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  DEL_KEY,
  HOME_KEY,
  END_KEY,
  PAGE_UP,
  PAGE_DOWN,
  CTRL_LEFT,
  CTRL_RIGHT,
  SHIFT_UP,
  SHIFT_DOWN,
  SHIFT_LEFT,
  SHIFT_RIGHT,
  CSHIFT_LEFT,
  CSHIFT_RIGHT,
  CTRL_BACKSPACE,
  MOUSE_KEY,
  UTF8_KEY,
  PASTE_KEY,
  RESIZE_KEY,
  NOKEY
};

enum editorCommand {
  CMD_NONE = -1,
  CMD_SAVE,
  CMD_QUIT,
  CMD_FIND,
  CMD_SELECT,
  CMD_COPY,
  CMD_CUT,
  CMD_PASTE,
  CMD_KILL,
  CMD_UNDO,
  CMD_REDO,
  CMD_REPLACE,
  CMD_FIND_NEXT,
  CMD_FIND_PREV,
  CMD_GOTO,
  CMD_HELP,
  CMD_DELETE_WORD,
  CMD_COUNT
};

enum editorColor {
  COL_STATUS_BG,
  COL_STATUS_FG,
  COL_GUTTER_FG,
  COL_GUTTER_CURSOR_FG,
  COL_GUTTER_BG,
  COL_GUTTER_CURSOR_BG,
  COL_TILDE_FG,
  COL_TEXT_FG,
  COL_SCROLLMARK_BG,
  COL_SCROLLMARK_FG,
  COL_FIND,
  COL_COUNT
};

enum { CL_PLAIN = 0, CL_COMMENT, CL_KW1, CL_KW2, CL_STR, CL_NUM, CL_PRE,
       CL_FUNC, CL_COUNT };

typedef struct erow {
  int size;
  char *chars;
} erow;

struct editorConfig {
  int cx, cy;
  int rx;
  int goal_rx;
  int rowoff;
  int coloff;
  int sel;
  int sel_cx;
  int sel_cy;
  int mouse_button;
  int mouse_release;
  int mouse_x;
  int mouse_y;
  int mouse_click_n;
  int mouse_click_x;
  int mouse_click_y;
  long mouse_click_ms;
  int screenrows;
  int screencols;
  int numrows;
  int rowcap;
  erow *row;
  int dirty;
  int crlf;
  int redraw;
  int ruler;
  int ruler_width;
  int gutter;
  int cursor_style;
  int binds[CMD_COUNT];
  char col_str[COL_COUNT][32];
  char *filename;
  char statusmsg[256];
  long statusmsg_time;
  int notice_pending;
  long notice_deadline;
  char config_err[160];
  long config_err_time;
  int prompt_col;
  int in_prompt;
  int quit_confirm;
  long quit_time;
  char utf8[8];
  int utf8len;
  char *clip;
  int clip_len;
  int clip_line;
  int clip_shared;
  struct termios orig_termios;
};

extern struct editorConfig E;

extern volatile sig_atomic_t got_winch;
extern volatile sig_atomic_t got_term;
extern int prompt_empty_ok;

struct editorCmd { const char *name; const char *desc; };
extern struct editorCmd editorCmds[CMD_COUNT];

struct abuf {
  char *b;
  int len;
  int cap;
};

#define ABUF_INIT {NULL, 0, 0}

extern int br_active;
extern int find_on;
extern char find_q[256];
extern long find_time;

void editorWrite(const char *s);
void editorUseAltScreen(void);
void editorTermSetup(void);
void editorTermReset(void);
void editorSetCursorStyle(void);
void disableRawMode(void);
void die(const char *s);
void dieMsg(const char *s, const char *why);
void editorQuit(void);
void enableRawMode(void);
void editorInstallSignals(void);
int editorResize(void);

int editorReadKey(void);
int getWindowSize(int *rows, int *cols);

void *xmalloc(size_t n);
void *xrealloc(void *p, size_t n);
void editorSkipPaste(void);
char *editorReadPasteBuf(int *len);

int utf8Decode(const unsigned char *s, int n, int *len);
int cpWidth(int cp);
int editorCharEnd(const erow *row, int b);
int editorPrevChar(const erow *row, int b);
int editorCellWidth(const erow *row, int b, int col);
int editorStrWidth(const char *s, int n);
int editorFitHead(const char *s, int n, int maxcols);
int editorFitTail(const char *s, int n, int maxcols);
int editorCharCount(const char *s, int len);
int editorLineCount(const char *s, int len);

int editorRowCxToRx(erow *row, int cx);
int editorRowRxToCx(erow *row, int rx);
void editorSyncGoal(void);
void editorClampCursor(void);
void editorInsertRow(int at, const char *s, size_t len);
void editorFreeRow(erow *row);
void editorDelRow(int at);
void editorReplaceRows(int at, int ndel, char **s, int *sz, int nins);
void editorRowInsertBytes(erow *row, int at, const char *s, int n);
void editorRowAppendString(erow *row, const char *s, size_t len);
void editorRowDeleteRange(erow *row, int from, int to);

void editorSnapshot(void);
void editorUpdateDirty(void);
void editorUndoBegin(int at, int nold);
void editorUndoCommit(void);
void editorUndo(void);
void editorRedo(void);
void editorUndoClear(void);
void editorUndoFreeAll(void);
int editorUndoRunContinues(void);
void editorUndoRunExtend(void);
void editorUndoRunStart(void);
void editorUndoRunEnd(void);
void editorUndoRunMark(void);

void editorSelectionRange(int *sy, int *sx, int *ey, int *ex);
int editorRowSelection(erow *row, int filerow, int *from, int *to);
void editorMoveToEnd(int dir);
int editorAllSelected(void);
void editorSelectAll(void);
char *editorSelectionToString(int *len);

void editorDeleteSelection(void);
void editorTypeBytes(const char *s, int n);
void editorInsertNewline(void);
void editorDeleteChar(int forward);
void editorDeleteWordBackward(void);
void editorDeleteLine(void);
int editorInsertText(const char *s, int n);

char *editorRowsToString(int *buflen);
void editorOpen(char *filename);
int editorWriteAll(int fd, const char *buf, int len);
char *editorParentDir(const char *path);
int editorIsDir(const char *path);
void editorSave(void);

void editorCopySelection(void);
void editorCut(void);
int editorInPath(const char *name);
char *editorWhich(const char *name);
char *editorClipboardRead(int *len);
char *editorClipboardImage(int *len, const char **ext);
void editorPasteInternal(void);

char *editorIstrstr(const char *hay, const char *needle);
int editorFindCount(int *cur);
void editorFind(void);
int editorFindNextMatch(int dir);
void editorReplace(void);
void editorGoto(void);
void editorFindClear(void);

void abAppend(struct abuf *ab, const char *s, int len);
void abFree(struct abuf *ab);
void editorScroll(void);
void editorRefreshScreen(void);
void editorSetStatusMessage(const char *fmt, ...);

void editorHlSwitch(struct abuf *ab, int *cur, int want);
int editorHlBegin(void);
int editorHlActive(void);
void editorHlRow(erow *hr, int *hstate);
void editorHlEnd(void);
const unsigned char *editorHlClasses(void);

char *editorPrompt(char *prompt, void (*callback)(char *, int),
                   void (*decorate)(char *));
long editorNowMs(void);
void editorDrawBox(struct abuf *ab, const char *title,
                   const char **body, int nbody, int width);
int editorSaveElevated(const char *buf, int len);
void editorProcessKeypress(void);

int editorCommandForKey(int c);
void editorKeyName(int key, char *buf, size_t len);
void editorLoadConfig(void);

void editorColorSet(int role, int code);
void editorColorSetRGB(int role, int r, int g, int b);
void editorApplyDefaultColors(void);
int editorParseColorRole(const char *name);

char *editorBrowse(const char *start, const char *select);
void editorLeave(void);

#endif
