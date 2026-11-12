#define _GNU_SOURCE

#include "oshi.h"

volatile sig_atomic_t got_winch;
volatile sig_atomic_t got_term;
static int raw_enabled;

void editorWrite(const char *s) {
  size_t len = strlen(s);
  ssize_t r = write(STDOUT_FILENO, s, len);
  (void)r;
}

void editorUseAltScreen(void) {
  editorWrite("\x1b[?1049h");
}

void editorTermSetup(void) {
  editorWrite("\x1b[?1002h\x1b[?1006h"
              "\x1b]22;text\x1b\\"
              "\x1b[?2004h"
              "\x1b[?7l");
}

void editorTermReset(void) {
  editorWrite("\x1b[?2004l\x1b[?7h"
              "\x1b]22;default\x1b\\"
              "\x1b[?1006l\x1b[?1002l"
              "\x1b[?1049l\x1b[?25h\x1b[0 q");
}

void editorSetCursorStyle(void) {
  char seq[16];
  int len = snprintf(seq, sizeof(seq), "\x1b[%d q", E.cursor_style);
  ssize_t r = write(STDOUT_FILENO, seq, len);
  (void)r;
}

void disableRawMode(void) {
  if (raw_enabled) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios);
    raw_enabled = 0;
  }
}

void die(const char *s) {
  int e = errno;
  editorTermReset();
  disableRawMode();
  errno = e;
  perror(s);
  exit(1);
}

void dieMsg(const char *s, const char *why) {
  editorTermReset();
  disableRawMode();
  fprintf(stderr, "oshi: %s: %s\n", s, why);
  exit(1);
}

void editorQuit(void) {
  editorTermReset();
  exit(0);
}

void enableRawMode(void) {
  static int registered;
  if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");
  if (!registered) { atexit(disableRawMode); registered = 1; }

  struct termios raw = E.orig_termios;
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= (CS8);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
  raw_enabled = 1;
}

static void editorOnWinch(int sig) { (void)sig; got_winch = 1; }
static void editorOnTerm(int sig) { got_term = sig; }

static void editorOnCrash(int sig) {
  editorTermReset();
  if (raw_enabled) tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios);
  signal(sig, SIG_DFL);
  raise(sig);
}

void editorInstallSignals(void) {
  struct sigaction sa;
  int i;
  static const int term_sigs[] = { SIGTERM, SIGHUP, SIGINT, SIGQUIT };
  static const int crash_sigs[] = { SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT };

  memset(&sa, 0, sizeof(sa));
  sigemptyset(&sa.sa_mask);

  sa.sa_handler = editorOnWinch;
  sigaction(SIGWINCH, &sa, NULL);

  signal(SIGPIPE, SIG_IGN);

  sa.sa_handler = editorOnTerm;
  for (i = 0; i < (int)(sizeof(term_sigs) / sizeof(term_sigs[0])); i++)
    sigaction(term_sigs[i], &sa, NULL);

  sa.sa_handler = editorOnCrash;
  sa.sa_flags = SA_RESETHAND | SA_NODEFER;
  for (i = 0; i < (int)(sizeof(crash_sigs) / sizeof(crash_sigs[0])); i++)
    sigaction(crash_sigs[i], &sa, NULL);
}

int editorResize(void) {
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0 ||
      ws.ws_row == 0)
    return 0;
  E.screencols = ws.ws_col;
  E.screenrows = ws.ws_row - 1;
  if (E.screenrows < 1) E.screenrows = 1;
  E.redraw = 1;
  return 1;
}
