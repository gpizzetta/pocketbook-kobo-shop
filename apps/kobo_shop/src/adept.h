#ifndef KOBO_SHOP_ADEPT_H
#define KOBO_SHOP_ADEPT_H

#include <stddef.h>

#include "config.h"

/* Locate helper binaries next to the .app or under data dir. */
void adept_set_tools_dir(const char *dir);

int adept_tools_available(void);

/* Activate ByteBooks → writes app_adept_dir(). Uses bundled adept_activate. */
int adept_activate(AppConfig *cfg, char *err, size_t err_len);

/* Fulfil ACSM → EPUB DRM in out_epub path (no DRM removal). */
int adept_fulfill_acsm(const char *acsm_path, const char *out_epub, char *err,
                       size_t err_len);

/* If path ends with .acsm, fulfil beside it as .epub and update book. */
int adept_fulfill_book(BookEntry *book, char *err, size_t err_len);

/*
 * Parcourt app_acsm_drop_dir() (kobo_shop/acsm/) :
 *  - chaque .acsm → fulfil ADEPT → EPUB dans app_books_dir() (Books/Kobo)
 *  - supprime le .acsm du dépôt après succès
 *  - rattache à un BookEntry si le titre correspond
 * *ok / *fail : compteurs (peuvent être NULL).
 * Retourne -1 si prérequis manquant (outils / activation), 0 sinon.
 */
int adept_import_books_folder(BookList *list, int *ok, int *fail, char *err,
                              size_t err_len);

#endif
