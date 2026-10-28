#include <inkview.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include "adept.h"
#include "config.h"
#include "http.h"
#include "kobo.h"

typedef enum {
  SCR_SETUP_MENU = 0,
  SCR_KOBO_CODE,
  SCR_BB_FORM,
  SCR_LIBRARY,
  SCR_BOOK
} Screen;

typedef enum {
  SORT_NAME = 0,
  SORT_ADDED,
  SORT_OPENED
} SortMode;

typedef struct {
  int x, y, w, h;
  const char *label;
  int enabled; /* 0 = grisé */
  int filled;  /* 1 = fond noir, texte blanc */
} UiBtn;

static ifont *font_title;
static ifont *font_body;
static ifont *font_small;

static AppConfig g_cfg;
static BookList g_books;
static KoboActivation g_act;
static Screen g_screen = SCR_SETUP_MENU;
static int g_list_top;
static int g_list_sel; /* index dans g_view */
static int g_book_sel; /* index dans g_books.items */
static char g_kbd_buf[KOBO_SHOP_STR];
static int g_polling;
static int g_poll_busy;
static int g_kobo_wait_ok; /* activation réussie, attendre OK utilisateur */

static int g_view[KOBO_SHOP_MAX_BOOKS];
static int g_view_count;
static char g_search[KOBO_SHOP_STR];
static SortMode g_sort = SORT_NAME;
static int g_search_pending; /* 1 = clavier recherche */

static UiBtn g_setup_btns[4];
static UiBtn g_kobo_cancel;
static UiBtn g_kobo_ok;
static UiBtn g_bb_email_btn;
static UiBtn g_bb_pass_btn;
static UiBtn g_bb_ok;
static UiBtn g_bb_cancel;
static UiBtn g_book_btns[3];
static UiBtn g_menu_btn;
static UiBtn g_search_btn;
static UiBtn g_sort_btn;
static int g_bb_editing; /* 0=email 1=pass */

/* Scroll liste : drag vertical ; tap = Téléch./Ouvrir */
static int g_ptr_down;
static int g_ptr_x0, g_ptr_y0;
static int g_ptr_view;
static int g_ptr_scrolling;
static int g_list_top_at_down;

static int btn_h(void) { return 88; }
static int btn_gap(void) { return 18; }
static int btn_pad_x(void) { return 24; }
static int cover_w(void) { return 96; }
static int cover_h(void) { return 144; }
static int row_h(void) { return 160; }
/* Liste juste sous recherche/tri (voir draw_library) */
static int list_top_y(void) { return 168; }
static int footer_y(void) {
  int ph = PanelHeight();
  if (ph < 0) {
    ph = 0;
  }
  return ScreenHeight() - ph - btn_h() - 16;
}
/* Marge bas = panneau système (évite wrap e-ink en haut d'écran) */
static int list_bottom_y(void) {
  int ph = PanelHeight();
  if (ph < 0) {
    ph = 0;
  }
  /* Marge un peu plus large : dernier livre ne doit pas toucher le panel */
  return ScreenHeight() - ph - 16;
}
static int menu_btn_size(void) { return 56; }
static int row_action_w(void) { return 168; }
static int scroll_threshold(void) { return 28; }

static int visible_rows(void) {
  int h = list_bottom_y() - list_top_y();
  int rh = row_h();
  /* Uniquement des lignes entièrement visibles */
  int n = (h >= rh) ? (h / rh) : 0;
  return n > 1 ? n : 1;
}

static void clamp_list_top(void) {
  int vis = visible_rows();
  int max_top = g_view_count - vis;
  if (max_top < 0) {
    max_top = 0;
  }
  if (g_list_top < 0) {
    g_list_top = 0;
  }
  if (g_list_top > max_top) {
    g_list_top = max_top;
  }
}

static int hit_btn(const UiBtn *b, int x, int y) {
  return b->enabled && x >= b->x && x < b->x + b->w && y >= b->y &&
         y < b->y + b->h;
}

static void draw_btn(const UiBtn *b) {
  int bg = WHITE;
  int fg = BLACK;
  if (!b->enabled) {
    bg = LGRAY;
    fg = DGRAY;
  } else if (b->filled) {
    bg = BLACK;
    fg = WHITE;
  }
  FillArea(b->x, b->y, b->w, b->h, bg);
  DrawRect(b->x, b->y, b->w, b->h, BLACK);
  if (b->enabled && !b->filled) {
    DrawRect(b->x + 2, b->y + 2, b->w - 4, b->h - 4, BLACK);
  }
  SetFont(font_body, fg);
  int tw = StringWidth((char *)b->label);
  int tx = b->x + (b->w - tw) / 2;
  if (tx < b->x + 8) {
    tx = b->x + 8;
  }
  int ty = b->y + (b->h - font_body->height) / 2;
  DrawString(tx, ty, b->label);
}

static void make_btn(UiBtn *b, int x, int y, int w, int h, const char *label,
                     int enabled, int filled) {
  b->x = x;
  b->y = y;
  b->w = w;
  b->h = h;
  b->label = label;
  b->enabled = enabled;
  b->filled = filled;
}

static void make_full_btn(UiBtn *b, int y, const char *label) {
  make_btn(b, btn_pad_x(), y, ScreenWidth() - 2 * btn_pad_x(), btn_h(), label, 1,
           0);
}

static void draw_header(const char *title) {
  FillArea(0, 0, ScreenWidth(), 78, WHITE);
  SetFont(font_title, BLACK);
  DrawString(20, 20, title);
  DrawLine(0, 76, ScreenWidth(), 76, BLACK);
}

static void draw_hamburger(int x, int y, int s) {
  int t = 5;
  int gap = (s - 3 * t) / 4;
  int w = s - 16;
  int lx = x + 8;
  for (int i = 0; i < 3; i++) {
    int ly = y + gap + i * (t + gap);
    FillArea(lx, ly, w, t, BLACK);
  }
  DrawRect(x, y, s, s, BLACK);
}

