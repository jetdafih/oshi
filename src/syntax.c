#define _GNU_SOURCE

#include "oshi.h"

static const char *cl_esc[CL_COUNT] = {
  "",
  "\x1b[38;2;152;160;174m",
  "\x1b[38;2;165;138;168m",
  "\x1b[38;2;128;157;158m",
  "\x1b[38;2;141;168;130m",
  "\x1b[38;2;224;165;104m",
  "\x1b[38;2;227;123;102m",
  "\x1b[38;2;137;153;170m"
};

enum { ST_NONE = 0, ST_BLOCK, ST_TDQ, ST_TSQ, ST_BT };

#define SF_NUM     (1 << 0)
#define SF_DQ      (1 << 1)
#define SF_SQ      (1 << 2)
#define SF_CHAR    (1 << 3)
#define SF_BT      (1 << 4)
#define SF_TRIPLE  (1 << 5)
#define SF_PRE     (1 << 6)
#define SF_ICASE   (1 << 7)
#define SF_HASHWS  (1 << 8)
#define SF_DOLLAR  (1 << 9)
#define SF_MAKEVAR (1 << 10)
#define SF_FUNC    (1 << 11)
#define SF_DOTDIR  (1 << 12)
#define SF_PCT     (1 << 13)
#define SF_LABEL   (1 << 14)
#define SF_SQRAW   (1 << 15)
#define SF_SQB     (1 << 16)

struct syn {
  const char *name;
  const char *match;
  const char *kw1, *kw2, *kw3;
  const char *lc, *lc2;
  const char *bs, *be;
  int flags;
};

