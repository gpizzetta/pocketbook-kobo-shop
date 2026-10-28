#ifndef KOBO_SHOP_CONFIG_H
#define KOBO_SHOP_CONFIG_H

#define KOBO_SHOP_MAX_BOOKS 512
#define KOBO_SHOP_STR 256
/* Access/Refresh Kobo ~600+ chars ; marge large pour évolutions API */
#define KOBO_SHOP_TOKEN 2048
#define KOBO_SHOP_PATH 512

typedef struct {
  char device_id[128];
  char serial_number[64];
  char access_token[KOBO_SHOP_TOKEN];
  char refresh_token[KOBO_SHOP_TOKEN];
  char user_key[KOBO_SHOP_TOKEN];
  char user_id[128];
  char library_sync_url[KOBO_SHOP_PATH];
  char content_access_url[KOBO_SHOP_PATH]; /* template with {ProductId} */
  /* https://cdn.kobo.com/book-images/{ImageId}/{Width}/{Height}/false/image.jpg */
  char image_url_template[KOBO_SHOP_PATH];

  char bytebooks_email[KOBO_SHOP_STR];
  char bytebooks_password[KOBO_SHOP_STR];
  int adept_activated; /* 1 if adept/ has activation.xml */
} AppConfig;

typedef enum {
  BOOK_DRM_UNKNOWN = 0,
  BOOK_DRM_SIGNED_NO_DRM,
  BOOK_DRM_ADOBE,
  BOOK_DRM_KDRM,
  BOOK_DRM_NONE_SAMPLE,
  BOOK_DRM_OTHER
} BookDrmKind;

typedef struct {
  char product_id[64];
  char title[KOBO_SHOP_STR];
  char author[KOBO_SHOP_STR];
  int archived;
  BookDrmKind drm;
  char url_format[32];
  char local_path[KOBO_SHOP_PATH];
  int downloaded;
  char cover_id[64];     /* CoverImageId Kobo */
  char date_added[40];   /* BookEntitlement.Created ISO */
  char last_opened[40];  /* LastTimeStartedReading ou ouverture locale */
} BookEntry;

typedef struct {
  BookEntry items[KOBO_SHOP_MAX_BOOKS];
  int count;
} BookList;

const char *app_data_dir(void);
const char *app_books_dir(void);
/* Dépôt USB des .acsm : /mnt/ext1/kobo_shop_data/acsm/ */
const char *app_acsm_drop_dir(void);
/* EPUB KDRM bruts : /mnt/ext1/kobo_shop_data/kdrm/ */
const char *app_kdrm_dir(void);
const char *app_adept_dir(void);
const char *app_covers_dir(void);
const char *app_config_path(void);
const char *app_library_cache_path(void);

void config_ensure_dirs(void);
int config_load(AppConfig *cfg);
int config_save(const AppConfig *cfg);

int library_cache_save(const BookList *list);
int library_cache_load(BookList *list);

int config_kobo_ready(const AppConfig *cfg);
int config_bytebooks_ready(const AppConfig *cfg);

#endif