static void redraw(void);
static void sync_library(void);
static void on_timer_poll(void);
static void rebuild_view(void);
static void open_library_screen(int do_sync);
static void lib_menu_handler(int index);
static void search_kbd_done(char *s);
static void do_download(void);
static void do_open(void);
static void row_quick_action(int view_i);

static int view_book_index(int view_i) {
  if (view_i < 0 || view_i >= g_view_count) {
    return -1;
  }
  return g_view[view_i];
}

static int strcasestr_ascii(const char *hay, const char *needle) {
  if (!needle || !needle[0]) {
    return 1;
  }
  size_t n = strlen(needle);
  for (const char *p = hay; *p; p++) {
    size_t i = 0;
    while (i < n) {
      char a = p[i];
      char b = needle[i];
      if (!a) {
        return 0;
      }
      if (tolower((unsigned char)a) != tolower((unsigned char)b)) {
        break;
      }
      i++;
    }
    if (i == n) {
      return 1;
    }
  }
  return 0;
}

static int cmp_books(int ia, int ib) {
  const BookEntry *a = &g_books.items[ia];
  const BookEntry *b = &g_books.items[ib];
  int c = 0;
  if (g_sort == SORT_ADDED) {
    c = -strcmp(a->date_added[0] ? a->date_added : "",
                b->date_added[0] ? b->date_added : "");
  } else if (g_sort == SORT_OPENED) {
    c = -strcmp(a->last_opened[0] ? a->last_opened : "",
                b->last_opened[0] ? b->last_opened : "");
  }
  if (c == 0) {
    c = strcasecmp(a->title, b->title);
  }
  return c;
}

static void rebuild_view(void) {
  g_view_count = 0;
  for (int i = 0; i < g_books.count; i++) {
    if (g_books.items[i].archived) {
      continue;
    }
    if (g_search[0] &&
        !strcasestr_ascii(g_books.items[i].title, g_search) &&
        !strcasestr_ascii(g_books.items[i].author, g_search)) {
      continue;
    }
    g_view[g_view_count++] = i;
  }
  /* tri insertion simple (N ~ 200) */
  for (int i = 1; i < g_view_count; i++) {
    int v = g_view[i];
    int j = i;
    while (j > 0 && cmp_books(g_view[j - 1], v) > 0) {
      g_view[j] = g_view[j - 1];
      j--;
    }
    g_view[j] = v;
  }
  if (g_list_sel >= g_view_count) {
    g_list_sel = g_view_count > 0 ? g_view_count - 1 : 0;
  }
  if (g_list_top > g_list_sel) {
    g_list_top = g_list_sel;
  }
  clamp_list_top();
}

static const char *sort_label(void) {
  switch (g_sort) {
  case SORT_ADDED:
    return "Ajout";
  case SORT_OPENED:
    return "Ouvert";
  default:
    return "Nom";
  }
}

static void stamp_now_iso(char *dst, size_t dst_len) {
  time_t t = time(NULL);
  struct tm tm;
  gmtime_r(&t, &tm);
  strftime(dst, dst_len, "%Y-%m-%dT%H:%M:%S", &tm);
}

/* allow_dl : télécharge la cover si absente du cache. */
static void draw_cover_at(const BookEntry *b, int x, int y, int w, int h,
                          int allow_dl) {
  DrawRect(x, y, w, h, BLACK);
  FillArea(x + 1, y + 1, w - 2, h - 2, WHITE);
  if (!b->cover_id[0]) {
    return;
  }
  char path[KOBO_SHOP_PATH];
  int dw = w * 2;
  int dh = h * 2;
  /* suffixe _c : invalide l'ancien cache forcé en niveaux de gris */
  snprintf(path, sizeof(path), "%s/%s_%dx%d_c.jpg", app_covers_dir(),
           b->cover_id, dw, dh);
  if (access(path, R_OK) != 0) {
    if (!allow_dl || kobo_ensure_cover(&g_cfg, b, dw, dh, path, sizeof(path)) != 0) {
      return;
    }
  }
  /* LoadJPEG scale ; pas de forçage « gris » côté app */
  ibitmap *bm = LoadJPEG(path, w - 2, h - 2, 100, 100, 1);
  if (!bm) {
    bm = LoadBitmap(path);
  }
  if (bm) {
    StretchBitmap(x + 1, y + 1, w - 2, h - 2, bm, 0);
    free(bm);
  }
}

/* Dialogue bloquant avec bouton OK (pas de disparition auto). */
static void alert_ok(int icon, const char *title, const char *text) {
  DialogSynchro(icon, title, text, "OK", NULL, NULL);
}

static void cancel_kobo_activation(void) {
  g_polling = 0;
  g_poll_busy = 0;
  g_kobo_wait_ok = 0;
  ClearTimerByName("kobo_poll");
  g_screen = SCR_SETUP_MENU;
  redraw();
}

static void confirm_kobo_activation(void) {
  if (!g_kobo_wait_ok) {
    return;
  }
  g_polling = 0;
  g_poll_busy = 0;
  g_kobo_wait_ok = 0;
  ClearTimerByName("kobo_poll");
  config_save(&g_cfg);
  alert_ok(ICON_INFORMATION, "Kobo", "Compte Kobo enregistré.");
  open_library_screen(1);
}

static void on_timer_poll(void) {
  if (!g_polling || g_screen != SCR_KOBO_CODE || g_kobo_wait_ok) {
    return;
  }
  if (g_poll_busy) {
    SetWeakTimer("kobo_poll", on_timer_poll, 500);
    return;
  }
  g_poll_busy = 1;
  int rc = kobo_activation_poll(&g_cfg, &g_act);
  g_poll_busy = 0;

  if (!g_polling || g_screen != SCR_KOBO_CODE) {
    return;
  }
  if (rc < 0) {
    g_polling = 0;
    alert_ok(ICON_ERROR, "Kobo", g_act.err);
    g_screen = SCR_SETUP_MENU;
    redraw();
    return;
  }
  if (rc == 1) {
    /* Succès : rester sur l'écran, activer OK — pas de ByteBooks auto */
    g_polling = 0;
    g_kobo_wait_ok = 1;
    ClearTimerByName("kobo_poll");
    config_save(&g_cfg);
    redraw();
    return;
  }
  SetWeakTimer("kobo_poll", on_timer_poll, 3000);
}

