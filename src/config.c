#define _GNU_SOURCE

#include "oshi.h"

struct editorCmd editorCmds[CMD_COUNT] = {
  { "save", "save the file (root via sudo/doas if refused)" },
  { "quit", "quit (one warning on the bar if modified, then quits)" },
  { "find", "find in the file" },
  { "select", "select everything" },
  { "copy", "copy selection, else the line" },
  { "cut", "cut selection, else the line" },
  { "paste", "paste what oshi last copied" },
  { "delete-line", "delete the line" },
  { "undo", "undo a change" },
  { "redo", "redo it" },
  { "replace", "find and replace" },
  { "find-next", "jump to the next match" },
  { "find-prev", "jump to the previous match" },
  { "goto", "go to a line (N, +N, -N or N:col)" },
  { "help", "show this help screen (ctrl-? or ctrl-/)" },
  { "delete-word", "delete the word behind the cursor (ctrl-backspace)" }
};

static int cfg_errs;
static char cfg_first[128];

static void cfgError(int lineno, const char *fmt, ...) {
  char msg[96];
  va_list ap;
  if (cfg_errs++ > 0) return;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);
  snprintf(cfg_first, sizeof(cfg_first), "config line %d: %s", lineno, msg);
}

static void editorMkdirs(char *dir) {
  char *p;
  for (p = dir + 1; *p; p++) {
    if (*p == '/') {
      *p = '\0';
      mkdir(dir, 0700);
      *p = '/';
    }
  }
  mkdir(dir, 0700);
}

static void editorMakeConfig(const char *path) {
  char dir[512];
  FILE *fp;
  snprintf(dir, sizeof(dir), "%s", path);
  char *slash = strrchr(dir, '/');
  if (!slash) return;
  *slash = '\0';
  editorMkdirs(dir);

  fp = fopen(path, "w");
  if (!fp) return;
  fputs(
    "# oshi config - ctrl-e in the editor lists every command and its key.\n"
    "\n"
    "ruler true            # line numbers in the gutter\n"
    "ruler_width default   # or a fixed number of columns\n"
    "cursor block          # block | beam\n"
    "\n"
    "# color <role> <value>   256-palette code or hex, e.g. color text #EAEFF5\n"
    "# roles: status_bg status_fg gutter_fg gutter_cursor_fg gutter_bg\n"
    "#        gutter_cursor_bg tilde_fg text scrollmark_bg scrollmark_fg find\n"
    "# bind <key> <command>    e.g. bind ctrl-w save\n", fp);
  fclose(fp);
}

static int editorParseKey(const char *spec) {
  if (strcmp(spec, "none") == 0) return 0;
  if (!strcmp(spec, "ctrl-?") || !strcmp(spec, "ctrl-/") || !strcmp(spec, "ctrl-_"))
    return OSHI_KEY_HELP;
  if (strncmp(spec, "ctrl-", 5) == 0 && spec[5] && !spec[6] &&
      isalpha((unsigned char)spec[5])) {
    int k = CTRL_KEY(tolower((unsigned char)spec[5]));
    if (k == '\t' || k == '\r') return -2;
    return k;
  }
  if (strncmp(spec, "alt-", 4) == 0 && spec[4] && !spec[5])
    return ALT_KEY(tolower((unsigned char)spec[4]));
  return -1;
}

static int editorBind(const char *spec, const char *name) {
  int key = editorParseKey(spec);
  int cmd = -1, i;
  for (i = 0; i < CMD_COUNT; i++)
    if (strcmp(name, editorCmds[i].name) == 0) cmd = i;
  if (cmd < 0) return -3;
  if (key < 0) return key;
  for (i = 0; i < CMD_COUNT; i++)
    if (i != cmd && E.binds[i] == key) E.binds[i] = 0;
  E.binds[cmd] = key;
  return 0;
}

int editorCommandForKey(int c) {
  int i;
  if (c == 0) return CMD_NONE;
  for (i = 0; i < CMD_COUNT; i++)
    if (E.binds[i] == c) return i;
  return CMD_NONE;
}

void editorKeyName(int key, char *buf, size_t len) {
  if (key == 0)
    snprintf(buf, len, "off");
  else if (key == CTRL_BACKSPACE)
    snprintf(buf, len, "ctrl-bksp");
  else if (key == OSHI_KEY_HELP)
    snprintf(buf, len, "ctrl-?");
  else if (key >= ALT_KEY(0) && key <= ALT_KEY(255))
    snprintf(buf, len, "alt-%c", key - ALT_KEY(0));
  else if (key >= 1 && key <= 26)
    snprintf(buf, len, "ctrl-%c", key - 1 + 'a');
  else
    snprintf(buf, len, "?");
}

static int cfgBool(const char *val) {
  if (!strcmp(val, "true") || !strcmp(val, "on") ||
      !strcmp(val, "1") || !strcmp(val, "yes")) return 1;
  if (!strcmp(val, "false") || !strcmp(val, "off") ||
      !strcmp(val, "0") || !strcmp(val, "no")) return 0;
  return -1;
}

