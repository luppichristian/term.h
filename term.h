// term.h
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <wchar.h>

#define TRGB(r, g, b)                                                          \
  (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | ((uint32_t)(b)))

#define TDEFAULT 0xffffffffu

#define TKEY_NONE 0
#define TKEY_ESCAPE 27
#define TKEY_BACKSPACE 8
#define TKEY_ENTER 13
#define TKEY_TAB 9

#define TKEY_SPECIAL_BASE 0x10000
#define TKEY_LEFT (TKEY_SPECIAL_BASE + 1)
#define TKEY_RIGHT (TKEY_SPECIAL_BASE + 2)
#define TKEY_UP (TKEY_SPECIAL_BASE + 3)
#define TKEY_DOWN (TKEY_SPECIAL_BASE + 4)
#define TKEY_RESIZE (TKEY_SPECIAL_BASE + 5)

// Platform requirements:
// Win32: requires a Windows console with input/output handles that support
// console modes, and output must support virtual terminal escape sequences.
// Unix/Linux: requires stdin/stdout to be attached to a TTY, termios raw mode
// support, ANSI escape sequence support, and SIGWINCH/ioctl(TIOCGWINSZ) for
// resize handling.

// Initializes terminal state and switches the console into raw rendering mode.
bool tinit(void);
// Restores the console state and releases all terminal resources.
void tquit(void);

// Returns the current terminal width in character cells.
int twidth(void);
// Returns the current terminal height in character cells.
int theight(void);
// Returns the terminal's default foreground color, or TDEFAULT if unknown.
uint32_t tdefaultfg(void);
// Returns the terminal's default background color, or TDEFAULT if unknown.
uint32_t tdefaultbg(void);

// Returns the next key if one is available, or TKEY_NONE otherwise.
int tpoll(void);
// Waits until a key or resize event is available and returns it.
int twait(void);

// Fills the back buffer with blank cells using the terminal default colors.
void tclear(void);
// Writes a single character into the back buffer at the given position.
void tput(int x, int y, wchar_t ch, uint32_t fg, uint32_t bg);
// Writes a null-terminated string into the back buffer starting at the given
// position.
void twrite(int x, int y, const wchar_t *text, uint32_t fg, uint32_t bg);
// Flushes the back buffer to the terminal display.
void trender(void);

#ifdef TIMPLEMENTATION

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

typedef struct tcell_t {
  wchar_t ch;
  uint32_t fg;
  uint32_t bg;
} tcell_t;

typedef struct tstate_t {
  HANDLE in;
  HANDLE out;

  DWORD oldInMode;
  DWORD oldOutMode;

  uint32_t defaultFg;
  uint32_t defaultBg;

  int width;
  int height;

  tcell_t *cells;
  tcell_t *oldCells;

  char *outBuffer;
  int outBufferSize;
  int outBufferCount;
} tstate_t;

static tstate_t gTerm = {0};

static uint32_t tcolorref_to_rgb(COLORREF color) {
  return TRGB(GetRValue(color), GetGValue(color), GetBValue(color));
}

static uint32_t tansi_index_to_rgb(uint8_t index) {
  static const uint32_t palette[16] = {
      TRGB(0, 0, 0),       TRGB(128, 0, 0),     TRGB(0, 128, 0),
      TRGB(128, 128, 0),   TRGB(0, 0, 128),     TRGB(128, 0, 128),
      TRGB(0, 128, 128),   TRGB(192, 192, 192), TRGB(128, 128, 128),
      TRGB(255, 0, 0),     TRGB(0, 255, 0),     TRGB(255, 255, 0),
      TRGB(0, 0, 255),     TRGB(255, 0, 255),   TRGB(0, 255, 255),
      TRGB(255, 255, 255),
  };

  return palette[index & 0x0f];
}