static void start_kobo_activation(void) {
  if (config_kobo_ready(&g_cfg)) {
    int choice = DialogSynchro(
        ICON_QUESTION, "Kobo",
        "Un compte Kobo est déjà enregistré.\nReconnecter ?", "Oui", "Non", NULL);
    if (choice != 1) {
      return;
    }
  }
  g_kobo_wait_ok = 0;
  ShowHourglassForce();
  NetConnect3(1);
  if (kobo_activation_start(&g_cfg, &g_act) != 0) {
    HideHourglass();
    alert_ok(ICON_ERROR, "Kobo", g_act.err);
    g_screen = SCR_SETUP_MENU;
    redraw();
    return;
  }
  HideHourglass();
  g_screen = SCR_KOBO_CODE;
  g_polling = 1;
  g_poll_busy = 0;
  SetWeakTimer("kobo_poll", on_timer_poll, 3000);
  redraw();
}

static void sync_library(void) {
  ShowHourglassForce();
  NetConnect3(1);
  char err[256];
  if (kobo_initialization(&g_cfg, err, sizeof(err)) != 0) {
    HideHourglass();
    alert_ok(ICON_ERROR, "Init", err);
    return;
  }
  if (kobo_library_sync(&g_cfg, &g_books, err, sizeof(err)) != 0) {
    HideHourglass();
    alert_ok(ICON_ERROR, "Sync", err);
    return;
  }
  HideHourglass();
  g_list_top = 0;
  g_list_sel = 0;
  rebuild_view();
  g_screen = SCR_LIBRARY;
  redraw();
}

static void open_library_screen(int do_sync) {
  if (!config_kobo_ready(&g_cfg)) {
    alert_ok(ICON_WARNING, "Kobo", "Connecte Kobo d'abord.");
    g_screen = SCR_SETUP_MENU;
    redraw();
    return;
  }
  if (do_sync || g_books.count == 0) {
    sync_library();
    return;
  }
  rebuild_view();
  g_screen = SCR_LIBRARY;
  redraw();
}

static void search_kbd_done(char *s) {
  g_search_pending = 0;
  if (s) {
    snprintf(g_search, sizeof(g_search), "%s", s);
  }
  g_list_top = 0;
  g_list_sel = 0;
  rebuild_view();
  g_screen = SCR_LIBRARY;
  redraw();
}

static void start_search(void) {
  g_search_pending = 1;
  snprintf(g_kbd_buf, sizeof(g_kbd_buf), "%s", g_search);
  OpenKeyboard("Rechercher (titre / auteur)", g_kbd_buf,
               (int)sizeof(g_kbd_buf) - 1, 0, search_kbd_done);
}

static void import_acsm_from_books(void) {
  if (!g_cfg.adept_activated && !config_bytebooks_ready(&g_cfg)) {
    alert_ok(ICON_WARNING, "Importer ACSM",
             "Configure ByteBooks d'abord (menu Configuration).");
    return;
  }
  ShowHourglassForce();
  NetConnect3(1);
  int ok = 0, fail = 0;
  char err[256];
  if (adept_import_books_folder(&g_books, &ok, &fail, err, sizeof(err)) != 0) {
    HideHourglass();
    alert_ok(ICON_WARNING, "Importer ACSM", err);
    return;
  }
  library_cache_save(&g_books);
  rebuild_view();
  HideHourglass();
  char msg[220];
  snprintf(msg, sizeof(msg),
           "Dépôt : %s\n"
           "OK : %d   Échecs : %d\n"
           "EPUB → %s",
           app_acsm_drop_dir(), ok, fail, app_books_dir());
  alert_ok(ICON_INFORMATION, "Importer ACSM", msg);
  redraw();
}

static void lib_menu_handler(int index) {
  if (index <= 0) {
    return;
  }
  switch (index) {
  case 1: /* Configuration */
    g_screen = SCR_SETUP_MENU;
    redraw();
    break;
  case 2: /* Sync */
    sync_library();
    break;
  case 3: /* Search */
    start_search();
    break;
  case 4:
    g_sort = SORT_NAME;
    rebuild_view();
    redraw();
    break;
  case 5:
    g_sort = SORT_ADDED;
    rebuild_view();
    redraw();
    break;
  case 6:
    g_sort = SORT_OPENED;
    rebuild_view();
    redraw();
    break;
  case 7: /* clear search */
    g_search[0] = 0;
    rebuild_view();
    redraw();
    break;
  case 8: /* import ACSM */
    import_acsm_from_books();
    break;
  default:
    break;
  }
}

static void open_lib_menu(void) {
  static imenu menu[14];
  int n = 0;
  menu[n++] = (imenu){ITEM_HEADER, 0, "Menu", NULL};
  menu[n++] = (imenu){ITEM_ACTIVE, 1, "Configuration", NULL};
  menu[n++] = (imenu){ITEM_ACTIVE, 2, "Synchroniser", NULL};
  menu[n++] = (imenu){ITEM_ACTIVE, 3, "Rechercher", NULL};
  if (g_search[0]) {
    menu[n++] = (imenu){ITEM_ACTIVE, 7, "Effacer la recherche", NULL};
  }
  menu[n++] = (imenu){ITEM_ACTIVE, 8, "Importer ACSM (kobo_shop_data/acsm)", NULL};
  menu[n++] = (imenu){ITEM_SEPARATOR, 0, NULL, NULL};
  menu[n++] = (imenu){ITEM_ACTIVE, 4, "Tri : nom", NULL};
  menu[n++] = (imenu){ITEM_ACTIVE, 5, "Tri : date d'ajout", NULL};
  menu[n++] = (imenu){ITEM_ACTIVE, 6, "Tri : dernier ouvert", NULL};
  menu[n++] = (imenu){0, 0, NULL, NULL};
  OpenMenu(menu, 0, ScreenWidth() - menu_btn_size() - 8, 78, lib_menu_handler);
}
static void bb_kbd_done(char *s);