static int editorApplyColorSpec(int role, const char *spec) {
  if (spec[0] == '#') {
    size_t i, len = strlen(spec + 1);
    unsigned vals[6];
    if (len != 3 && len != 6) return 0;
    for (i = 0; i < len; i++)
      if (!isxdigit((unsigned char)spec[1 + i])) return 0;
    if (len == 6) {
      if (sscanf(spec + 1, "%02x%02x%02x", &vals[0], &vals[1], &vals[2]) != 3)
        return 0;
      editorColorSetRGB(role, (int)vals[0], (int)vals[1], (int)vals[2]);
    } else {
      if (sscanf(spec + 1, "%01x%01x%01x", &vals[0], &vals[1], &vals[2]) != 3)
        return 0;
      editorColorSetRGB(role, (int)(vals[0] * 17), (int)(vals[1] * 17),
                        (int)(vals[2] * 17));
    }
    return 1;
  }
  {
    char *end;
    long c = strtol(spec, &end, 10);
    if (*spec && !*end && c >= 0 && c < 256) {
      editorColorSet(role, (int)c);
      return 1;
    }
  }
  return 0;
}

void editorLoadConfig(void) {
  char path[512];
  const char *dir = getenv("XDG_CONFIG_HOME");
  if (dir && *dir) {
    snprintf(path, sizeof(path), "%s/oshi/config", dir);
  } else {
    const char *home = getenv("HOME");
    if (!home) return;
    snprintf(path, sizeof(path), "%s/.config/oshi/config", home);
  }

  FILE *fp = fopen(path, "r");
  if (!fp) {
    if (errno == ENOENT) editorMakeConfig(path);
    return;
  }

  char line[256];
  int lineno = 0;
  while (fgets(line, sizeof(line), fp)) {
    lineno++;
    size_t n = strlen(line);
    if (n > 0 && line[n - 1] != '\n' && !feof(fp)) {
      int ch;
      while ((ch = fgetc(fp)) != EOF && ch != '\n') ;
      cfgError(lineno, "line too long");
      continue;
    }
    {
      char w1[64], w2[64], w3[64];
      int nw = sscanf(line, " %63[^ \t=\r\n] %63[^ \t=\r\n] %63[^ \t=\r\n]",
                      w1, w2, w3);
      if (nw >= 1 && strcmp(w1, "color") == 0) {
        int r;
        if (nw < 3) cfgError(lineno, "'color' wants a role and a value");
        else if ((r = editorParseColorRole(w2)) < 0)
          cfgError(lineno, "no color role '%s'", w2);
        else if (!editorApplyColorSpec(r, w3))
          cfgError(lineno, "'%s' isn't a color (0-255 or #rrggbb)", w3);
        continue;
      }
    }

    char *hash = strchr(line, '#');
    if (hash) *hash = '\0';

    char *key = strtok(line, " \t=\r\n");
    if (!key) continue;
    char *val = strtok(NULL, " \t=\r\n");
    if (!val) {
      cfgError(lineno, "'%s' needs a value", key);
      continue;
    }

    if (strcmp(key, "bind") == 0) {
      char *cmdname = strtok(NULL, " \t=\r\n");
      if (!cmdname) {
        cfgError(lineno, "bind wants a key and a command");
        continue;
      }
      switch (editorBind(val, cmdname)) {
      case -1: cfgError(lineno, "'%s' isn't a key (ctrl-x, alt-x, none)", val); break;
      case -2: cfgError(lineno, "%s can't be rebound (Tab/Enter)", val); break;
      case -3: cfgError(lineno, "no command called '%s'", cmdname); break;
      }
      continue;
    }

    if (strcmp(key, "colorscheme") == 0) {
      if (strcmp(val, "default") == 0) editorApplyDefaultColors();
      else cfgError(lineno, "colorscheme knows only 'default'");
      continue;
    }

    if (strcmp(key, "ruler") == 0) {
      int b = cfgBool(val);
      if (b < 0) cfgError(lineno, "ruler wants true or false");
      else E.ruler = b;
    } else if (strcmp(key, "ruler_width") == 0) {
      if (strcmp(val, "default") == 0) {
        E.ruler_width = 0;
      } else {
        char *end;
        long w = strtol(val, &end, 10);
        if (*val && !*end && w > 0 && w < 100000)
          E.ruler_width = (int)w;
        else
          cfgError(lineno, "ruler_width wants default or a number");
      }
    } else if (strcmp(key, "cursor") == 0) {
      if (strcmp(val, "block") == 0) E.cursor_style = 2;
      else if (strcmp(val, "beam") == 0) E.cursor_style = 6;
      else cfgError(lineno, "cursor wants block or beam");
    } else {
      cfgError(lineno, "unknown setting '%s'", key);
    }
  }
  fclose(fp);
  if (cfg_errs > 0) {
    snprintf(E.config_err, sizeof(E.config_err), "%s%s", cfg_first,
             cfg_errs > 1 ? " (and more)" : "");
    E.config_err_time = editorNowMs();
  }
}
