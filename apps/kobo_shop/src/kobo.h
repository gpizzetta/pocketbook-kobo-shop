#ifndef KOBO_SHOP_KOBO_H
#define KOBO_SHOP_KOBO_H

#include <stddef.h>

#include "config.h"

typedef struct {
  char code[32];
  char poll_url[KOBO_SHOP_PATH];
  char err[256];
} KoboActivation;

int kobo_ensure_device_ids(AppConfig *cfg);

/* Anonymous device auth (needed before ActivateOnWeb). */
int kobo_auth_device(AppConfig *cfg, int with_user_key, char *err, size_t err_len);

int kobo_refresh(AppConfig *cfg, char *err, size_t err_len);

/* Start ActivateOnWeb: fills code + poll_url. */
int kobo_activation_start(AppConfig *cfg, KoboActivation *act);

/* Poll once. Returns 1 if complete, 0 if pending, -1 on error. */
int kobo_activation_poll(AppConfig *cfg, KoboActivation *act);

int kobo_initialization(AppConfig *cfg, char *err, size_t err_len);

/* Sync library metadata (titles). DRM filled lightly; call probe for details. */
int kobo_library_sync(AppConfig *cfg, BookList *list, char *err, size_t err_len);

/* Interroge content_access : remplit book->drm / url_format (pas de DL). */
int kobo_probe_book(AppConfig *cfg, BookEntry *book, char *err, size_t err_len);

/*
 * Télécharge SignedNoDrm, ACSM (Desktop), ou archive KDRM dans app_kdrm_dir().
 * Retourne 0 = OK lisible, 1 = KDRM archivé (message dans err), -1 = échec.
 */
int kobo_download_book(AppConfig *cfg, BookEntry *book, char *err, size_t err_len);

int kobo_book_downloadable(const BookEntry *book);

/* URL CDN cover Kobo ; path cache local (télécharge si besoin). */
int kobo_cover_url(const AppConfig *cfg, const BookEntry *book, int w, int h,
                   char *out, size_t out_len);
int kobo_ensure_cover(AppConfig *cfg, const BookEntry *book, int w, int h,
                      char *path_out, size_t path_len);

#endif