static void open_bb_keyboard(int which) {
  g_bb_editing = which;
  g_kbd_buf[0] = 0;
  if (which == 0) {
    if (g_cfg.bytebooks_email[0]) {
      snprintf(g_kbd_buf, sizeof(g_kbd_buf), "%s", g_cfg.bytebooks_email);
    }
    OpenKeyboard("E-mail ByteBooks", g_kbd_buf, (int)sizeof(g_kbd_buf) - 1, 0,
                 bb_kbd_done);
  } else {
    OpenKeyboard("Mot de passe ByteBooks", g_kbd_buf,
                 (int)sizeof(g_kbd_buf) - 1, KBD_PASSWORD, bb_kbd_done);
  }
}

static void bb_kbd_done(char *s) {
  if (s && s[0]) {
    if (g_bb_editing == 0) {
      snprintf(g_cfg.bytebooks_email, sizeof(g_cfg.bytebooks_email), "%s", s);
    } else {
      snprintf(g_cfg.bytebooks_password, sizeof(g_cfg.bytebooks_password), "%s",
               s);
    }
  }
  g_screen = SCR_BB_FORM;
  redraw();
}

static void submit_bytebooks(void) {
  if (!config_bytebooks_ready(&g_cfg)) {
    alert_ok(ICON_WARNING, "ByteBooks",
             "Remplissez l'e-mail et le mot de passe.");
    return;
  }
  config_save(&g_cfg);
  ShowHourglassForce();
  NetConnect3(1);
  char err[256];
  if (adept_activate(&g_cfg, err, sizeof(err)) != 0) {
    HideHourglass();
    alert_ok(ICON_WARNING, "ADEPT", err);
  } else {
    HideHourglass();
    alert_ok(ICON_INFORMATION, "ADEPT", "Activation ByteBooks OK.");
  }
  g_screen = SCR_SETUP_MENU;
  redraw();
}

static void show_bytebooks_form(void) {
  g_screen = SCR_BB_FORM;
  redraw();
}

static const char *drm_label(const BookEntry *b) {
  switch (b->drm) {
  case BOOK_DRM_SIGNED_NO_DRM:
    return b->downloaded ? "[OK free]" : "[free ✓]";
  case BOOK_DRM_ADOBE:
    return b->downloaded ? "[OK ACSM]" : "[ACSM ✓]";
  case BOOK_DRM_KDRM:
    return "[sans ACSM]";
  case BOOK_DRM_NONE_SAMPLE:
    return "[échantillon]";
  default:
    return b->downloaded ? "[OK]" : "[…]";
  }
}

/* Ancienne fiche livre retirée : l'action se fait depuis la liste. */

static void layout_setup_btns(void) {
  int y = 210;
  make_full_btn(&g_setup_btns[0], y,
                config_kobo_ready(&g_cfg) ? "Kobo (reconnecter)" : "Connecter Kobo");
  y += btn_h() + btn_gap();
  make_full_btn(&g_setup_btns[1], y, "Compte ByteBooks");
  y += btn_h() + btn_gap();
  make_full_btn(&g_setup_btns[2], y, "Bibliothèque");
  y += btn_h() + btn_gap();
  make_full_btn(&g_setup_btns[3], y, "Quitter");
}

static void layout_kobo_footer(void) {
  int y = footer_y();
  int gap = 16;
  int w = (ScreenWidth() - 2 * btn_pad_x() - gap) / 2;
  make_btn(&g_kobo_cancel, btn_pad_x(), y, w, btn_h(), "Annuler", 1, 0);
  make_btn(&g_kobo_ok, btn_pad_x() + w + gap, y, w, btn_h(), "OK",
           g_kobo_wait_ok ? 1 : 0, 1);
}

static void layout_bb_form(void) {
  int y = 200;
  make_full_btn(&g_bb_email_btn, y, "E-mail (toucher)");
  y += btn_h() + btn_gap();
  make_full_btn(&g_bb_pass_btn, y, "Mot de passe (toucher)");
  int fy = footer_y();
  int gap = 16;
  int w = (ScreenWidth() - 2 * btn_pad_x() - gap) / 2;
  make_btn(&g_bb_cancel, btn_pad_x(), fy, w, btn_h(), "Annuler", 1, 0);
  make_btn(&g_bb_ok, btn_pad_x() + w + gap, fy, w, btn_h(), "Valider",
           config_bytebooks_ready(&g_cfg) ? 1 : 0, 1);
}

static void draw_setup(void) {
  draw_header("Kobo Shop");
  SetFont(font_body, BLACK);
  char line[280];
  snprintf(line, sizeof(line), "Kobo: %s",
           config_kobo_ready(&g_cfg) ? "connecté (mémorisé)" : "non connecté");
  DrawString(30, 95, line);
  snprintf(line, sizeof(line), "ByteBooks: %s%s",
           config_bytebooks_ready(&g_cfg) ? g_cfg.bytebooks_email : "—",
           g_cfg.adept_activated ? " · ADEPT" : "");
  DrawString(30, 140, line);

  layout_setup_btns();
  for (int i = 0; i < 4; i++) {
    draw_btn(&g_setup_btns[i]);
  }
  SetFont(font_small, BLACK);
  DrawString(30, ScreenHeight() - 36,
             adept_tools_available() ? "ADEPT: OK" : "ADEPT ARM: absent (free OK)");
}