static const struct syn syntaxes[] = {
  { "c", ".c .h .cc .cpp .cxx .hpp .hh .ino",
    "auto break case const continue default do else enum extern for goto if "
    "inline register restrict return sizeof static struct switch typedef "
    "union volatile while _Alignas _Alignof _Atomic _Bool _Generic _Noreturn "
    "_Static_assert _Thread_local class namespace template typename public "
    "private protected virtual new delete this try catch throw using operator "
    "constexpr nullptr override final noexcept explicit mutable friend "
    "static_assert true false NULL",
    "int long short char float double void unsigned signed bool size_t "
    "ssize_t off_t pid_t int8_t int16_t int32_t int64_t uint8_t uint16_t "
    "uint32_t uint64_t uintptr_t intptr_t ptrdiff_t FILE wchar_t string "
    "vector map",
    NULL, "//", NULL, "/*", "*/",
    SF_NUM | SF_DQ | SF_CHAR | SF_PRE | SF_FUNC },

  { "python", ".py .pyw .pyi",
    "False None True and as assert async await break class continue def del "
    "elif else except finally for from global if import in is lambda "
    "nonlocal not or pass raise return try while with yield match case",
    "int float str bool list dict set tuple bytes object type self cls print "
    "len range open super isinstance enumerate zip map filter",
    NULL, "#", NULL, NULL, NULL,
    SF_NUM | SF_DQ | SF_SQ | SF_TRIPLE | SF_FUNC },

  { "javascript", ".js .mjs .cjs .jsx .ts .tsx",
    "async await break case catch class const continue debugger default "
    "delete do else export extends finally for function if import in "
    "instanceof let new of return static super switch this throw try typeof "
    "var void while with yield from as get set interface type enum "
    "implements namespace declare readonly abstract public private "
    "protected null undefined true false NaN",
    "string number boolean any unknown never object Array Map Set Promise "
    "Object console JSON Math Date Error Symbol",
    NULL, "//", NULL, "/*", "*/",
    SF_NUM | SF_DQ | SF_SQ | SF_BT | SF_FUNC },

  { "go", ".go",
    "break case chan const continue default defer else fallthrough for func "
    "go goto if import interface map package range return select struct "
    "switch type var true false nil iota",
    "bool byte complex64 complex128 error float32 float64 int int8 int16 "
    "int32 int64 rune string uint uint8 uint16 uint32 uint64 uintptr any "
    "make new len cap append copy delete panic recover print println",
    NULL, "//", NULL, "/*", "*/",
    SF_NUM | SF_DQ | SF_CHAR | SF_BT | SF_FUNC },

  { "rust", ".rs",
    "as async await break const continue crate dyn else enum extern false fn "
    "for if impl in let loop match mod move mut pub ref return self Self "
    "static struct super trait true type unsafe use where while",
    "bool char f32 f64 i8 i16 i32 i64 i128 isize u8 u16 u32 u64 u128 usize "
    "str String Vec Option Result Some None Ok Err Box Rc Arc",
    NULL, "//", NULL, "/*", "*/",
    SF_NUM | SF_DQ | SF_CHAR | SF_FUNC },

  { "shell", ".sh .bash .zsh .ksh .bashrc .zshrc .profile .bash_profile "
    "PKGBUILD",
    "if then else elif fi case esac for while until do done in function "
    "select time return break continue exit local export readonly declare "
    "unset shift source alias set",
    "echo printf cd pwd test read eval exec trap true false",
    NULL, "#", NULL, NULL, NULL,
    SF_DQ | SF_SQ | SF_SQRAW | SF_DOLLAR | SF_HASHWS },

  { "make", "Makefile makefile GNUmakefile .mk",
    "ifeq ifneq ifdef ifndef else endif include sinclude define endef "
    "export unexport override vpath",
    NULL, NULL, "#", NULL, NULL, NULL,
    SF_DQ | SF_DOLLAR | SF_MAKEVAR | SF_HASHWS },

  { "json", ".json .jsonc .webmanifest",
    NULL, "true false null", NULL, NULL, NULL, NULL, NULL,
    SF_NUM | SF_DQ },

  { "conf", ".toml .yaml .yml .ini .conf .cfg .service .desktop .env",
    NULL, "true false null yes no on off True False", NULL,
    "#", NULL, NULL, NULL,
    SF_NUM | SF_DQ | SF_SQ | SF_SQB | SF_HASHWS },

  { "kdl", ".kdl",
    NULL,
    "true false null inf nan",
    NULL, "//", NULL, "/*", "*/",
    SF_NUM | SF_DQ },

  { "asm", ".asm .s .S .nasm .inc",
    "mov movzx movsx movsxd movabs lea push pop pushf popf pushq popq call "
    "ret retq callq jmp jmpq je jne jz jnz ja jae jb jbe jg jge jl jle js "
    "jns jo jno jc jnc jp jnp jcxz jecxz jrcxz loop loope loopne cmp test "
    "add sub mul imul div idiv inc dec neg not and or xor shl shr sal sar "
    "rol ror rcl rcr adc sbb xchg cmove cmovne cmovz cmovnz cmova cmovae "
    "cmovb cmovbe cmovg cmovge cmovl cmovle cmovs cmovns sete setne setz "
    "setnz seta setae setb setbe setg setge setl setle nop hlt syscall "
    "sysret sysenter int int3 iret iretq leave enter cld std cli sti rep "
    "repe repne repz repnz lock movsb movsw movsd movsq stosb stosw stosd "
    "stosq lodsb lodsw lodsd lodsq scasb scasw scasd scasq cmpsb cmpsw "
    "cmpsd cmpsq cpuid rdtsc rdmsr wrmsr xadd cmpxchg bsf bsr bt bts btr "
    "btc popcnt lzcnt tzcnt bswap cdq cqo cwd cbw cwde cdqe sahf lahf in "
    "out movaps movups movdqa movdqu movq movd movss movsd addps addss "
    "addpd addsd subps subss mulps mulss divps divss sqrtss pxor paddd "
    "paddq psubd pshufb pcmpeqb pmovmskb vmovdqa vmovdqu vpxor movl movb "
    "movw addl addq subl subq cmpl cmpq xorl xorq andl andq orl orq testl "
    "testq shll shlq shrl shrq leaq incl incq decl decq negq notq imulq "
    "ud2 pause mfence lfence sfence",
    "rax rbx rcx rdx rsi rdi rbp rsp rip r8 r9 r10 r11 r12 r13 r14 r15 "
    "r8d r9d r10d r11d r12d r13d r14d r15d r8w r9w r10w r11w r12w r13w r14w "
    "r15w r8b r9b r10b r11b r12b r13b r14b r15b eax ebx ecx edx esi edi ebp "
    "esp eip ax bx cx dx si di bp sp al bl cl dl ah bh ch dh sil dil bpl "
    "spl cs ds es fs gs ss cr0 cr2 cr3 cr4 xmm0 xmm1 xmm2 xmm3 xmm4 xmm5 "
    "xmm6 xmm7 xmm8 xmm9 xmm10 xmm11 xmm12 xmm13 xmm14 xmm15 ymm0 ymm1 "
    "ymm2 ymm3 ymm4 ymm5 ymm6 ymm7 st0 st1 st2 st3 st4 st5 st6 st7 "
    "byte word dword qword tword oword yword ptr offset short near far "
    "rel abs",
    "section segment global globl extern db dw dd dq dt resb resw resd resq "
    "equ times bits default align org use16 use32 use64 absolute struc "
    "endstruc istruc iend incbin .text .data .bss .rodata .globl .global "
    ".section .byte .word .long .quad .ascii .asciz .string .align .balign "
    ".p2align .equ .set .type .size .file .ident .intel_syntax .att_syntax "
    ".zero .space .skip .rept .endr .macro .endm .include .org .code16 "
    ".code32 .code64 .cfi_startproc .cfi_endproc .weak .hidden .comm",
    ";", "#", "/*", "*/",
    SF_NUM | SF_DQ | SF_SQ | SF_ICASE | SF_DOTDIR | SF_PCT | SF_LABEL },
};

