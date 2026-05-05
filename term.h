// term.h
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <wchar.h>

typedef uint32_t tcolor_t;

// Colors are packed as 0xRRGGBBAA.
#define TRGBA(r, g, b, a)                                                      \
  ((((tcolor_t)(r) & 0xffu) << 24) | (((tcolor_t)(g) & 0xffu) << 16) |         \
   (((tcolor_t)(b) & 0xffu) << 8) | ((tcolor_t)(a) & 0xffu))

// Keymods, can be acquired with tkeymods()
#define TKEY_MOD_SHIFT 0x10000000
#define TKEY_MOD_ALT 0x20000000
#define TKEY_MOD_CTRL 0x40000000
#define TKEY_MOD_MASK (TKEY_MOD_SHIFT | TKEY_MOD_ALT | TKEY_MOD_CTRL)

// Special keys
#define TKEY_NONE 0
#define TKEY_ESCAPE 27
#define TKEY_BACKSPACE 8
#define TKEY_ENTER 13
#define TKEY_TAB 9
#define TKEY_SPECIAL_BASE 0x00110000
#define TKEY_INSERT (TKEY_SPECIAL_BASE + 1)
#define TKEY_DELETE (TKEY_SPECIAL_BASE + 2)
#define TKEY_HOME (TKEY_SPECIAL_BASE + 3)
#define TKEY_END (TKEY_SPECIAL_BASE + 4)
#define TKEY_PAGEUP (TKEY_SPECIAL_BASE + 5)
#define TKEY_PAGEDOWN (TKEY_SPECIAL_BASE + 6)
#define TKEY_LEFT (TKEY_SPECIAL_BASE + 7)
#define TKEY_RIGHT (TKEY_SPECIAL_BASE + 8)
#define TKEY_UP (TKEY_SPECIAL_BASE + 9)
#define TKEY_DOWN (TKEY_SPECIAL_BASE + 10)
#define TKEY_F1 (TKEY_SPECIAL_BASE + 11)
#define TKEY_F2 (TKEY_SPECIAL_BASE + 12)
#define TKEY_F3 (TKEY_SPECIAL_BASE + 13)
#define TKEY_F4 (TKEY_SPECIAL_BASE + 14)
#define TKEY_F5 (TKEY_SPECIAL_BASE + 15)
#define TKEY_F6 (TKEY_SPECIAL_BASE + 16)
#define TKEY_F7 (TKEY_SPECIAL_BASE + 17)
#define TKEY_F8 (TKEY_SPECIAL_BASE + 18)
#define TKEY_F9 (TKEY_SPECIAL_BASE + 19)
#define TKEY_F10 (TKEY_SPECIAL_BASE + 20)
#define TKEY_F11 (TKEY_SPECIAL_BASE + 21)
#define TKEY_F12 (TKEY_SPECIAL_BASE + 22)
#define TKEY_RESIZE (TKEY_SPECIAL_BASE + 23)

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
// Returns the terminal's default foreground color.
tcolor_t tdefaultfg(void);
// Returns the terminal's default background color.
tcolor_t tdefaultbg(void);

// Returns the next key if one is available, or TKEY_NONE otherwise.
int tpoll(void);
// Waits until a key or resize event is available and returns it.
int twait(void);
// Returns modifier bits for the most recent key returned by tpoll() or twait().
int tkeymods(void);

// Fills the back buffer with blank cells using the terminal default colors.
void tclear(void);
// Writes a single character into the back buffer at the given position.
void tput(int x, int y, wchar_t ch, tcolor_t fg, tcolor_t bg);
// Writes a null-terminated string into the back buffer starting at the given
// position.
void twrite(int x, int y, const wchar_t *text, tcolor_t fg, tcolor_t bg);
// Flushes the back buffer to the terminal display.
void trender(void);

#ifdef TIMPLEMENTATION

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tkey_ctrl_base(int value) {
  if (value >= 1 && value <= 26)
    return 'a' + value - 1;

  switch (value) {
  case 0:
    return ' ';
  case 28:
    return '\\';
  case 29:
    return ']';
  case 30:
    return '^';
  case 31:
    return '_';
  default:
    return TKEY_NONE;
  }
}

static int tkey_ansi_mods(int value) {
  if (value < 2 || value > 8)
    return 0;

  value -= 1;

  return ((value & 1) ? TKEY_MOD_SHIFT : 0) | ((value & 2) ? TKEY_MOD_ALT : 0) |
         ((value & 4) ? TKEY_MOD_CTRL : 0);
}

