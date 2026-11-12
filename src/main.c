#define _GNU_SOURCE

#include "oshi.h"

static void editorFreeAll(void) {
  int i;
  for (i = 0; i < E.numrows; i++)
    editorFreeRow(&E.row[i]);
  free(E.row);
  E.row = NULL;
  E.numrows = 0;
  free(E.filename);
  E.filename = NULL;
  free(E.clip);
  E.clip = NULL;
  editorUndoFreeAll();
}

static void initEditor(void) {
  E.cx = 0;
  E.cy = 0;
  E.rx = 0;
  E.goal_rx = 0;
  E.rowoff = 0;
  E.coloff = 0;
  E.numrows = 0;
  E.rowcap = 0;
  E.row = NULL;
  E.dirty = 0;
  E.filename = NULL;
  E.statusmsg[0] = '\0';
  E.statusmsg_time = 0;
  E.notice_pending = 0;
  E.notice_deadline = 0;
  E.config_err[0] = '\0';
  E.config_err_time = 0;
  E.quit_time = 0;
  E.ruler = 1;
  E.ruler_width = 0;
  E.gutter = 0;
  E.cursor_style = 2;
  E.clip = NULL;
  E.clip_shared = 0;
  editorSnapshot();
  E.dirty = 0;
  E.binds[CMD_SAVE] = CTRL_KEY('s');
  E.binds[CMD_QUIT] = CTRL_KEY('q');
  E.binds[CMD_FIND] = CTRL_KEY('f');
  E.binds[CMD_SELECT] = CTRL_KEY('a');
  E.binds[CMD_COPY] = CTRL_KEY('c');
  E.binds[CMD_CUT] = CTRL_KEY('x');
  E.binds[CMD_PASTE] = CTRL_KEY('v');
  E.binds[CMD_KILL] = CTRL_KEY('d');
  E.binds[CMD_UNDO] = CTRL_KEY('z');
  E.binds[CMD_REDO] = CTRL_KEY('r');
  E.binds[CMD_REPLACE] = ALT_KEY('r');
  E.binds[CMD_FIND_NEXT] = CTRL_KEY('n');
  E.binds[CMD_FIND_PREV] = CTRL_KEY('p');
  E.binds[CMD_GOTO] = CTRL_KEY('g');
  E.binds[CMD_HELP] = OSHI_KEY_HELP;
  E.binds[CMD_DELETE_WORD] = CTRL_BACKSPACE;
  editorApplyDefaultColors();
  editorLoadConfig();
  atexit(editorFreeAll);
  editorSetCursorStyle();
  editorUseAltScreen();
  editorTermSetup();

  if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
  E.screenrows -= 1;
  if (E.screenrows < 1) E.screenrows = 1;
}

int main(int argc, char *argv[]) {
  if (argc >= 2 && (strcmp(argv[1], "--version") == 0 ||
                    strcmp(argv[1], "-v") == 0)) {
    printf("oshi %s\n", OSHI_VERSION);
    return 0;
  }
  if (argc > 2) {
    fprintf(stderr, "usage: oshi [-v|--version] [file|folder]\n");
    return 1;
  }

  enableRawMode();
  editorInstallSignals();
  initEditor();
  if (argc >= 2 && editorIsDir(argv[1])) {
    char *picked;
    br_active = 1;
    picked = editorBrowse(argv[1], NULL);
    if (picked == NULL) editorQuit();
    editorOpen(picked);
    free(picked);
  } else if (argc >= 2) {
    editorOpen(argv[1]);
  }

  while (1) {
    editorRefreshScreen();
    editorProcessKeypress();
  }
  return 0;
}