static const struct syn *syn_cur;
static unsigned char *hl_buf;
static int hl_cap;
static const unsigned char *hl_cls;

static int synWordIn(const char *list, const char *w, int wl) {
  const char *p = list;
  if (!list) return 0;
  while (*p) {
    const char *e;
    while (*p == ' ') p++;
    e = p;
    while (*e && *e != ' ') e++;
    if ((int)(e - p) == wl && memcmp(p, w, (size_t)wl) == 0) return 1;
    p = e;
  }
  return 0;
}

static int synMatches(const char *match, const char *base) {
  size_t bl = strlen(base);
  const char *p = match;
  while (*p) {
    const char *e;
    size_t tl;
    while (*p == ' ') p++;
    e = p;
    while (*e && *e != ' ') e++;
    tl = (size_t)(e - p);
    if (tl > 0) {
      if (tl == bl && memcmp(p, base, tl) == 0) return 1;
      if (p[0] == '.' && bl > tl && memcmp(base + bl - tl, p, tl) == 0) return 1;
    }
    p = e;
  }
  return 0;
}

static const struct syn *synByName(const char *name) {
  size_t i;
  for (i = 0; i < sizeof(syntaxes) / sizeof(syntaxes[0]); i++)
    if (!strcmp(syntaxes[i].name, name)) return &syntaxes[i];
  return NULL;
}

static const struct syn *synFor(void) {
  size_t i;
  if (E.filename) {
    const char *slash = strrchr(E.filename, '/');
    const char *base = slash ? slash + 1 : E.filename;
    for (i = 0; i < sizeof(syntaxes) / sizeof(syntaxes[0]); i++)
      if (synMatches(syntaxes[i].match, base)) return &syntaxes[i];
  }
  if (E.numrows > 0 && E.row[0].size > 2 &&
      E.row[0].chars[0] == '#' && E.row[0].chars[1] == '!') {
    char line[128];
    int n = E.row[0].size < 127 ? E.row[0].size : 127;
    memcpy(line, E.row[0].chars, (size_t)n);
    line[n] = '\0';
    if (strstr(line, "python")) return synByName("python");
    if (strstr(line, "node") || strstr(line, "deno")) return synByName("javascript");
    if (strstr(line, "sh")) return synByName("shell");
  }
  return NULL;
}