static void draw_kobo_code(void) {
  draw_header("Activation Kobo");
  SetFont(font_body, BLACK);
  DrawTextRect(24, 95, ScreenWidth() - 48, 90,
               "Sur votre navigateur, consultez l'adresse :",
               ALIGN_LEFT | VALIGN_TOP);
  SetFont(font_title, BLACK);
  DrawString(24, 185, "www.kobo.com/activate");
  SetFont(font_body, BLACK);
  DrawString(24, 250, "et entrez ce code :");

  SetFont(font_title, BLACK);
  char code[64];
  snprintf(code, sizeof(code), "%s", g_act.code);
  int tw = StringWidth(code);
  DrawString((ScreenWidth() - tw) / 2, 310, code);

  SetFont(font_body, BLACK);
  if (g_kobo_wait_ok) {
    DrawString(24, 390, "Activation réussie — appuyez sur OK");
  } else {
    DrawString(24, 390, "En attente de confirmation…");
  }

  layout_kobo_footer();
  draw_btn(&g_kobo_cancel);
  draw_btn(&g_kobo_ok);
}

static void draw_bb_form(void) {
  draw_header("Compte ByteBooks");
  SetFont(font_body, BLACK);
  DrawTextRect(24, 95, ScreenWidth() - 48, 80,
               "Identifiants ADE / ByteBooks pour le DRM Adobe.",
               ALIGN_LEFT | VALIGN_TOP);

  layout_bb_form();

  /* Afficher les valeurs actuelles dans les boutons-zone */
  char email_lbl[KOBO_SHOP_STR + 32];
  char pass_lbl[64];
  if (g_cfg.bytebooks_email[0]) {
    snprintf(email_lbl, sizeof(email_lbl), "%s", g_cfg.bytebooks_email);
  } else {
    snprintf(email_lbl, sizeof(email_lbl), "E-mail (toucher pour saisir)");
  }
  if (g_cfg.bytebooks_password[0]) {
    snprintf(pass_lbl, sizeof(pass_lbl), "Mot de passe : ********");
  } else {
    snprintf(pass_lbl, sizeof(pass_lbl), "Mot de passe (toucher)");
  }
  g_bb_email_btn.label = email_lbl;
  g_bb_pass_btn.label = pass_lbl;
  draw_btn(&g_bb_email_btn);
  draw_btn(&g_bb_pass_btn);
  draw_btn(&g_bb_cancel);
  draw_btn(&g_bb_ok);
}

static void draw_library(void) {
  int ms = menu_btn_size();
  /* Titre + compteur ; marge sous le titre avant les boutons */
  int header_h = 64;
  FillArea(0, 0, ScreenWidth(), header_h, WHITE);
  make_btn(&g_menu_btn, ScreenWidth() - ms - 8, 6, ms, ms, "", 1, 0);
  draw_hamburger(g_menu_btn.x, g_menu_btn.y, ms);
  SetFont(font_title, BLACK);
  DrawString(14, 12, "Bibliothèque");
  SetFont(font_small, BLACK);
  char info[96];
  snprintf(info, sizeof(info), "%d / %d", g_view_count, g_books.count);
  int info_w = StringWidth(info);
  DrawString(ScreenWidth() - ms - 16 - info_w, 22, info);
  DrawLine(0, header_h - 1, ScreenWidth(), header_h - 1, BLACK);

  /* Espace sous « Bibliothèque » + boutons 2× plus hauts */
  int by = header_h + 14;
  int gap = 10;
  int bh = 88;
  int sw = (ScreenWidth() - 2 * btn_pad_x() - gap) / 2;
  make_btn(&g_search_btn, btn_pad_x(), by, sw, bh,
           g_search[0] ? g_search : "Rechercher…", 1, 0);
  char sortlbl[48];
  snprintf(sortlbl, sizeof(sortlbl), "Tri: %s", sort_label());
  make_btn(&g_sort_btn, btn_pad_x() + sw + gap, by, sw, bh, sortlbl, 1, 0);
  draw_btn(&g_search_btn);
  draw_btn(&g_sort_btn);

  int vis = visible_rows();
  int y = list_top_y();
  int cw = cover_w();
  int ch = cover_h();
  int aw = row_action_w();
  int row_x = 12;
  int row_w = ScreenWidth() - 24;
  int rh = row_h() - 6;
  int bottom = list_bottom_y();
  for (int i = 0; i < vis; i++) {
    int vi = g_list_top + i;
    if (vi >= g_view_count) {
      break;
    }
    if (y + rh > bottom) {
      break;
    }
    int idx = g_view[vi];
    BookEntry *b = &g_books.items[idx];
    DrawRect(row_x, y, row_w, rh, BLACK);

    /* Gros bouton toujours visible à droite */
    int ax = row_x + row_w - aw;
    int filled = b->downloaded ? 1 : 0;
    FillArea(ax, y, aw, rh, filled ? BLACK : WHITE);
    DrawRect(ax, y, aw, rh, BLACK);
    SetFont(font_body, filled ? WHITE : BLACK);
    const char *alabel = b->downloaded ? "Ouvrir" : "Télécharger";
    int tw = StringWidth((char *)alabel);
    DrawString(ax + (aw - tw) / 2, y + (rh - font_body->height) / 2 - 8, alabel);
    SetFont(font_small, filled ? WHITE : BLACK);
    DrawString(ax + 12, y + rh / 2 + 14, drm_label(b));

    int cx = row_x + 8;
    int cy = y + (rh - ch) / 2;
    draw_cover_at(b, cx, cy, cw, ch, 1);
    int tx = cx + cw + 12;
    int text_max_w = ax - tx - 8;
    SetFont(font_body, BLACK);
    char title[80];
    snprintf(title, sizeof(title), "%s", b->title);
    while ((int)strlen(title) > 8 && StringWidth(title) > text_max_w) {
      size_t n = strlen(title);
      title[n - 1] = 0;
      if (n > 4) {
        title[n - 4] = '.';
        title[n - 3] = '.';
        title[n - 2] = '.';
        title[n - 1] = 0;
      }
    }
    DrawString(tx, y + 18, title);
    SetFont(font_small, BLACK);
    char author[80];
    snprintf(author, sizeof(author), "%s", b->author);
    while ((int)strlen(author) > 4 && StringWidth(author) > text_max_w) {
      author[strlen(author) - 1] = 0;
    }
    DrawString(tx, y + 70, author);
    y += row_h();
  }
}