#ifdef _WIN32

#define TINPUT_BUFFER_SIZE 1024

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#ifndef CONSOLE_READ_NOWAIT
#define CONSOLE_READ_NOWAIT 0x0002
#endif

typedef BOOL(WINAPI *treadconsoleinputexw_fn)(HANDLE, PINPUT_RECORD, DWORD,
                                              LPDWORD, WORD);

typedef struct tcell_t {
  wchar_t ch;
  tcolor_t fg;
  tcolor_t bg;
} tcell_t;

typedef struct tstate_t {
  HANDLE in;
  HANDLE out;

  DWORD oldInMode;
  DWORD oldOutMode;

  tcolor_t defaultFg;
  tcolor_t defaultBg;

  int width;
  int height;

  int lastKeyMods;

  tcell_t *cells;
  tcell_t *oldCells;

  char *outBuffer;
  int outBufferSize;
  int outBufferCount;

  unsigned char inBuffer[TINPUT_BUFFER_SIZE];
  int inBufferStart;
  int inBufferCount;
} tstate_t;

static tstate_t gTerm = {0};
static volatile LONG gTermResizePending = 0;

static int tset_keymods(int key, int mods) {
  gTerm.lastKeyMods = (key == TKEY_NONE) ? 0 : (mods & TKEY_MOD_MASK);
  return key;
}

static int tmods_from_control_state(DWORD state) {
  int mods = 0;

  if (state & SHIFT_PRESSED)
    mods |= TKEY_MOD_SHIFT;

  if (state & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED))
    mods |= TKEY_MOD_ALT;

  if (state & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))
    mods |= TKEY_MOD_CTRL;

  return mods;
}

static tcolor_t tfallback_defaultfg(void) { return TRGBA(255, 255, 255, 255); }

static tcolor_t tfallback_defaultbg(void) { return TRGBA(0, 0, 0, 255); }

static tcolor_t tcolorref_to_rgba(COLORREF color) {
  return TRGBA(GetRValue(color), GetGValue(color), GetBValue(color), 255);
}

static tcolor_t tansi_index_to_rgba(uint8_t index) {
  static const tcolor_t palette[16] = {
      TRGBA(0, 0, 0, 255),       TRGBA(128, 0, 0, 255),
      TRGBA(0, 128, 0, 255),     TRGBA(128, 128, 0, 255),
      TRGBA(0, 0, 128, 255),     TRGBA(128, 0, 128, 255),
      TRGBA(0, 128, 128, 255),   TRGBA(192, 192, 192, 255),
      TRGBA(128, 128, 128, 255), TRGBA(255, 0, 0, 255),
      TRGBA(0, 255, 0, 255),     TRGBA(255, 255, 0, 255),
      TRGBA(0, 0, 255, 255),     TRGBA(255, 0, 255, 255),
      TRGBA(0, 255, 255, 255),   TRGBA(255, 255, 255, 255),
  };

  return palette[index & 0x0f];
}

static void tloaddefaults(void) {
  CONSOLE_SCREEN_BUFFER_INFO info;
  if (!GetConsoleScreenBufferInfo(gTerm.out, &info)) {
    gTerm.defaultFg = tfallback_defaultfg();
    gTerm.defaultBg = tfallback_defaultbg();
    return;
  }

  uint8_t fgIndex = (uint8_t)(info.wAttributes & 0x0f);
  uint8_t bgIndex = (uint8_t)((info.wAttributes >> 4) & 0x0f);

  CONSOLE_SCREEN_BUFFER_INFOEX infoEx;
  memset(&infoEx, 0, sizeof(infoEx));
  infoEx.cbSize = sizeof(infoEx);

  if (GetConsoleScreenBufferInfoEx(gTerm.out, &infoEx)) {
    gTerm.defaultFg = tcolorref_to_rgba(infoEx.ColorTable[fgIndex]);
    gTerm.defaultBg = tcolorref_to_rgba(infoEx.ColorTable[bgIndex]);
    return;
  }

  gTerm.defaultFg = tansi_index_to_rgba(fgIndex);
  gTerm.defaultBg = tansi_index_to_rgba(bgIndex);
}

