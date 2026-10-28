#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cJSON.h"

static char g_data[KOBO_SHOP_PATH];
static char g_books[KOBO_SHOP_PATH];
static char g_acsm_drop[KOBO_SHOP_PATH];
static char g_kdrm[KOBO_SHOP_PATH];
static char g_adept[KOBO_SHOP_PATH];
static char g_covers[KOBO_SHOP_PATH];
static char g_cfg[KOBO_SHOP_PATH];
static char g_lib[KOBO_SHOP_PATH];
static int g_paths_ok;

static void mkdir_p(const char *path) {
  char tmp[KOBO_SHOP_PATH];
  snprintf(tmp, sizeof(tmp), "%s", path);
  size_t len = strlen(tmp);
  if (len == 0) {
    return;
  }
  if (tmp[len - 1] == '/') {
    tmp[len - 1] = 0;
  }
  for (char *p = tmp + 1; *p; p++) {
    if (*p == '/') {
      *p = 0;
      mkdir(tmp, 0755);
      *p = '/';
    }
  }
  mkdir(tmp, 0755);
}

static int dir_writable(const char *path) {
  char test[KOBO_SHOP_PATH];
  mkdir_p(path);
  snprintf(test, sizeof(test), "%s/.wtest", path);
  FILE *f = fopen(test, "wb");
  if (!f) {
    return 0;
  }
  fputc('1', f);
  fclose(f);
  remove(test);
  return 1;
}

static void init_paths(void) {
  if (g_paths_ok) {
    return;
  }
  /*
   * Données app à la racine du stockage (créé par l'app) :
   *   /mnt/ext1/kobo_shop_data/{config,covers,adept,acsm,kdrm,…}
   * Libs / binaires installés : applications/kobo_shop/ (deploy).
   */
  const char *candidates[] = {
      "/mnt/ext1/kobo_shop_data",
      "/mnt/ext1/applications/kobo_shop_data", /* ancien emplacement */
      "/mnt/ext1/system/config/kobo_shop",
      "./kobo_shop_data",
      NULL,
  };
  const char *base = candidates[3];
  for (int i = 0; candidates[i]; i++) {
    if (dir_writable(candidates[i])) {
      base = candidates[i];
      break;
    }
  }
  snprintf(g_data, sizeof(g_data), "%s", base);
  if (dir_writable("/mnt/ext1/Books/Kobo")) {
    snprintf(g_books, sizeof(g_books), "%s", "/mnt/ext1/Books/Kobo");
  } else {
    snprintf(g_books, sizeof(g_books), "%s/books", g_data);
    mkdir_p(g_books);
  }
  /* Dépôts sous kobo_shop_data ; repli ancien /mnt/ext1/kobo_shop/{acsm,kdrm} */
  snprintf(g_acsm_drop, sizeof(g_acsm_drop), "%s/acsm", g_data);
  if (!dir_writable(g_acsm_drop) && dir_writable("/mnt/ext1/kobo_shop/acsm")) {
    snprintf(g_acsm_drop, sizeof(g_acsm_drop), "%s", "/mnt/ext1/kobo_shop/acsm");
  }
  snprintf(g_kdrm, sizeof(g_kdrm), "%s/kdrm", g_data);
  if (!dir_writable(g_kdrm) && dir_writable("/mnt/ext1/kobo_shop/kdrm")) {
    snprintf(g_kdrm, sizeof(g_kdrm), "%s", "/mnt/ext1/kobo_shop/kdrm");
  }
  snprintf(g_adept, sizeof(g_adept), "%s/adept", g_data);
  snprintf(g_covers, sizeof(g_covers), "%s/covers", g_data);
  snprintf(g_cfg, sizeof(g_cfg), "%s/config.json", g_data);
  snprintf(g_lib, sizeof(g_lib), "%s/library.json", g_data);
  g_paths_ok = 1;
}

const char *app_data_dir(void) {
  init_paths();
  return g_data;
}
const char *app_books_dir(void) {
  init_paths();
  return g_books;
}
const char *app_acsm_drop_dir(void) {
  init_paths();
  return g_acsm_drop;
}
const char *app_kdrm_dir(void) {
  init_paths();
  return g_kdrm;
}
const char *app_adept_dir(void) {
  init_paths();
  return g_adept;
}
const char *app_covers_dir(void) {
  init_paths();
  return g_covers;
}
const char *app_config_path(void) {
  init_paths();
  return g_cfg;
}
const char *app_library_cache_path(void) {
  init_paths();
  return g_lib;
}