static void tloaddefaults(void) {
  CONSOLE_SCREEN_BUFFER_INFO info;
  if (!GetConsoleScreenBufferInfo(gTerm.out, &info)) {
    gTerm.defaultFg = TDEFAULT;
    gTerm.defaultBg = TDEFAULT;
    return;
  }

  uint8_t fgIndex = (uint8_t)(info.wAttributes & 0x0f);
  uint8_t bgIndex = (uint8_t)((info.wAttributes >> 4) & 0x0f);

  CONSOLE_SCREEN_BUFFER_INFOEX infoEx;
  memset(&infoEx, 0, sizeof(infoEx));
  infoEx.cbSize = sizeof(infoEx);

  if (GetConsoleScreenBufferInfoEx(gTerm.out, &infoEx)) {
    gTerm.defaultFg = tcolorref_to_rgb(infoEx.ColorTable[fgIndex]);
    gTerm.defaultBg = tcolorref_to_rgb(infoEx.ColorTable[bgIndex]);
    return;
  }

  gTerm.defaultFg = tansi_index_to_rgb(fgIndex);
  gTerm.defaultBg = tansi_index_to_rgb(bgIndex);
}

static void tappend(const char *text) {
  int len = (int)strlen(text);

  if (gTerm.outBufferCount + len >= gTerm.outBufferSize)
    return;

  memcpy(gTerm.outBuffer + gTerm.outBufferCount, text, len);
  gTerm.outBufferCount += len;
}

static void tappendf(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);

  int available = gTerm.outBufferSize - gTerm.outBufferCount;
  if (available > 0) {
    int written =
        vsnprintf(gTerm.outBuffer + gTerm.outBufferCount, available, fmt, args);
    if (written > 0 && written < available)
      gTerm.outBufferCount += written;
  }

  va_end(args);
}

static void tappendutf8(wchar_t ch) {
  char buffer[8];
  int len = WideCharToMultiByte(CP_UTF8, 0, &ch, 1, buffer, sizeof(buffer),
                                NULL, NULL);

  if (len <= 0)
    return;

  if (gTerm.outBufferCount + len >= gTerm.outBufferSize)
    return;

  memcpy(gTerm.outBuffer + gTerm.outBufferCount, buffer, len);
  gTerm.outBufferCount += len;
}

static void tupdatesz(void) {
  CONSOLE_SCREEN_BUFFER_INFO info;
  GetConsoleScreenBufferInfo(gTerm.out, &info);

  gTerm.width = info.srWindow.Right - info.srWindow.Left + 1;
  gTerm.height = info.srWindow.Bottom - info.srWindow.Top + 1;
}

static bool tallocbuff(void) {
  int count = gTerm.width * gTerm.height;

  free(gTerm.cells);
  free(gTerm.oldCells);
  free(gTerm.outBuffer);

  gTerm.cells = calloc(count, sizeof(tcell_t));
  gTerm.oldCells = calloc(count, sizeof(tcell_t));

  gTerm.outBufferSize = count * 64 + 4096;
  gTerm.outBuffer = malloc(gTerm.outBufferSize);

  if (!gTerm.cells || !gTerm.oldCells || !gTerm.outBuffer)
    return false;

  for (int i = 0; i < count; ++i) {
    gTerm.cells[i].ch = L' ';
    gTerm.cells[i].fg = TDEFAULT;
    gTerm.cells[i].bg = TDEFAULT;

    gTerm.oldCells[i].ch = 0;
    gTerm.oldCells[i].fg = TDEFAULT;
    gTerm.oldCells[i].bg = TDEFAULT;
  }

  return true;
}

static void tresizebuffs(void) {
  tupdatesz();
  tallocbuff();

  fputs("\x1b[2J\x1b[H", stdout);
  fflush(stdout);
}

bool tinit(void) {
  memset(&gTerm, 0, sizeof(gTerm));

  gTerm.in = GetStdHandle(STD_INPUT_HANDLE);
  gTerm.out = GetStdHandle(STD_OUTPUT_HANDLE);

  if (gTerm.in == INVALID_HANDLE_VALUE || gTerm.out == INVALID_HANDLE_VALUE)
    return false;

  if (!GetConsoleMode(gTerm.in, &gTerm.oldInMode))
    return false;

  if (!GetConsoleMode(gTerm.out, &gTerm.oldOutMode))
    return false;

  DWORD inMode = gTerm.oldInMode;
  inMode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);
  inMode |= ENABLE_WINDOW_INPUT | ENABLE_EXTENDED_FLAGS;
  inMode &= ~ENABLE_QUICK_EDIT_MODE;

  if (!SetConsoleMode(gTerm.in, inMode))
    return false;

  DWORD outMode = gTerm.oldOutMode;
  outMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;

  if (!SetConsoleMode(gTerm.out, outMode))
    return false;

  tloaddefaults();

  tupdatesz();

  if (!tallocbuff())
    return false;

  fputs("\x1b[?25l\x1b[2J\x1b[H", stdout);
  fflush(stdout);

  return true;
}