static bool tqueue_input_bytes(const unsigned char *bytes, int count) {
  if (!bytes || count <= 0)
    return false;

  if (count > (int)sizeof(gTerm.inBuffer) - gTerm.inBufferCount)
    return false;

  if (gTerm.inBufferCount == 0) {
    gTerm.inBufferStart = 0;
  } else if (gTerm.inBufferStart + gTerm.inBufferCount + count >
             (int)sizeof(gTerm.inBuffer)) {
    memmove(gTerm.inBuffer, gTerm.inBuffer + gTerm.inBufferStart,
            gTerm.inBufferCount);
    gTerm.inBufferStart = 0;
  }

  memcpy(gTerm.inBuffer + gTerm.inBufferStart + gTerm.inBufferCount, bytes,
         count);
  gTerm.inBufferCount += count;
  return true;
}

static bool tqueue_utf8(wchar_t ch) {
  char buffer[8];
  int len = WideCharToMultiByte(CP_UTF8, 0, &ch, 1, buffer, sizeof(buffer),
                                NULL, NULL);

  if (len <= 0)
    return false;

  return tqueue_input_bytes((const unsigned char *)buffer, len);
}

static int tansi_mods_from_keymods(int mods) {
  int value = 1;

  if (mods & TKEY_MOD_SHIFT)
    value |= 1;

  if (mods & TKEY_MOD_ALT)
    value |= 2;

  if (mods & TKEY_MOD_CTRL)
    value |= 4;

  return value;
}

static bool tqueue_csi_final(char final, int mods) {
  char sequence[16];
  int len = 0;

  if (mods != 0)
    len = snprintf(sequence, sizeof(sequence), "\x1b[1;%d%c",
                   tansi_mods_from_keymods(mods), final);
  else
    len = snprintf(sequence, sizeof(sequence), "\x1b[%c", final);

  return len > 0 && len < (int)sizeof(sequence) &&
         tqueue_input_bytes((const unsigned char *)sequence, len);
}

static bool tqueue_ss3_final(char final, int mods) {
  char sequence[16];
  int len = 0;

  if (mods != 0)
    len = snprintf(sequence, sizeof(sequence), "\x1bO1;%d%c",
                   tansi_mods_from_keymods(mods), final);
  else
    len = snprintf(sequence, sizeof(sequence), "\x1bO%c", final);

  return len > 0 && len < (int)sizeof(sequence) &&
         tqueue_input_bytes((const unsigned char *)sequence, len);
}

static bool tqueue_csi_tilde(int code, int mods) {
  char sequence[16];
  int len = 0;

  if (mods != 0)
    len = snprintf(sequence, sizeof(sequence), "\x1b[%d;%d~", code,
                   tansi_mods_from_keymods(mods));
  else
    len = snprintf(sequence, sizeof(sequence), "\x1b[%d~", code);

  return len > 0 && len < (int)sizeof(sequence) &&
         tqueue_input_bytes((const unsigned char *)sequence, len);
}

static bool tqueue_key_event(const KEY_EVENT_RECORD *k) {
  if (!k || !k->bKeyDown)
    return true;

  int mods = tmods_from_control_state(k->dwControlKeyState);
  wchar_t ch = k->uChar.UnicodeChar;

  switch (k->wVirtualKeyCode) {
  case VK_UP:
    return tqueue_csi_final('A', mods);
  case VK_DOWN:
    return tqueue_csi_final('B', mods);
  case VK_RIGHT:
    return tqueue_csi_final('C', mods);
  case VK_LEFT:
    return tqueue_csi_final('D', mods);
  case VK_END:
    return tqueue_csi_final('F', mods);
  case VK_HOME:
    return tqueue_csi_final('H', mods);
  case VK_INSERT:
    return tqueue_csi_tilde(2, mods);
  case VK_DELETE:
    return tqueue_csi_tilde(3, mods);
  case VK_PRIOR:
    return tqueue_csi_tilde(5, mods);
  case VK_NEXT:
    return tqueue_csi_tilde(6, mods);
  case VK_F1:
    return tqueue_ss3_final('P', mods);
  case VK_F2:
    return tqueue_ss3_final('Q', mods);
  case VK_F3:
    return tqueue_ss3_final('R', mods);
  case VK_F4:
    return tqueue_ss3_final('S', mods);
  case VK_F5:
    return tqueue_csi_tilde(15, mods);
  case VK_F6:
    return tqueue_csi_tilde(17, mods);
  case VK_F7:
    return tqueue_csi_tilde(18, mods);
  case VK_F8:
    return tqueue_csi_tilde(19, mods);
  case VK_F9:
    return tqueue_csi_tilde(20, mods);
  case VK_F10:
    return tqueue_csi_tilde(21, mods);
  case VK_F11:
    return tqueue_csi_tilde(23, mods);
  case VK_F12:
    return tqueue_csi_tilde(24, mods);
  case VK_RETURN: {
    unsigned char byte = '\r';
    return tqueue_input_bytes(&byte, 1);
  }
  case VK_BACK: {
    unsigned char byte = 0x7f;
    return tqueue_input_bytes(&byte, 1);
  }
  case VK_TAB:
    if (mods & TKEY_MOD_SHIFT)
      return tqueue_csi_final('Z', mods & ~TKEY_MOD_SHIFT);
    else {
      unsigned char byte = '\t';
      return tqueue_input_bytes(&byte, 1);
    }
  case VK_ESCAPE: {
    unsigned char byte = 0x1b;
    return tqueue_input_bytes(&byte, 1);
  }
  default:
    break;
  }

  if (ch == 0)
    return true;

  if ((mods & TKEY_MOD_ALT) != 0) {
    unsigned char escape = 0x1b;
    if (!tqueue_input_bytes(&escape, 1))
      return false;
  }

  return tqueue_utf8(ch);
}