void config_ensure_dirs(void) {
  init_paths();
  mkdir_p(g_data);
  mkdir_p(g_books);
  mkdir_p(g_acsm_drop);
  mkdir_p(g_kdrm);
  mkdir_p(g_adept);
  mkdir_p(g_covers);
  /* Aide visible en USB */
  {
    char note[KOBO_SHOP_PATH];
    snprintf(note, sizeof(note), "%s/LISEZMOI.txt", g_acsm_drop);
    if (access(note, R_OK) != 0) {
      FILE *f = fopen(note, "wb");
      if (f) {
        fputs("Depose ici les fichiers .acsm telecharges depuis kobo.com\n"
              "(Mes livres > Telecharger Adobe).\n"
              "Puis sur la liseuse : kobo_shop > menu > Importer ACSM.\n"
              "Les EPUB Adobe iront dans Books/Kobo/.\n",
              f);
        fclose(f);
      }
    }
  }
}

static void set_str(char *dst, size_t n, cJSON *o, const char *key) {
  cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
  if (cJSON_IsString(v) && v->valuestring) {
    snprintf(dst, n, "%s", v->valuestring);
  }
}

int config_load(AppConfig *cfg) {
  memset(cfg, 0, sizeof(*cfg));
  config_ensure_dirs();
  FILE *f = fopen(app_config_path(), "rb");
  if (!f) {
    return 0;
  }
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (sz <= 0 || sz > 1024 * 1024) {
    fclose(f);
    return -1;
  }
  char *buf = (char *)malloc((size_t)sz + 1);
  if (!buf) {
    fclose(f);
    return -1;
  }
  fread(buf, 1, (size_t)sz, f);
  buf[sz] = 0;
  fclose(f);

  cJSON *root = cJSON_Parse(buf);
  free(buf);
  if (!root) {
    return -1;
  }

  set_str(cfg->device_id, sizeof(cfg->device_id), root, "device_id");
  set_str(cfg->serial_number, sizeof(cfg->serial_number), root, "serial_number");
  set_str(cfg->access_token, sizeof(cfg->access_token), root, "access_token");
  set_str(cfg->refresh_token, sizeof(cfg->refresh_token), root, "refresh_token");
  set_str(cfg->user_key, sizeof(cfg->user_key), root, "user_key");
  set_str(cfg->user_id, sizeof(cfg->user_id), root, "user_id");
  set_str(cfg->library_sync_url, sizeof(cfg->library_sync_url), root,
          "library_sync_url");
  set_str(cfg->content_access_url, sizeof(cfg->content_access_url), root,
          "content_access_url");
  set_str(cfg->image_url_template, sizeof(cfg->image_url_template), root,
          "image_url_template");
  set_str(cfg->bytebooks_email, sizeof(cfg->bytebooks_email), root,
          "bytebooks_email");
  set_str(cfg->bytebooks_password, sizeof(cfg->bytebooks_password), root,
          "bytebooks_password");
  cJSON *aa = cJSON_GetObjectItemCaseSensitive(root, "adept_activated");
  cfg->adept_activated = cJSON_IsTrue(aa) || (cJSON_IsNumber(aa) && aa->valueint);

  cJSON_Delete(root);

  char act[KOBO_SHOP_PATH];
  snprintf(act, sizeof(act), "%s/activation.xml", app_adept_dir());
  if (access(act, R_OK) == 0) {
    cfg->adept_activated = 1;
  }
  return 1;
}

int config_save(const AppConfig *cfg) {
  config_ensure_dirs();
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "device_id", cfg->device_id);
  cJSON_AddStringToObject(root, "serial_number", cfg->serial_number);
  cJSON_AddStringToObject(root, "access_token", cfg->access_token);
  cJSON_AddStringToObject(root, "refresh_token", cfg->refresh_token);
  cJSON_AddStringToObject(root, "user_key", cfg->user_key);
  cJSON_AddStringToObject(root, "user_id", cfg->user_id);
  cJSON_AddStringToObject(root, "library_sync_url", cfg->library_sync_url);
  cJSON_AddStringToObject(root, "content_access_url", cfg->content_access_url);
  cJSON_AddStringToObject(root, "image_url_template", cfg->image_url_template);
  cJSON_AddStringToObject(root, "bytebooks_email", cfg->bytebooks_email);
  cJSON_AddStringToObject(root, "bytebooks_password", cfg->bytebooks_password);
  cJSON_AddBoolToObject(root, "adept_activated", cfg->adept_activated ? 1 : 0);

  char *printed = cJSON_Print(root);
  cJSON_Delete(root);
  if (!printed) {
    return -1;
  }
  FILE *f = fopen(app_config_path(), "wb");
  if (!f) {
    free(printed);
    return -1;
  }
  fputs(printed, f);
  fclose(f);
  free(printed);
  return 0;
}

int config_kobo_ready(const AppConfig *cfg) {
  return cfg->access_token[0] && cfg->user_key[0] && cfg->device_id[0];
}

int config_bytebooks_ready(const AppConfig *cfg) {
  return cfg->bytebooks_email[0] && cfg->bytebooks_password[0];
}

