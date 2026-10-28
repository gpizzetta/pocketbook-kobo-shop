#include <inkview.h>

static ifont *font;

static int main_handler(int type, int par1, int par2) {
  (void)par1;
  (void)par2;

  switch (type) {
  case EVT_INIT:
    font = OpenFont("LiberationSans", 32, 0);
    if (!font) {
      font = OpenFont("DejaVu Sans", 32, 0);
    }
    SetPanelType(0);
    SetOrientation(0);
    break;

  case EVT_SHOW:
    ClearScreen();
    SetFont(font, BLACK);
    DrawString(40, 80, "Hello PocketBook x Kobo");
    DrawString(40, 140, "SDK 6.10 / B288 / Era");
    DrawString(40, 200, "u700.6.10.3209");
    FullUpdate();
    break;

  case EVT_KEYPRESS:
    if (par1 == IV_KEY_BACK || par1 == IV_KEY_HOME) {
      CloseApp();
    }
    break;

  case EVT_EXIT:
    if (font) {
      CloseFont(font);
      font = NULL;
    }
    break;
  }
  return 0;
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  InkViewMain(main_handler);
  return 0;
}