static bool tfill_input_buffer(int timeoutMs) {
  if (gTerm.inBufferCount > 0)
    return true;

  for (;;) {
    DWORD waitTimeout = timeoutMs < 0 ? INFINITE : (DWORD)timeoutMs;
    DWORD waitResult = WaitForSingleObject(gTerm.in, waitTimeout);

    if (waitResult == WAIT_TIMEOUT)
      return false;

    if (waitResult != WAIT_OBJECT_0)
      return false;

    INPUT_RECORD records[64];
    DWORD readCount = 0;

    static treadconsoleinputexw_fn readConsoleInputExW = NULL;
    static bool readConsoleInputExWLoaded = false;
    if (!readConsoleInputExWLoaded) {
      HMODULE kernel32 = GetModuleHandleA("kernel32.dll");
      if (kernel32) {
        FARPROC proc = GetProcAddress(kernel32, "ReadConsoleInputExW");
        memcpy(&readConsoleInputExW, &proc, sizeof(proc));
      }
      readConsoleInputExWLoaded = true;
    }

    if (readConsoleInputExW) {
      if (!readConsoleInputExW(gTerm.in, records, ARRAYSIZE(records),
                               &readCount, CONSOLE_READ_NOWAIT))
        return false;
    } else {
      if (!PeekConsoleInputW(gTerm.in, records, ARRAYSIZE(records), &readCount))
        return false;

      if (readCount > 0 &&
          !ReadConsoleInputW(gTerm.in, records, readCount, &readCount))
        return false;
    }

    if (readCount == 0) {
      if (timeoutMs == 0)
        return false;
      continue;
    }

    for (DWORD i = 0; i < readCount; ++i) {
      INPUT_RECORD *record = &records[i];

      if (record->EventType == WINDOW_BUFFER_SIZE_EVENT) {
        gTermResizePending = 1;
        continue;
      }

      if (record->EventType != KEY_EVENT)
        continue;

      if (!tqueue_key_event(&record->Event.KeyEvent))
        break;
    }

    if (gTerm.inBufferCount > 0)
      return true;

    if (gTermResizePending)
      return false;

    if (timeoutMs == 0)
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

static int tdecode_input_byte(unsigned char byte, bool blocking);

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

static int tparse_ansi_params(const char *sequence, int length, int *params,
                              int maxParams) {
  int count = 0;
  int value = 0;
  bool hasValue = false;

  for (int i = 0; i < length - 1; ++i) {
    char ch = sequence[i];

    if (ch >= '0' && ch <= '9') {
      value = value * 10 + (ch - '0');
      hasValue = true;
      continue;
    }

    if (ch == ';') {
      if (count < maxParams)
        params[count++] = hasValue ? value : 0;
      value = 0;
      hasValue = false;
      continue;
    }

    return count;
  }

  if ((hasValue || count > 0) && count < maxParams)
    params[count++] = hasValue ? value : 0;

  return count;
}

static int tdecode_csi_sequence(const char *sequence, int length) {
  int params[4] = {0};
  int paramCount = tparse_ansi_params(sequence, length, params, 4);
  int mods = paramCount >= 2 ? tkey_ansi_mods(params[paramCount - 1]) : 0;
  char final = sequence[length - 1];

  switch (final) {
  case 'A':
    return tset_keymods(TKEY_UP, mods);
  case 'B':
    return tset_keymods(TKEY_DOWN, mods);
  case 'C':
    return tset_keymods(TKEY_RIGHT, mods);
  case 'D':
    return tset_keymods(TKEY_LEFT, mods);
  case 'F':
    return tset_keymods(TKEY_END, mods);
  case 'H':
    return tset_keymods(TKEY_HOME, mods);
  case 'P':
    return tset_keymods(TKEY_F1, mods);
  case 'Q':
    return tset_keymods(TKEY_F2, mods);
  case 'R':
    return tset_keymods(TKEY_F3, mods);
  case 'S':
    return tset_keymods(TKEY_F4, mods);
  case 'Z':
    return tset_keymods(TKEY_TAB, TKEY_MOD_SHIFT | mods);
  case '~':
    break;
  default:
    return TKEY_NONE;
  }

  if (paramCount == 0)
    return TKEY_NONE;

  switch (params[0]) {
  case 1:
  case 7:
    return tset_keymods(TKEY_HOME, mods);
  case 2:
    return tset_keymods(TKEY_INSERT, mods);
  case 3:
    return tset_keymods(TKEY_DELETE, mods);
  case 4:
  case 8:
    return tset_keymods(TKEY_END, mods);
  case 5:
    return tset_keymods(TKEY_PAGEUP, mods);
  case 6:
    return tset_keymods(TKEY_PAGEDOWN, mods);
  case 11:
    return tset_keymods(TKEY_F1, mods);
  case 12:
    return tset_keymods(TKEY_F2, mods);
  case 13:
    return tset_keymods(TKEY_F3, mods);
  case 14:
    return tset_keymods(TKEY_F4, mods);
  case 15:
    return tset_keymods(TKEY_F5, mods);
  case 17:
    return tset_keymods(TKEY_F6, mods);
  case 18:
    return tset_keymods(TKEY_F7, mods);
  case 19:
    return tset_keymods(TKEY_F8, mods);
  case 20:
    return tset_keymods(TKEY_F9, mods);
  case 21:
    return tset_keymods(TKEY_F10, mods);
  case 23:
    return tset_keymods(TKEY_F11, mods);
  case 24:
    return tset_keymods(TKEY_F12, mods);
  default:
    return TKEY_NONE;
  }
}

static int tdecode_ss3_sequence(char final, int mods) {
  switch (final) {
  case 'A':
    return tset_keymods(TKEY_UP, mods);
  case 'B':
    return tset_keymods(TKEY_DOWN, mods);
  case 'C':
    return tset_keymods(TKEY_RIGHT, mods);
  case 'D':
    return tset_keymods(TKEY_LEFT, mods);
  case 'F':
    return tset_keymods(TKEY_END, mods);
  case 'H':
    return tset_keymods(TKEY_HOME, mods);
  case 'P':
    return tset_keymods(TKEY_F1, mods);
  case 'Q':
    return tset_keymods(TKEY_F2, mods);
  case 'R':
    return tset_keymods(TKEY_F3, mods);
  case 'S':
    return tset_keymods(TKEY_F4, mods);
  default:
    return TKEY_NONE;
  }
}

static int tdecode_ss3_buffer(const char *sequence, int length) {
  int params[4] = {0};
  int paramCount = tparse_ansi_params(sequence, length, params, 4);
  int mods = paramCount >= 2 ? tkey_ansi_mods(params[paramCount - 1]) : 0;

  return tdecode_ss3_sequence(sequence[length - 1], mods);
}

static int tread_escape_sequence(bool blocking) {
  unsigned char introducer = 0;
  char sequence[32];
  unsigned char next = 0;
  int timeoutMs = blocking ? 25 : 0;

  if (!tread_input_byte(timeoutMs, &next))
    return TKEY_ESCAPE;

  if (next != '[' && next != 'O') {
    if (next == 0x1b)
      return tset_keymods(TKEY_ESCAPE, TKEY_MOD_ALT);

    return tset_keymods(tdecode_input_byte(next, blocking),
                        tkeymods() | TKEY_MOD_ALT);
  }

  introducer = next;

  for (int i = 0; i < (int)sizeof(sequence) - 1; ++i) {
    if (!tread_input_byte(timeoutMs, &next))
      return TKEY_ESCAPE;

    sequence[i] = (char)next;
    sequence[i + 1] = '\0';

    if (next >= '@' && next <= '~') {
      if (introducer == '[')
        return tdecode_csi_sequence(sequence, i + 1);

      return tdecode_ss3_buffer(sequence, i + 1);
    }
  }

  return TKEY_ESCAPE;
}

static int tdecode_input_byte(unsigned char byte, bool blocking) {
  if (byte == 0x1b)
    return tread_escape_sequence(blocking);

  if (byte == '\r' || byte == '\n')
    return tset_keymods(TKEY_ENTER, 0);

  if (byte == '\t')
    return tset_keymods(TKEY_TAB, 0);

  if (byte == 0x08 || byte == 0x7f)
    return tset_keymods(TKEY_BACKSPACE, 0);

  int ctrlBase = tkey_ctrl_base(byte);
  if (ctrlBase != TKEY_NONE)
    return tset_keymods(ctrlBase, TKEY_MOD_CTRL);

  if (byte < 0x80)
    return tset_keymods(byte, 0);

  return tset_keymods(tdecode_utf8(byte, blocking), 0);
}

static int tread_key(bool blocking) {
  unsigned char byte = 0;

  if (!tread_input_byte(blocking ? -1 : 0, &byte))
    return TKEY_NONE;

  return tdecode_input_byte(byte, blocking);
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

static void tappend_color_fg(tcolor_t color, tcolor_t defaultColor) {
  if (color == defaultColor) {
    tappend("\x1b[39m");
    return;
  }

  uint8_t r = (uint8_t)((color >> 24) & 0xff);
  uint8_t g = (uint8_t)((color >> 16) & 0xff);
  uint8_t b = (uint8_t)((color >> 8) & 0xff);
  tappendf("\x1b[38;2;%u;%u;%um", r, g, b);
}

static void tappend_color_bg(tcolor_t color, tcolor_t defaultColor) {
  if (color == defaultColor) {
    tappend("\x1b[49m");
    return;
  }

  uint8_t r = (uint8_t)((color >> 24) & 0xff);
  uint8_t g = (uint8_t)((color >> 16) & 0xff);
  uint8_t b = (uint8_t)((color >> 8) & 0xff);
  tappendf("\x1b[48;2;%u;%u;%um", r, g, b);
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
    gTerm.cells[i].fg = gTerm.defaultFg;
    gTerm.cells[i].bg = gTerm.defaultBg;

    gTerm.oldCells[i].ch = 0;
    gTerm.oldCells[i].fg = gTerm.defaultFg;
    gTerm.oldCells[i].bg = gTerm.defaultBg;
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
  inMode |= ENABLE_WINDOW_INPUT | ENABLE_EXTENDED_FLAGS |
            ENABLE_VIRTUAL_TERMINAL_INPUT;
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
  gTermResizePending = 0;
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
  tcolor_t fg;
  tcolor_t bg;
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

  tcolor_t defaultFg;
  tcolor_t defaultBg;

  int width;
  int height;

  int lastKeyMods;

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

static int tset_keymods(int key, int mods) {
  gTerm.lastKeyMods = (key == TKEY_NONE) ? 0 : (mods & TKEY_MOD_MASK);
  return key;
}

static tcolor_t tfallback_defaultfg(void) { return TRGBA(255, 255, 255, 255); }

static tcolor_t tfallback_defaultbg(void) { return TRGBA(0, 0, 0, 255); }

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

static void tappend_color_fg(tcolor_t color, tcolor_t defaultColor) {
  if (color == defaultColor) {
    tappend("\x1b[39m");
    return;
  }

  uint8_t r = (uint8_t)((color >> 24) & 0xff);
  uint8_t g = (uint8_t)((color >> 16) & 0xff);
  uint8_t b = (uint8_t)((color >> 8) & 0xff);
  tappendf("\x1b[38;2;%u;%u;%um", r, g, b);
}

static void tappend_color_bg(tcolor_t color, tcolor_t defaultColor) {
  if (color == defaultColor) {
    tappend("\x1b[49m");
    return;
  }

  uint8_t r = (uint8_t)((color >> 24) & 0xff);
  uint8_t g = (uint8_t)((color >> 16) & 0xff);
  uint8_t b = (uint8_t)((color >> 8) & 0xff);
  tappendf("\x1b[48;2;%u;%u;%um", r, g, b);
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
    gTerm.cells[i].fg = gTerm.defaultFg;
    gTerm.cells[i].bg = gTerm.defaultBg;

    gTerm.oldCells[i].ch = 0;
    gTerm.oldCells[i].fg = gTerm.defaultFg;
    gTerm.oldCells[i].bg = gTerm.defaultBg;
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

static int tdecode_input_byte(unsigned char byte, bool blocking);

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

static int tparse_ansi_params(const char *sequence, int length, int *params,
                              int maxParams) {
  int count = 0;
  int value = 0;
  bool hasValue = false;

  for (int i = 0; i < length - 1; ++i) {
    char ch = sequence[i];

    if (ch >= '0' && ch <= '9') {
      value = value * 10 + (ch - '0');
      hasValue = true;
      continue;
    }

    if (ch == ';') {
      if (count < maxParams)
        params[count++] = hasValue ? value : 0;
      value = 0;
      hasValue = false;
      continue;
    }

    return count;
  }

  if ((hasValue || count > 0) && count < maxParams)
    params[count++] = hasValue ? value : 0;

  return count;
}

static int tdecode_csi_sequence(const char *sequence, int length) {
  int params[4] = {0};
  int paramCount = tparse_ansi_params(sequence, length, params, 4);
  int mods = paramCount >= 2 ? tkey_ansi_mods(params[paramCount - 1]) : 0;
  char final = sequence[length - 1];

  switch (final) {
  case 'A':
    return tset_keymods(TKEY_UP, mods);
  case 'B':
    return tset_keymods(TKEY_DOWN, mods);
  case 'C':
    return tset_keymods(TKEY_RIGHT, mods);
  case 'D':
    return tset_keymods(TKEY_LEFT, mods);
  case 'F':
    return tset_keymods(TKEY_END, mods);
  case 'H':
    return tset_keymods(TKEY_HOME, mods);
  case 'P':
    return tset_keymods(TKEY_F1, mods);
  case 'Q':
    return tset_keymods(TKEY_F2, mods);
  case 'R':
    return tset_keymods(TKEY_F3, mods);
  case 'S':
    return tset_keymods(TKEY_F4, mods);
  case 'Z':
    return tset_keymods(TKEY_TAB, TKEY_MOD_SHIFT | mods);
  case '~':
    break;
  default:
    return TKEY_NONE;
  }

  if (paramCount == 0)
    return TKEY_NONE;

  switch (params[0]) {
  case 1:
  case 7:
    return tset_keymods(TKEY_HOME, mods);
  case 2:
    return tset_keymods(TKEY_INSERT, mods);
  case 3:
    return tset_keymods(TKEY_DELETE, mods);
  case 4:
  case 8:
    return tset_keymods(TKEY_END, mods);
  case 5:
    return tset_keymods(TKEY_PAGEUP, mods);
  case 6:
    return tset_keymods(TKEY_PAGEDOWN, mods);
  case 11:
    return tset_keymods(TKEY_F1, mods);
  case 12:
    return tset_keymods(TKEY_F2, mods);
  case 13:
    return tset_keymods(TKEY_F3, mods);
  case 14:
    return tset_keymods(TKEY_F4, mods);
  case 15:
    return tset_keymods(TKEY_F5, mods);
  case 17:
    return tset_keymods(TKEY_F6, mods);
  case 18:
    return tset_keymods(TKEY_F7, mods);
  case 19:
    return tset_keymods(TKEY_F8, mods);
  case 20:
    return tset_keymods(TKEY_F9, mods);
  case 21:
    return tset_keymods(TKEY_F10, mods);
  case 23:
    return tset_keymods(TKEY_F11, mods);
  case 24:
    return tset_keymods(TKEY_F12, mods);
  default:
    return TKEY_NONE;
  }
}

static int tdecode_ss3_sequence(char final, int mods) {
  switch (final) {
  case 'A':
    return tset_keymods(TKEY_UP, mods);
  case 'B':
    return tset_keymods(TKEY_DOWN, mods);
  case 'C':
    return tset_keymods(TKEY_RIGHT, mods);
  case 'D':
    return tset_keymods(TKEY_LEFT, mods);
  case 'F':
    return tset_keymods(TKEY_END, mods);
  case 'H':
    return tset_keymods(TKEY_HOME, mods);
  case 'P':
    return tset_keymods(TKEY_F1, mods);
  case 'Q':
    return tset_keymods(TKEY_F2, mods);
  case 'R':
    return tset_keymods(TKEY_F3, mods);
  case 'S':
    return tset_keymods(TKEY_F4, mods);
  default:
    return TKEY_NONE;
  }
}

static int tdecode_ss3_buffer(const char *sequence, int length) {
  int params[4] = {0};
  int paramCount = tparse_ansi_params(sequence, length, params, 4);
  int mods = paramCount >= 2 ? tkey_ansi_mods(params[paramCount - 1]) : 0;

  return tdecode_ss3_sequence(sequence[length - 1], mods);
}

static int tread_escape_sequence(bool blocking) {
  unsigned char introducer = 0;
  char sequence[32];
  unsigned char next = 0;
  int timeoutMs = blocking ? 25 : 0;

  if (!tread_input_byte(timeoutMs, &next))
    return TKEY_ESCAPE;

  if (next != '[' && next != 'O') {
    if (next == 0x1b)
      return tset_keymods(TKEY_ESCAPE, TKEY_MOD_ALT);

    return tset_keymods(tdecode_input_byte(next, blocking),
                        tkeymods() | TKEY_MOD_ALT);
  }

  introducer = next;

  for (int i = 0; i < (int)sizeof(sequence) - 1; ++i) {
    if (!tread_input_byte(timeoutMs, &next))
      return TKEY_ESCAPE;

    sequence[i] = (char)next;
    if (next >= 0x40 && next <= 0x7e) {
      sequence[i + 1] = '\0';

      if (introducer == '[')
        return tdecode_csi_sequence(sequence, i + 1);

      return tdecode_ss3_buffer(sequence, i + 1);
    }
  }

  return TKEY_ESCAPE;
}

static int tdecode_input_byte(unsigned char byte, bool blocking) {
  if (byte == 0x1b)
    return tread_escape_sequence(blocking);

  if (byte == '\r' || byte == '\n')
    return tset_keymods(TKEY_ENTER, 0);

  if (byte == '\t')
    return tset_keymods(TKEY_TAB, 0);

  if (byte == 0x08 || byte == 0x7f)
    return tset_keymods(TKEY_BACKSPACE, 0);

  int ctrlBase = tkey_ctrl_base(byte);
  if (ctrlBase != TKEY_NONE)
    return tset_keymods(ctrlBase, TKEY_MOD_CTRL);

  if (byte < 0x80)
    return tset_keymods(byte, 0);

  return tset_keymods(tdecode_utf8(byte, blocking), 0);
}

static int tread_key(bool blocking) {
  unsigned char byte = 0;

  if (!tread_input_byte(blocking ? -1 : 0, &byte))
    return TKEY_NONE;

  return tdecode_input_byte(byte, blocking);
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
  gTerm.defaultFg = tfallback_defaultfg();
  gTerm.defaultBg = tfallback_defaultbg();

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

#endif

int twidth(void) { return gTerm.width; }

int theight(void) { return gTerm.height; }

tcolor_t tdefaultfg(void) { return gTerm.defaultFg; }

tcolor_t tdefaultbg(void) { return gTerm.defaultBg; }

int tkeymods(void) { return gTerm.lastKeyMods; }

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
    gTerm.cells[i].fg = gTerm.defaultFg;
    gTerm.cells[i].bg = gTerm.defaultBg;
  }
}

void tput(int x, int y, wchar_t ch, tcolor_t fg, tcolor_t bg) {
  if (x < 0 || y < 0 || x >= gTerm.width || y >= gTerm.height)
    return;

  int index = y * gTerm.width + x;

  gTerm.cells[index].ch = ch;
  gTerm.cells[index].fg = fg;
  gTerm.cells[index].bg = bg;
}

void twrite(int x, int y, const wchar_t *text, tcolor_t fg, tcolor_t bg) {
  if (!text)
    return;

  for (int i = 0; text[i]; ++i)
    tput(x + i, y, text[i], fg, bg);
}

void trender(void) {
  gTerm.outBufferCount = 0;

  tcolor_t currentFg = gTerm.defaultFg;
  tcolor_t currentBg = gTerm.defaultBg;
  bool haveCurrentColors = false;

  for (int y = 0; y < gTerm.height; ++y) {
    for (int x = 0; x < gTerm.width; ++x) {
      int index = y * gTerm.width + x;

      tcell_t *cell = &gTerm.cells[index];
      tcell_t *old = &gTerm.oldCells[index];

      if (cell->ch == old->ch && cell->fg == old->fg && cell->bg == old->bg)
        continue;

      tappendf("\x1b[%d;%dH", y + 1, x + 1);

      if (!haveCurrentColors || cell->fg != currentFg) {
        tappend_color_fg(cell->fg, gTerm.defaultFg);
        currentFg = cell->fg;
      }

      if (!haveCurrentColors || cell->bg != currentBg) {
        tappend_color_bg(cell->bg, gTerm.defaultBg);
        currentBg = cell->bg;
      }

      haveCurrentColors = true;
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

#ifdef __cplusplus
}
#endif