void tquit(void) {
  fputs("\x1b[0m\x1b[?25h\x1b[2J\x1b[H", stdout);
  fflush(stdout);

  if (gTerm.in)
    SetConsoleMode(gTerm.in, gTerm.oldInMode);

  if (gTerm.out)
    SetConsoleMode(gTerm.out, gTerm.oldOutMode);

  free(gTerm.cells);
  free(gTerm.oldCells);
  free(gTerm.outBuffer);

  memset(&gTerm, 0, sizeof(gTerm));
}

int twidth(void) { return gTerm.width; }

int theight(void) { return gTerm.height; }

uint32_t tdefaultfg(void) { return gTerm.defaultFg; }

uint32_t tdefaultbg(void) { return gTerm.defaultBg; }

static int ttranslate(INPUT_RECORD *r) {
  if (r->EventType == WINDOW_BUFFER_SIZE_EVENT) {
    tresizebuffs();
    return TKEY_RESIZE;
  }

  if (r->EventType != KEY_EVENT)
    return TKEY_NONE;

  KEY_EVENT_RECORD *k = &r->Event.KeyEvent;

  if (!k->bKeyDown)
    return TKEY_NONE;

  switch (k->wVirtualKeyCode) {
  case VK_ESCAPE:
    return TKEY_ESCAPE;
  case VK_RETURN:
    return TKEY_ENTER;
  case VK_BACK:
    return TKEY_BACKSPACE;
  case VK_TAB:
    return TKEY_TAB;

  case VK_LEFT:
    return TKEY_LEFT;
  case VK_RIGHT:
    return TKEY_RIGHT;
  case VK_UP:
    return TKEY_UP;
  case VK_DOWN:
    return TKEY_DOWN;

  default:
    break;
  }

  if (k->uChar.UnicodeChar != 0)
    return (int)k->uChar.UnicodeChar;

  return TKEY_NONE;
}

int tpoll(void) {
  DWORD available = 0;

  if (!GetNumberOfConsoleInputEvents(gTerm.in, &available))
    return TKEY_NONE;

  while (available > 0) {
    INPUT_RECORD r;
    DWORD readCount = 0;

    if (!ReadConsoleInputW(gTerm.in, &r, 1, &readCount))
      return TKEY_NONE;

    if (readCount > 0) {
      int key = ttranslate(&r);
      if (key != TKEY_NONE)
        return key;
    }

    if (!GetNumberOfConsoleInputEvents(gTerm.in, &available))
      return TKEY_NONE;
  }

  return TKEY_NONE;
}

int twait(void) {
  for (;;) {
    INPUT_RECORD r;
    DWORD readCount = 0;

    if (!ReadConsoleInputW(gTerm.in, &r, 1, &readCount))
      return TKEY_NONE;

    if (readCount == 0)
      continue;

    int key = ttranslate(&r);
    if (key != TKEY_NONE)
      return key;
  }
}

void tclear(void) {
  int count = gTerm.width * gTerm.height;

  for (int i = 0; i < count; ++i) {
    gTerm.cells[i].ch = L' ';
    gTerm.cells[i].fg = TDEFAULT;
    gTerm.cells[i].bg = TDEFAULT;
  }
}

void tput(int x, int y, wchar_t ch, uint32_t fg, uint32_t bg) {
  if (x < 0 || y < 0 || x >= gTerm.width || y >= gTerm.height)
    return;

  int index = y * gTerm.width + x;

  gTerm.cells[index].ch = ch;
  gTerm.cells[index].fg = fg;
  gTerm.cells[index].bg = bg;
}