static void draw_book(void) {
  if (g_book_sel < 0 || g_book_sel >= g_books.count) {
    g_screen = SCR_LIBRARY;
    draw_library();
    return;
  }
  BookEntry *b = &g_books.items[g_book_sel];
  draw_header("Livre");
  draw_cover_at(b, 24, 95, 100, 150, 1);
  SetFont(font_body, BLACK);
  DrawTextRect(140, 95, ScreenWidth() - 164, 90, b->title,
               ALIGN_LEFT | VALIGN_TOP);
  SetFont(font_small, BLACK);
  DrawString(140, 195, b->author);
  char drm[120];
  snprintf(drm, sizeof(drm), "Format: %s %s", drm_label(b),
           b->url_format[0] ? b->url_format : "");
  DrawString(140, 235, drm);
  if (b->drm == BOOK_DRM_ADOBE) {
    DrawTextRect(24, 260, ScreenWidth() - 48, 40,
                 "Export Adobe (ACSM) → fulfil ByteBooks.",
                 ALIGN_LEFT | VALIGN_TOP);
  } else if (b->drm == BOOK_DRM_KDRM) {
    DrawTextRect(24, 260, ScreenWidth() - 48, 80,
                 "Pas d'ACSM API — .acsm depuis kobo.com "
                 "dans kobo_shop_data/acsm/ puis menu Importer.",
                 ALIGN_LEFT | VALIGN_TOP);
  }

  int can_dl = kobo_book_downloadable(b) || b->drm == BOOK_DRM_UNKNOWN;
  int y = 320;
  make_full_btn(&g_book_btns[0], y,
                can_dl ? "Télécharger" : "Télécharger (indisponible)");
  g_book_btns[0].enabled = can_dl ? 1 : 0;
  g_book_btns[0].filled = 0;
  y += btn_h() + btn_gap();
  make_full_btn(&g_book_btns[1], y, "Ouvrir");
  g_book_btns[1].enabled = b->downloaded ? 1 : 0;
  g_book_btns[1].filled = 0;
  y += btn_h() + btn_gap();
  make_full_btn(&g_book_btns[2], y, "Retour à la liste");
  g_book_btns[2].enabled = 1;
  g_book_btns[2].filled = 0;
  for (int i = 0; i < 3; i++) {
    draw_btn(&g_book_btns[i]);
  }
}

static void redraw(void) {
  ClearScreen();
  switch (g_screen) {
  case SCR_SETUP_MENU:
    draw_setup();
    break;
  case SCR_KOBO_CODE:
    draw_kobo_code();
    break;
  case SCR_BB_FORM:
    draw_bb_form();
    break;
  case SCR_LIBRARY:
    draw_library();
    break;
  case SCR_BOOK:
    draw_book();
    break;
  }
  /* Redessine la barre statut (sinon souvent absente après ClearScreen) */
  DrawPanel(NULL, "", "Kobo Shop", -1);
  FullUpdate();
}

static void do_download(void) {
  BookEntry *b = &g_books.items[g_book_sel];
  if (b->drm == BOOK_DRM_NONE_SAMPLE) {
    alert_ok(ICON_WARNING, "Téléchargement", "Échantillon uniquement.");
    return;
  }
  /* Toujours re-interroger l'API (Desktop→ACSM) : un cache [KDRM] peut
   * être obsolète, et certains titres n'offrent l'Adobe que via Desktop. */
  if (b->drm == BOOK_DRM_ADOBE && !config_bytebooks_ready(&g_cfg) &&
      !g_cfg.adept_activated) {
    alert_ok(ICON_WARNING, "ByteBooks",
             "Active ByteBooks avant de télécharger un ACSM.");
    return;
  }
  ShowHourglassForce();
  NetConnect3(1);
  char err[512];
  int dl = kobo_download_book(&g_cfg, b, err, sizeof(err));
  if (dl < 0) {
    library_cache_save(&g_books);
    HideHourglass();
    alert_ok(ICON_ERROR, "Téléchargement", err);
    redraw();
    return;
  }
  if (dl == 1) {
    /* KDRM archivé dans kobo_shop_data/kdrm/ — pas ouvrable ici */
    library_cache_save(&g_books);
    HideHourglass();
    alert_ok(ICON_INFORMATION, "KDRM archivé", err);
    redraw();
    return;
  }
  size_t n = strlen(b->local_path);
  if (n > 5 && strcasecmp(b->local_path + n - 5, ".acsm") == 0) {
    if (!g_cfg.adept_activated) {
      HideHourglass();
      alert_ok(ICON_WARNING, "ByteBooks",
               "Active ByteBooks (menu) avant le fulfil ACSM.");
      b->downloaded = 0;
      redraw();
      return;
    }
    if (adept_fulfill_book(b, err, sizeof(err)) != 0) {
      HideHourglass();
      /* ACSM seul : pas ouvrable par le lecteur PB */
      b->downloaded = 0;
      alert_ok(ICON_WARNING, "Fulfil ADEPT", err);
      library_cache_save(&g_books);
      redraw();
      return;
    }
  }
  library_cache_save(&g_books);
  HideHourglass();
  alert_ok(ICON_INFORMATION, "OK", "Fichier prêt.");
  redraw();
}

static void do_open(void) {
  BookEntry *b = &g_books.items[g_book_sel];
  if (!b->local_path[0] || access(b->local_path, R_OK) != 0) {
    alert_ok(ICON_WARNING, "Ouvrir", "Télécharge d'abord le livre.");
    return;
  }
  size_t n = strlen(b->local_path);
  if (n > 5 && strcasecmp(b->local_path + n - 5, ".acsm") == 0) {
    alert_ok(ICON_WARNING, "Ouvrir",
             "Fichier ACSM brut : le fulfil ADEPT a échoué.\n"
             "Retélécharge après activation ByteBooks.");
    return;
  }
  stamp_now_iso(b->last_opened, sizeof(b->last_opened));
  library_cache_save(&g_books);
  OpenBook(b->local_path, "", 0);
}