static const char *drm_name(BookDrmKind k) {
  switch (k) {
  case BOOK_DRM_SIGNED_NO_DRM:
    return "SignedNoDrm";
  case BOOK_DRM_ADOBE:
    return "AdobeDrm";
  case BOOK_DRM_KDRM:
    return "KDRM";
  case BOOK_DRM_NONE_SAMPLE:
    return "None";
  default:
    return "Unknown";
  }
}

static BookDrmKind drm_parse(const char *s) {
  if (!s) {
    return BOOK_DRM_UNKNOWN;
  }
  if (strcmp(s, "SignedNoDrm") == 0) {
    return BOOK_DRM_SIGNED_NO_DRM;
  }
  if (strcmp(s, "AdobeDrm") == 0) {
    return BOOK_DRM_ADOBE;
  }
  if (strcmp(s, "KDRM") == 0) {
    return BOOK_DRM_KDRM;
  }
  if (strcmp(s, "None") == 0) {
    return BOOK_DRM_NONE_SAMPLE;
  }
  return BOOK_DRM_OTHER;
}

int library_cache_save(const BookList *list) {
  config_ensure_dirs();
  cJSON *arr = cJSON_CreateArray();
  for (int i = 0; i < list->count; i++) {
    const BookEntry *b = &list->items[i];
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "product_id", b->product_id);
    cJSON_AddStringToObject(o, "title", b->title);
    cJSON_AddStringToObject(o, "author", b->author);
    cJSON_AddBoolToObject(o, "archived", b->archived);
    cJSON_AddStringToObject(o, "drm", drm_name(b->drm));
    cJSON_AddStringToObject(o, "url_format", b->url_format);
    cJSON_AddStringToObject(o, "local_path", b->local_path);
    cJSON_AddBoolToObject(o, "downloaded", b->downloaded);
    cJSON_AddStringToObject(o, "cover_id", b->cover_id);
    cJSON_AddStringToObject(o, "date_added", b->date_added);
    cJSON_AddStringToObject(o, "last_opened", b->last_opened);
    cJSON_AddItemToArray(arr, o);
  }
  char *printed = cJSON_Print(arr);
  cJSON_Delete(arr);
  if (!printed) {
    return -1;
  }
  FILE *f = fopen(app_library_cache_path(), "wb");
  if (!f) {
    free(printed);
    return -1;
  }
  fputs(printed, f);
  fclose(f);
  free(printed);
  return 0;
}

int library_cache_load(BookList *list) {
  memset(list, 0, sizeof(*list));
  FILE *f = fopen(app_library_cache_path(), "rb");
  if (!f) {
    return 0;
  }
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (sz <= 0 || sz > 8 * 1024 * 1024) {
    fclose(f);
    return -1;
  }
  char *buf = (char *)malloc((size_t)sz + 1);
  if (!buf) {
    fclose(f);
    return -1;
  }
  fread(buf, 1, (size_t)sz, f);
  buf[sz] = 0;
  fclose(f);
  cJSON *arr = cJSON_Parse(buf);
  free(buf);
  if (!cJSON_IsArray(arr)) {
    cJSON_Delete(arr);
    return -1;
  }
  cJSON *it;
  cJSON_ArrayForEach(it, arr) {
    if (list->count >= KOBO_SHOP_MAX_BOOKS) {
      break;
    }
    BookEntry *b = &list->items[list->count];
    memset(b, 0, sizeof(*b));
    set_str(b->product_id, sizeof(b->product_id), it, "product_id");
    set_str(b->title, sizeof(b->title), it, "title");
    set_str(b->author, sizeof(b->author), it, "author");
    set_str(b->url_format, sizeof(b->url_format), it, "url_format");
    set_str(b->local_path, sizeof(b->local_path), it, "local_path");
    set_str(b->cover_id, sizeof(b->cover_id), it, "cover_id");
    set_str(b->date_added, sizeof(b->date_added), it, "date_added");
    set_str(b->last_opened, sizeof(b->last_opened), it, "last_opened");
    cJSON *ar = cJSON_GetObjectItemCaseSensitive(it, "archived");
    b->archived = cJSON_IsTrue(ar) || (cJSON_IsNumber(ar) && ar->valueint);
    cJSON *dl = cJSON_GetObjectItemCaseSensitive(it, "downloaded");
    b->downloaded = cJSON_IsTrue(dl) || (cJSON_IsNumber(dl) && dl->valueint);
    cJSON *drm = cJSON_GetObjectItemCaseSensitive(it, "drm");
    b->drm = drm_parse(cJSON_IsString(drm) ? drm->valuestring : NULL);
    if (b->local_path[0] && access(b->local_path, R_OK) == 0) {
      b->downloaded = 1;
    }
    list->count++;
  }
  cJSON_Delete(arr);
  return 1;
}