void twrite(int x, int y, const wchar_t *text, uint32_t fg, uint32_t bg) {
  if (!text)
    return;

  for (int i = 0; text[i]; ++i)
    tput(x + i, y, text[i], fg, bg);
}

void trender(void) {
  gTerm.outBufferCount = 0;

  uint32_t currentFg = TDEFAULT;
  uint32_t currentBg = TDEFAULT;

  for (int y = 0; y < gTerm.height; ++y) {
    for (int x = 0; x < gTerm.width; ++x) {
      int index = y * gTerm.width + x;

      tcell_t *cell = &gTerm.cells[index];
      tcell_t *old = &gTerm.oldCells[index];

      if (cell->ch == old->ch && cell->fg == old->fg && cell->bg == old->bg) {
        continue;
      }

      tappendf("\x1b[%d;%dH", y + 1, x + 1);

      if (cell->fg != currentFg) {
        if (cell->fg == TDEFAULT) {
          tappend("\x1b[39m");
        } else {
          uint8_t r = (uint8_t)((cell->fg >> 16) & 0xff);
          uint8_t g = (uint8_t)((cell->fg >> 8) & 0xff);
          uint8_t b = (uint8_t)(cell->fg & 0xff);
          tappendf("\x1b[38;2;%u;%u;%um", r, g, b);
        }
        currentFg = cell->fg;
      }

      if (cell->bg != currentBg) {
        if (cell->bg == TDEFAULT) {
          tappend("\x1b[49m");
        } else {
          uint8_t r = (uint8_t)((cell->bg >> 16) & 0xff);
          uint8_t g = (uint8_t)((cell->bg >> 8) & 0xff);
          uint8_t b = (uint8_t)(cell->bg & 0xff);
          tappendf("\x1b[48;2;%u;%u;%um", r, g, b);
        }
        currentBg = cell->bg;
      }

      tappendutf8(cell->ch);

      *old = *cell;
    }
  }

  tappend("\x1b[0m");

  if (gTerm.outBufferCount > 0) {
    fwrite(gTerm.outBuffer, 1, gTerm.outBufferCount, stdout);
    fflush(stdout);
  }
}

#else

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

typedef struct tcell_t {
  wchar_t ch;
  uint32_t fg;
  uint32_t bg;
} tcell_t;

typedef struct tstate_t {
  int inFd;
  int outFd;

  struct termios oldTermios;
  int oldFlags;
  void (*oldWinchHandler)(int);

  bool hasOldTermios;
  bool hasOldFlags;
  bool hasOldWinchHandler;

  uint32_t defaultFg;
  uint32_t defaultBg;

  int width;
  int height;

  tcell_t *cells;
  tcell_t *oldCells;

  char *outBuffer;
  int outBufferSize;
  int outBufferCount;

  unsigned char inBuffer[256];
  int inBufferStart;
  int inBufferCount;
} tstate_t;

static tstate_t gTerm = {0};
static volatile sig_atomic_t gTermResizePending = 0;

#define TRGB(r, g, b)                                                          \
  (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | ((uint32_t)(b)))

static void tappend(const char *text) {
  int len = (int)strlen(text);

  if (gTerm.outBufferCount + len >= gTerm.outBufferSize)
    return;

  memcpy(gTerm.outBuffer + gTerm.outBufferCount, text, len);
  gTerm.outBufferCount += len;
}

static void tappendf(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);

  int available = gTerm.outBufferSize - gTerm.outBufferCount;
  if (available > 0) {
    int written =
        vsnprintf(gTerm.outBuffer + gTerm.outBufferCount, available, fmt, args);
    if (written > 0 && written < available)
      gTerm.outBufferCount += written;
  }

  va_end(args);
}

static void tappendutf8(wchar_t ch) {
  char buffer[MB_LEN_MAX];
  mbstate_t state;
  memset(&state, 0, sizeof(state));

  size_t len = wcrtomb(buffer, ch, &state);
  if (len == (size_t)-1) {
    buffer[0] = '?';
    len = 1;
  }

  if (gTerm.outBufferCount + (int)len >= gTerm.outBufferSize)
    return;

  memcpy(gTerm.outBuffer + gTerm.outBufferCount, buffer, len);
  gTerm.outBufferCount += (int)len;
}