static int synStarts(const char *p, int n, int i, const char *s) {
  int l = (int)strlen(s);
  return l > 0 && i + l <= n && memcmp(p + i, s, (size_t)l) == 0;
}

static int synIdent(int c) { return isalnum(c) || c == '_' || c >= 0x80; }

static int synFindEnd(const char *p, int n, int i, const char *term, int esc) {
  int tl = (int)strlen(term);
  for (; i < n; i++) {
    if (esc && p[i] == '\\') { i++; continue; }
    if (i + tl <= n && memcmp(p + i, term, (size_t)tl) == 0) return i + tl;
  }
  return -1;
}

static void synMark(unsigned char *out, int from, int to, int cls) {
  if (out && to > from) memset(out + from, cls, (size_t)(to - from));
}

static int synAtLineStart(const char *p, int i) {
  int k;
  for (k = 0; k < i; k++)
    if (p[k] != ' ' && p[k] != '\t') return 0;
  return 1;
}

static void synRow(const struct syn *s, const erow *row, int *state,
                   unsigned char *out) {
  const char *p = row->chars;
  int n = row->size, i = 0, st = *state, f = s->flags;

  if (out) memset(out, CL_PLAIN, (size_t)n);

  if (st != ST_NONE) {
    const char *term = st == ST_BLOCK ? s->be : st == ST_TDQ ? "\"\"\"" :
                       st == ST_TSQ ? "'''" : "`";
    int cls = st == ST_BLOCK ? CL_COMMENT : CL_STR;
    int e = synFindEnd(p, n, 0, term, st != ST_BLOCK);
    if (e < 0) { synMark(out, 0, n, cls); return; }
    synMark(out, 0, e, cls);
    i = e;
    st = ST_NONE;
  }

  while (i < n) {
    unsigned char c = (unsigned char)p[i];

    if ((s->lc && synStarts(p, n, i, s->lc)) ||
        (s->lc2 && synStarts(p, n, i, s->lc2))) {
      if (!(f & SF_HASHWS) || i == 0 || p[i - 1] == ' ' || p[i - 1] == '\t' ||
          p[i - 1] == ';' || p[i - 1] == '&' || p[i - 1] == '|' ||
          p[i - 1] == '(' || p[i - 1] == ')') {
        synMark(out, i, n, CL_COMMENT);
        break;
      }
    }
    if (s->bs && synStarts(p, n, i, s->bs)) {
      int e = synFindEnd(p, n, i + (int)strlen(s->bs), s->be, 0);
      if (e < 0) { synMark(out, i, n, CL_COMMENT); st = ST_BLOCK; break; }
      synMark(out, i, e, CL_COMMENT);
      i = e;
      continue;
    }

    if ((f & SF_PRE) && c == '#' && synAtLineStart(p, i)) {
      int j = i + 1, ws, wl;
      while (j < n && (p[j] == ' ' || p[j] == '\t')) j++;
      ws = j;
      while (j < n && synIdent((unsigned char)p[j])) j++;
      wl = j - ws;
      synMark(out, i, j, CL_PRE);
      i = j;
      if ((wl == 7 && !memcmp(p + ws, "include", 7)) ||
          (wl == 12 && !memcmp(p + ws, "include_next", 12))) {
        int k = i;
        while (k < n && (p[k] == ' ' || p[k] == '\t')) k++;
        if (k < n && p[k] == '<') {
          int e = k + 1;
          while (e < n && p[e] != '>') e++;
          if (e < n) e++;
          synMark(out, k, e, CL_STR);
          i = e;
        }
      }
      continue;
    }

    if ((f & SF_TRIPLE) &&
        (synStarts(p, n, i, "\"\"\"") || synStarts(p, n, i, "'''"))) {
      int dq = c == '"';
      int e = synFindEnd(p, n, i + 3, dq ? "\"\"\"" : "'''", 1);
      if (e < 0) { synMark(out, i, n, CL_STR); st = dq ? ST_TDQ : ST_TSQ; break; }
      synMark(out, i, e, CL_STR);
      i = e;
      continue;
    }
    if ((f & SF_DQ) && c == '"') {
      int j = i + 1;
      while (j < n && p[j] != '"') {
        if (p[j] == '\\') j++;
        j++;
      }
      if (j < n) j++; else j = n;
      synMark(out, i, j, CL_STR);
      i = j;
      continue;
    }
    if ((f & SF_BT) && c == '`') {
      int e = synFindEnd(p, n, i + 1, "`", 1);
      if (e < 0) { synMark(out, i, n, CL_STR); st = ST_BT; break; }
      synMark(out, i, e, CL_STR);
      i = e;
      continue;
    }
    if (c == '\'') {
      if (f & SF_CHAR) {
        int j = i + 1;
        if (j < n && p[j] == '\\') {
          j += 2;
          while (j < n && p[j] != '\'' && j < i + 12) j++;
        } else if (j < n) {
          int l;
          utf8Decode((const unsigned char *)p + j, n - j, &l);
          j += l;
        }
        if (j < n && p[j] == '\'') {
          synMark(out, i, j + 1, CL_STR);
          i = j + 1;
          continue;
        }
      } else if ((f & SF_SQ) &&
                 (!(f & SF_SQB) || i == 0 || !synIdent((unsigned char)p[i - 1]))) {
        int j = i + 1;
        while (j < n && p[j] != '\'') {
          if (!(f & SF_SQRAW) && p[j] == '\\') j++;
          j++;
        }
        if (j < n) j++; else j = n;
        synMark(out, i, j, CL_STR);
        i = j;
        continue;
      }
    }

    if ((f & SF_DOLLAR) && c == '$' && i + 1 < n) {
      unsigned char d = (unsigned char)p[i + 1];
      int j = i + 1;
      if (d == '{' || (d == '(' && (f & SF_MAKEVAR))) {
        char close = d == '{' ? '}' : ')';
        while (j < n && p[j] != close) j++;
        if (j < n) j++;
      } else if (d == '(') {
        j = i + 2;
      } else if (isalnum(d) || d == '_') {
        while (j < n && (isalnum((unsigned char)p[j]) || p[j] == '_')) j++;
      } else if (d && strchr("@*#?$!-<^%", d)) {
        j = i + 2;
      }
      if (j > i + 1) {
        synMark(out, i, j, CL_KW2);
        i = j;
        continue;
      }
    }

    if ((f & SF_PCT) && c == '%' && i + 1 < n &&
        synIdent((unsigned char)p[i + 1])) {
      char lw[40];
      int j = i + 1, wl, k, cls = CL_PRE;
      while (j < n && synIdent((unsigned char)p[j])) j++;
      wl = j - i - 1;
      if (wl < (int)sizeof(lw)) {
        for (k = 0; k < wl; k++) lw[k] = (char)tolower((unsigned char)p[i + 1 + k]);
        if (synWordIn(s->kw2, lw, wl)) cls = CL_KW2;
      }
      synMark(out, i, j, cls);
      i = j;
      continue;
    }

    if ((f & SF_DOTDIR) && c == '.' && i + 1 < n && isalpha((unsigned char)p[i + 1]) &&
        (i == 0 || !synIdent((unsigned char)p[i - 1]))) {
      char lw[40];
      int j = i + 1, wl, k, cls = CL_PLAIN;
      while (j < n && (synIdent((unsigned char)p[j]) || p[j] == '.')) j++;
      wl = j - i;
      if (j < n && p[j] == ':' && synAtLineStart(p, i)) cls = CL_FUNC;
      else if (wl < (int)sizeof(lw)) {
        for (k = 0; k < wl; k++) lw[k] = (char)tolower((unsigned char)p[i + k]);
        if (synWordIn(s->kw3, lw, wl)) cls = CL_PRE;
        else if (wl > 2 && p[i + 1] == 'L') cls = CL_FUNC;
      }
      synMark(out, i, j, cls);
      i = j;
      continue;
    }

    if ((f & SF_NUM) && isdigit(c) && (i == 0 || !synIdent((unsigned char)p[i - 1]))) {
      int j = i;
      while (j < n) {
        if (isalnum((unsigned char)p[j]) || p[j] == '_') j++;
        else if (p[j] == '.' && j + 1 < n && isdigit((unsigned char)p[j + 1])) j++;
        else break;
      }
      synMark(out, i, j, CL_NUM);
      i = j;
      continue;
    }

    if (isalpha(c) || c == '_' || c >= 0x80) {
      char lw[40];
      const char *w = p + i;
      int j = i, wl, cls = CL_PLAIN;
      while (j < n && synIdent((unsigned char)p[j])) j++;
      wl = j - i;
      if ((f & SF_ICASE) && wl < (int)sizeof(lw)) {
        int k;
        for (k = 0; k < wl; k++) lw[k] = (char)tolower((unsigned char)p[i + k]);
        w = lw;
      }
      if (synWordIn(s->kw1, w, wl)) cls = CL_KW1;
      else if (synWordIn(s->kw2, w, wl)) cls = CL_KW2;
      else if (synWordIn(s->kw3, w, wl)) cls = CL_PRE;
      else if ((f & SF_FUNC) && j < n &&
               (p[j] == '(' || (p[j] == '!' && j + 1 < n &&
                                (p[j + 1] == '(' || p[j + 1] == '[' || p[j + 1] == '{'))))
        cls = CL_FUNC;
      else if ((f & SF_LABEL) && j < n && p[j] == ':' && synAtLineStart(p, i))
        cls = CL_FUNC;
      synMark(out, i, j, cls);
      i = j;
      continue;
    }

    i++;
  }
  *state = st;
}