static void open_library_or_sync(void) {
  open_library_screen(1);
}

static int view_at_y(int y) {
  int rel = y - list_top_y();
  if (rel < 0) {
    return -1;
  }
  int row = rel / row_h();
  int vis = visible_rows();
  if (row < 0 || row >= vis) {
    return -1;
  }
  int vi = g_list_top + row;
  return (vi >= 0 && vi < g_view_count) ? vi : -1;
}

/* Zone du bouton Télécharger/Ouvrir (bandeau droit de la ligne). */
static int row_action_hit(int view_i, int x, int y) {
  if (view_i < 0) {
    return 0;
  }
  int rel = y - list_top_y();
  int row = rel / row_h();
  int y0 = list_top_y() + row * row_h();
  int rh = row_h() - 6;
  int aw = row_action_w();
  int ax = 12 + (ScreenWidth() - 24) - aw;
  return x >= ax && x < ax + aw && y >= y0 && y < y0 + rh;
}

/* Action depuis la liste (plus de page détail). */
static void row_quick_action(int view_i) {
  int idx = view_book_index(view_i);
  if (idx < 0) {
    return;
  }
  g_list_sel = view_i;
  g_book_sel = idx;
  BookEntry *b = &g_books.items[idx];
  if (b->downloaded && b->local_path[0] && access(b->local_path, R_OK) == 0) {
    do_open();
    return;
  }
  /* Probe DRM si besoin puis télécharge */
  if (b->drm == BOOK_DRM_UNKNOWN || b->drm == BOOK_DRM_KDRM) {
    ShowHourglassForce();
    NetConnect3(1);
    char err[256];
    if (kobo_probe_book(&g_cfg, b, err, sizeof(err)) != 0) {
      HideHourglass();
      alert_ok(ICON_ERROR, "Livre", err);
      return;
    }
    library_cache_save(&g_books);
    HideHourglass();
  }
  do_download();
}

static void handle_library_key(int key) {
  int vis = visible_rows();
  if (key == IV_KEY_UP || key == IV_KEY_PREV) {
    if (g_list_top > 0) {
      g_list_top--;
      redraw();
    }
  } else if (key == IV_KEY_DOWN || key == IV_KEY_NEXT) {
    if (g_list_top + vis < g_view_count) {
      g_list_top++;
      redraw();
    }
  } else if (key == IV_KEY_OK) {
    if (g_view_count > 0) {
      int vi = g_list_sel;
      if (vi < g_list_top || vi >= g_list_top + vis) {
        vi = g_list_top;
      }
      row_quick_action(vi);
    }
  } else if (key == IV_KEY_MENU) {
    open_lib_menu();
  } else if (key == IV_KEY_BACK) {
    g_screen = SCR_SETUP_MENU;
    redraw();
  }
}

static int is_back_key(int key) {
  return key == IV_KEY_BACK || key == IV_KEY_PREV || key == IV_KEY_PREV2;
}

static void handle_library_pointer_up(int x, int y) {
  int ms = menu_btn_size();
  make_btn(&g_menu_btn, ScreenWidth() - ms - 12, 8, ms, ms, "", 1, 0);
  int gap = 10;
  int sw = (ScreenWidth() - 2 * btn_pad_x() - gap) / 2;
  make_btn(&g_search_btn, btn_pad_x(), 84, sw, 52, "Rechercher…", 1, 0);
  make_btn(&g_sort_btn, btn_pad_x() + sw + gap, 84, sw, 52, "Tri", 1, 0);

  /* Scroll vertical : ne pas traiter comme un clic */
  if (g_ptr_scrolling) {
    int dy = g_ptr_y0 - y; /* doigt vers le haut → liste avance */
    int rows = dy / (row_h() / 2);
    if (rows != 0) {
      g_list_top = g_list_top_at_down + rows;
      clamp_list_top();
      redraw();
    }
    return;
  }

  if (hit_btn(&g_menu_btn, x, y)) {
    open_lib_menu();
    return;
  }
  if (hit_btn(&g_search_btn, x, y)) {
    start_search();
    return;
  }
  if (hit_btn(&g_sort_btn, x, y)) {
    g_sort = (SortMode)((g_sort + 1) % 3);
    rebuild_view();
    redraw();
    return;
  }

  int vi = view_at_y(y);
  if (vi < 0) {
    return;
  }
  /* Uniquement le bouton à droite déclenche l'action (le reste = scroll) */
  if (row_action_hit(vi, x, y)) {
    g_list_sel = vi;
    row_quick_action(vi);
  }
}

static void handle_pointer(int x, int y) {
  if (g_screen == SCR_LIBRARY) {
    handle_library_pointer_up(x, y);
  } else if (g_screen == SCR_SETUP_MENU) {
    layout_setup_btns();
    if (hit_btn(&g_setup_btns[0], x, y)) {
      start_kobo_activation();
    } else if (hit_btn(&g_setup_btns[1], x, y)) {
      show_bytebooks_form();
    } else if (hit_btn(&g_setup_btns[2], x, y)) {
      open_library_or_sync();
    } else if (hit_btn(&g_setup_btns[3], x, y)) {
      CloseApp();
    }
  } else if (g_screen == SCR_KOBO_CODE) {
    layout_kobo_footer();
    if (hit_btn(&g_kobo_cancel, x, y)) {
      cancel_kobo_activation();
    } else if (hit_btn(&g_kobo_ok, x, y)) {
      confirm_kobo_activation();
    }
  } else if (g_screen == SCR_BB_FORM) {
    layout_bb_form();
    if (hit_btn(&g_bb_email_btn, x, y)) {
      open_bb_keyboard(0);
    } else if (hit_btn(&g_bb_pass_btn, x, y)) {
      open_bb_keyboard(1);
    } else if (hit_btn(&g_bb_cancel, x, y)) {
      g_screen = SCR_SETUP_MENU;
      redraw();
    } else if (hit_btn(&g_bb_ok, x, y)) {
      submit_bytebooks();
    }
  } else if (g_screen == SCR_BOOK) {
    int by = 310;
    make_full_btn(&g_book_btns[0], by, "Télécharger / Fulfil");
    by += btn_h() + btn_gap();
    make_full_btn(&g_book_btns[1], by, "Ouvrir");
    by += btn_h() + btn_gap();
    make_full_btn(&g_book_btns[2], by, "Retour à la liste");
    for (int i = 0; i < 3; i++) {
      g_book_btns[i].enabled = 1;
    }
    /* Fiche livre retirée — retour liste */
    g_screen = SCR_LIBRARY;
    redraw();
  }
}