static void thandle_winch(int sig) {
  (void)sig;
  gTermResizePending = 1;
}

static void tupdatesz(void) {
  struct winsize ws;

  if (ioctl(gTerm.outFd, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 &&
      ws.ws_row > 0) {
    gTerm.width = ws.ws_col;
    gTerm.height = ws.ws_row;
    return;
  }

  gTerm.width = 80;
  gTerm.height = 25;
}

static bool tallocbuff(void) {
  int count = gTerm.width * gTerm.height;

  free(gTerm.cells);
  free(gTerm.oldCells);
  free(gTerm.outBuffer);

  gTerm.cells = calloc(count, sizeof(tcell_t));
  gTerm.oldCells = calloc(count, sizeof(tcell_t));

  gTerm.outBufferSize = count * 64 + 4096;
  gTerm.outBuffer = malloc(gTerm.outBufferSize);

  if (!gTerm.cells || !gTerm.oldCells || !gTerm.outBuffer)
    return false;

  for (int i = 0; i < count; ++i) {
    gTerm.cells[i].ch = L' ';
    gTerm.cells[i].fg = TDEFAULT;
    gTerm.cells[i].bg = TDEFAULT;

    gTerm.oldCells[i].ch = 0;
    gTerm.oldCells[i].fg = TDEFAULT;
    gTerm.oldCells[i].bg = TDEFAULT;
  }

  return true;
}

static void tresizebuffs(void) {
  gTermResizePending = 0;
  tupdatesz();
  tallocbuff();

  fputs("\x1b[2J\x1b[H", stdout);
  fflush(stdout);
}

static void tunread_input_byte(unsigned char byte) {
  if (gTerm.inBufferStart > 0) {
    gTerm.inBufferStart--;
    gTerm.inBuffer[gTerm.inBufferStart] = byte;
    gTerm.inBufferCount++;
    return;
  }

  if (gTerm.inBufferCount >= (int)sizeof(gTerm.inBuffer))
    return;

  memmove(gTerm.inBuffer + 1, gTerm.inBuffer, gTerm.inBufferCount);
  gTerm.inBuffer[0] = byte;
  gTerm.inBufferCount++;
}

static bool tfill_input_buffer(int timeoutMs) {
  if (gTerm.inBufferCount > 0)
    return true;

  for (;;) {
    struct pollfd pfd;
    pfd.fd = gTerm.inFd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    int result = poll(&pfd, 1, timeoutMs);
    if (result == 0)
      return false;

    if (result < 0) {
      if (errno == EINTR) {
        if (gTermResizePending)
          return false;
        continue;
      }

      return false;
    }

    if ((pfd.revents & POLLIN) == 0)
      return false;

    ssize_t count = read(gTerm.inFd, gTerm.inBuffer, sizeof(gTerm.inBuffer));
    if (count > 0) {
      gTerm.inBufferStart = 0;
      gTerm.inBufferCount = (int)count;
      return true;
    }

    if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
      return false;

    if (count < 0 && errno == EINTR) {
      if (gTermResizePending)
        return false;
      continue;
    }

    return false;
  }
}

static bool tread_input_byte(int timeoutMs, unsigned char *byte) {
  if (!byte)
    return false;

  if (!tfill_input_buffer(timeoutMs))
    return false;

  *byte = gTerm.inBuffer[gTerm.inBufferStart++];
  gTerm.inBufferCount--;

  if (gTerm.inBufferCount == 0)
    gTerm.inBufferStart = 0;

  return true;
}

static int tdecode_utf8(unsigned char firstByte, bool blocking) {
  unsigned char bytes[4];
  int length = 0;

  bytes[0] = firstByte;

  if ((firstByte & 0xe0) == 0xc0)
    length = 2;
  else if ((firstByte & 0xf0) == 0xe0)
    length = 3;
  else if ((firstByte & 0xf8) == 0xf0)
    length = 4;
  else
    return TKEY_NONE;

  for (int i = 1; i < length; ++i) {
    if (!tread_input_byte(blocking ? -1 : 0, &bytes[i]))
      return TKEY_NONE;

    if ((bytes[i] & 0xc0) != 0x80)
      return TKEY_NONE;
  }

  if (length == 2)
    return ((bytes[0] & 0x1f) << 6) | (bytes[1] & 0x3f);

  if (length == 3)
    return ((bytes[0] & 0x0f) << 12) | ((bytes[1] & 0x3f) << 6) |
           (bytes[2] & 0x3f);

  return ((bytes[0] & 0x07) << 18) | ((bytes[1] & 0x3f) << 12) |
         ((bytes[2] & 0x3f) << 6) | (bytes[3] & 0x3f);
}

static int tread_escape_sequence(bool blocking) {
  unsigned char next = 0;
  int timeoutMs = blocking ? 25 : 0;

  if (!tread_input_byte(timeoutMs, &next))
    return TKEY_ESCAPE;

  if (next != '[' && next != 'O') {
    tunread_input_byte(next);
    return TKEY_ESCAPE;
  }

  if (!tread_input_byte(timeoutMs, &next))
    return TKEY_ESCAPE;

  switch (next) {
  case 'A':
    return TKEY_UP;
  case 'B':
    return TKEY_DOWN;
  case 'C':
    return TKEY_RIGHT;
  case 'D':
    return TKEY_LEFT;
  default:
    return TKEY_ESCAPE;
  }
}

static int tread_key(bool blocking) {
  unsigned char byte = 0;

  if (!tread_input_byte(blocking ? -1 : 0, &byte))
    return TKEY_NONE;

  if (byte == 0x1b)
    return tread_escape_sequence(blocking);

  if (byte == '\r' || byte == '\n')
    return TKEY_ENTER;

  if (byte == '\t')
    return TKEY_TAB;

  if (byte == 0x08 || byte == 0x7f)
    return TKEY_BACKSPACE;

  if (byte < 0x80)
    return byte;

  return tdecode_utf8(byte, blocking);
}

bool tinit(void) {
  memset(&gTerm, 0, sizeof(gTerm));
  gTerm.inFd = STDIN_FILENO;
  gTerm.outFd = STDOUT_FILENO;

  if (!isatty(gTerm.inFd) || !isatty(gTerm.outFd))
    return false;

  if (tcgetattr(gTerm.inFd, &gTerm.oldTermios) != 0)
    return false;
  gTerm.hasOldTermios = true;

  gTerm.oldFlags = fcntl(gTerm.inFd, F_GETFL, 0);
  if (gTerm.oldFlags < 0) {
    tquit();
    return false;
  }
  gTerm.hasOldFlags = true;
  gTerm.defaultFg = TDEFAULT;
  gTerm.defaultBg = TDEFAULT;

  struct termios raw = gTerm.oldTermios;
  raw.c_iflag &= (tcflag_t) ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_oflag &= (tcflag_t) ~(OPOST);
  raw.c_cflag |= (tcflag_t)CS8;
  raw.c_lflag &= (tcflag_t) ~(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 0;

  if (tcsetattr(gTerm.inFd, TCSAFLUSH, &raw) != 0) {
    tquit();
    return false;
  }

  if (fcntl(gTerm.inFd, F_SETFL, gTerm.oldFlags | O_NONBLOCK) != 0) {
    tquit();
    return false;
  }

  gTerm.oldWinchHandler = signal(SIGWINCH, thandle_winch);
  if (gTerm.oldWinchHandler == SIG_ERR) {
    tquit();
    return false;
  }
  gTerm.hasOldWinchHandler = true;

  tupdatesz();

  if (!tallocbuff()) {
    tquit();
    return false;
  }

  fputs("\x1b[?25l\x1b[2J\x1b[H", stdout);
  fflush(stdout);

  return true;
}

void tquit(void) {
  fputs("\x1b[0m\x1b[?25h\x1b[2J\x1b[H", stdout);
  fflush(stdout);

  if (gTerm.hasOldTermios)
    tcsetattr(gTerm.inFd, TCSAFLUSH, &gTerm.oldTermios);

  if (gTerm.hasOldFlags)
    fcntl(gTerm.inFd, F_SETFL, gTerm.oldFlags);

  if (gTerm.hasOldWinchHandler)
    signal(SIGWINCH, gTerm.oldWinchHandler);

  free(gTerm.cells);
  free(gTerm.oldCells);
  free(gTerm.outBuffer);

  memset(&gTerm, 0, sizeof(gTerm));
  gTermResizePending = 0;
}

int twidth(void) { return gTerm.width; }

int theight(void) { return gTerm.height; }

uint32_t tdefaultfg(void) { return gTerm.defaultFg; }

uint32_t tdefaultbg(void) { return gTerm.defaultBg; }

int tpoll(void) {
  if (gTermResizePending) {
    tresizebuffs();
    return TKEY_RESIZE;
  }

  return tread_key(false);
}

int twait(void) {
  for (;;) {
    if (gTermResizePending) {
      tresizebuffs();
      return TKEY_RESIZE;
    }

    int key = tread_key(true);
    if (key != TKEY_NONE)
      return key;
  }
}

void tclear(void) {
  int count = gTerm.width * gTerm.height;

  for (int i = 0; i < count; ++i) {
    gTerm.cells[i].ch = L' ';
    gTerm.cells[i].fg = TDEFAULT;
    gTerm.cells[i].bg = TDEFAULT;
  }
}

void tput(int x, int y, wchar_t ch, uint32_t fg, uint32_t bg) {
  if (x < 0 || y < 0 || x >= gTerm.width || y >= gTerm.height)
    return;

  int index = y * gTerm.width + x;

  gTerm.cells[index].ch = ch;
  gTerm.cells[index].fg = fg;
  gTerm.cells[index].bg = bg;
}

void twrite(int x, int y, const wchar_t *text, uint32_t fg, uint32_t bg) {
  if (!text)
    return;

  for (int i = 0; text[i]; ++i)
    tput(x + i, y, text[i], fg, bg);
}

void trender(void) {
  gTerm.outBufferCount = 0;

  uint32_t currentFg = TDEFAULT;
  uint32_t currentBg = TDEFAULT;

  for (int y = 0; y < gTerm.height; ++y) {
    for (int x = 0; x < gTerm.width; ++x) {
      int index = y * gTerm.width + x;

      tcell_t *cell = &gTerm.cells[index];
      tcell_t *old = &gTerm.oldCells[index];

      if (cell->ch == old->ch && cell->fg == old->fg && cell->bg == old->bg) {
        continue;
      }

      tappendf("\x1b[%d;%dH", y + 1, x + 1);

      if (cell->fg != currentFg) {
        if (cell->fg == TDEFAULT) {
          tappend("\x1b[39m");
        } else {
          uint8_t r = (uint8_t)((cell->fg >> 16) & 0xff);
          uint8_t g = (uint8_t)((cell->fg >> 8) & 0xff);
          uint8_t b = (uint8_t)(cell->fg & 0xff);
          tappendf("\x1b[38;2;%u;%u;%um", r, g, b);
        }
        currentFg = cell->fg;
      }

      if (cell->bg != currentBg) {
        if (cell->bg == TDEFAULT) {
          tappend("\x1b[49m");
        } else {
          uint8_t r = (uint8_t)((cell->bg >> 16) & 0xff);
          uint8_t g = (uint8_t)((cell->bg >> 8) & 0xff);
          uint8_t b = (uint8_t)(cell->bg & 0xff);
          tappendf("\x1b[48;2;%u;%u;%um", r, g, b);
        }
        currentBg = cell->bg;
      }

      tappendutf8(cell->ch);

      *old = *cell;
    }
  }

  tappend("\x1b[0m");

  if (gTerm.outBufferCount > 0) {
    fwrite(gTerm.outBuffer, 1, gTerm.outBufferCount, stdout);
    fflush(stdout);
  }
}

#endif

#endif

#ifdef __cplusplus
}
#endif