void editorHlSwitch(struct abuf *ab, int *cur, int want) {
  if (*cur == want) return;
  if (want == CL_PLAIN) {
    abAppend(ab, "\x1b[39m", 5);
    abAppend(ab, E.col_str[COL_TEXT_FG], (int)strlen(E.col_str[COL_TEXT_FG]));
  } else {
    abAppend(ab, cl_esc[want], (int)strlen(cl_esc[want]));
  }
  *cur = want;
}

int editorHlBegin(void) {
  int hstate = ST_NONE;
  syn_cur = synFor();
  if (syn_cur && (syn_cur->bs || (syn_cur->flags & (SF_TRIPLE | SF_BT)))) {
    int r, from = E.rowoff > 3000 ? E.rowoff - 3000 : 0;
    for (r = from; r < E.rowoff && r < E.numrows; r++)
      synRow(syn_cur, &E.row[r], &hstate, NULL);
  }
  return hstate;
}

int editorHlActive(void) {
  return syn_cur != NULL;
}

void editorHlRow(erow *hr, int *hstate) {
  if (hr->size + 1 > hl_cap) {
    hl_cap = hr->size + 64;
    hl_buf = xrealloc(hl_buf, (size_t)hl_cap);
  }
  synRow(syn_cur, hr, hstate, hl_buf);
  hl_cls = hl_buf;
}

void editorHlEnd(void) {
  hl_cls = NULL;
}

const unsigned char *editorHlClasses(void) {
  return hl_cls;
}
