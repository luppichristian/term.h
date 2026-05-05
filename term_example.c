#define TIMPLEMENTATION
#include "term.h"

int main(void) {
  // Initialize the terminal backend before using any rendering or input APIs.
  if (!tinit())
    return 1;

  // Track the position of the highlighted cell.
  int x = 10;
  int y = 5;
  bool running = true;

  while (running) {
    // Redraw the full frame into the back buffer each iteration.
    tclear();
    tcolor_t defaultFg = tdefaultfg();
    tcolor_t defaultBg = tdefaultbg();
    twrite(0, 0, L"Diff-rendered terminal UI", TRGBA(120, 220, 120, 255),
           defaultBg);
    twrite(0, 1, L"Arrow keys move. Q or Escape quits.", defaultFg, defaultBg);
    // Draw a colored block by using a space character with a red background.
    tput(x, y, L' ', TRGBA(255, 255, 255, 255), TRGBA(255, 0, 0, 255));
    trender();

    // Wait for input, then update the block position or exit.
    int key = twait();
    switch (key) {
    case TKEY_ESCAPE:
      running = false;
      break;

    case TKEY_LEFT:
      x--;
      break;

    case TKEY_RIGHT:
      x++;
      break;

    case TKEY_UP:
      y--;
      break;

    case TKEY_DOWN:
      y++;
      break;

    default:
      if (key == 'q' || key == 'Q')
        running = false;
      break;
    }
  }

  // Restore the console before exiting.
  tquit();
  return 0;
}