static void handle_key(int key) {
  if (key == IV_KEY_HOME) {
    CloseApp();
    return;
  }
  if (g_screen == SCR_SETUP_MENU) {
    if (key == '1' || key == IV_KEY_1) {
      start_kobo_activation();
    } else if (key == '2' || key == IV_KEY_2) {
      show_bytebooks_form();
    } else if (key == '3' || key == IV_KEY_3) {
      open_library_or_sync();
    } else if (key == '0' || key == IV_KEY_0 || is_back_key(key)) {
      CloseApp();
    }
  } else if (g_screen == SCR_KOBO_CODE) {
    if (is_back_key(key)) {
      cancel_kobo_activation();
    } else if (key == IV_KEY_OK && g_kobo_wait_ok) {
      confirm_kobo_activation();
    }
  } else if (g_screen == SCR_BB_FORM) {
    if (is_back_key(key)) {
      g_screen = SCR_SETUP_MENU;
      redraw();
    } else if (key == IV_KEY_OK) {
      submit_bytebooks();
    }
  } else if (g_screen == SCR_LIBRARY) {
    handle_library_key(key);
  } else if (g_screen == SCR_BOOK) {
    g_screen = SCR_LIBRARY;
    redraw();
  }
}

static int main_handler(int type, int par1, int par2) {
  switch (type) {
  case EVT_INIT: {
    font_title = OpenFont("LiberationSans", 40, 1);
    font_body = OpenFont("LiberationSans", 30, 0);
    font_small = OpenFont("LiberationSans", 22, 0);
    if (!font_title) {
      font_title = OpenFont(DEFAULTFONTB, 40, 1);
    }
    if (!font_body) {
      font_body = OpenFont(DEFAULTFONT, 30, 0);
    }
    if (!font_small) {
      font_small = OpenFont(DEFAULTFONT, 22, 0);
    }
    /* Barre système PocketBook (Wi‑Fi, batterie, heure) — bas d'écran */
    SetPanelType(PANEL_ENABLED);
    SetOrientation(0);
    http_global_init();
    config_ensure_dirs();
    config_load(&g_cfg);
    library_cache_load(&g_books);

    /* Helpers ADEPT dans applications/kobo_shop/ (pas à la racine apps) */
    {
      char *cwd = getcwd(NULL, 0);
      if (cwd) {
        char beside[KOBO_SHOP_PATH];
        snprintf(beside, sizeof(beside), "%s/kobo_shop", cwd);
        if (access(beside, R_OK) == 0) {
          adept_set_tools_dir(beside);
        } else {
          adept_set_tools_dir(cwd); /* ancien layout à plat */
        }
        free(cwd);
      }
    }
    if (access("/mnt/ext1/applications/kobo_shop/acsmdownloader", R_OK) == 0) {
      adept_set_tools_dir("/mnt/ext1/applications/kobo_shop");
    } else if (access("/mnt/ext1/applications/acsmdownloader", R_OK) == 0) {
      adept_set_tools_dir("/mnt/ext1/applications");
    }

    /* Rafraîchir le token si compte déjà lié */
    if (config_kobo_ready(&g_cfg) && g_cfg.refresh_token[0]) {
      char err[128];
      NetConnect3(1);
      if (kobo_refresh(&g_cfg, err, sizeof(err)) == 0) {
        config_save(&g_cfg);
      }
    }

    if (config_kobo_ready(&g_cfg)) {
      rebuild_view();
      g_screen = SCR_LIBRARY;
    } else {
      g_screen = SCR_SETUP_MENU;
    }
    break;
  }
  case EVT_SHOW:
    if (g_screen == SCR_LIBRARY && g_books.count == 0 &&
        config_kobo_ready(&g_cfg)) {
      sync_library();
      return 0;
    }
    redraw();
    break;
  case EVT_KEYPRESS:
    handle_key(par1);
    break;
  case EVT_POINTERDOWN:
    if (g_screen == SCR_LIBRARY) {
      g_ptr_down = 1;
      g_ptr_x0 = par1;
      g_ptr_y0 = par2;
      g_ptr_view = view_at_y(par2);
      g_ptr_scrolling = 0;
      g_list_top_at_down = g_list_top;
    }
    break;
  case EVT_POINTERDRAG:
  case EVT_POINTERMOVE:
    if (g_screen == SCR_LIBRARY && g_ptr_down) {
      int dx = par1 - g_ptr_x0;
      int dy = par2 - g_ptr_y0;
      int adx = dx < 0 ? -dx : dx;
      int ady = dy < 0 ? -dy : dy;
      /* Marquer scroll sans redraw live (e-ink) — appliqué au POINTERUP */
      if (ady > scroll_threshold() && ady > adx) {
        g_ptr_scrolling = 1;
      }
    }
    break;
  case EVT_POINTERUP:
    if (g_screen == SCR_LIBRARY) {
      handle_library_pointer_up(par1, par2);
      g_ptr_down = 0;
      g_ptr_scrolling = 0;
      g_ptr_view = -1;
    } else {
      handle_pointer(par1, par2);
    }
    break;
  case EVT_EXIT:
    g_polling = 0;
    g_poll_busy = 0;
    ClearTimerByName("kobo_poll");
    http_global_cleanup();
    if (font_title) {
      CloseFont(font_title);
    }
    if (font_body) {
      CloseFont(font_body);
    }
    if (font_small) {
      CloseFont(font_small);
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
