# termc

Single-header C library for simple terminal UIs with colored cell rendering and keyboard input.

![Example terminal UI](term_example.png)

## Features

- Single-header distribution in `term.h`
- Colored cell-based rendering with diffed screen updates
- Keyboard input helpers for arrows, enter, tab, backspace, and escape
- Terminal resize event support via `TKEY_RESIZE`
- Works on Win32 consoles and Unix-like terminals

## Files

- `term.h`: library interface and implementation
- `term_example.c`: small demo program
- `build.bat`: builds the Windows example with `gcc`
- `build_wsl.bat`: builds the example inside WSL (testing unix backend)

## Quick Start

In exactly one `.c` file:

```c
#define TIMPLEMENTATION
#include "term.h"
```

In other translation units:

```c
#include "term.h"
```

## Notes

- Text APIs use `wchar_t` and wide string literals like `L"text"`.
- Colors are packed with `TRGB(r, g, b)`.
- The terminal backend expects a real console/TTY, not redirected stdin/stdout.

## License

MIT. See `LICENSE`.
