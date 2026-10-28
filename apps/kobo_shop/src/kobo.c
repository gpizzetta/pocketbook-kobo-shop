#include "kobo.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "cJSON.h"
#include "http.h"

#define PLATFORM_ID "00000000-0000-0000-0000-000000000373"
#define APP_VERSION "4.38.23171"
#define AFFILIATE "Kobo"

static void random_hex(char *dst, size_t hex_len) {
  static int seeded;
  if (!seeded) {
    srand((unsigned)time(NULL) ^ (unsigned)getpid());
    seeded = 1;
  }
  static const char *hex = "0123456789abcdef";
  for (size_t i = 0; i < hex_len; i++) {
    dst[i] = hex[rand() % 16];
  }
  dst[hex_len] = 0;
}

static void b64_platform(char *out, size_t out_len) {
  /* base64 of PLATFORM_ID — fixed known value */
  snprintf(out, out_len, "MDAwMDAwMDAtMDAwMC0wMDAwLTAwMDAtMDAwMDAwMDAwMzcz");
}

int kobo_ensure_device_ids(AppConfig *cfg) {
  if (!cfg->device_id[0]) {
    random_hex(cfg->device_id, 64);
  }
  if (!cfg->serial_number[0]) {
    random_hex(cfg->serial_number, 32);
  }
  return 0;
}

int kobo_auth_device(AppConfig *cfg, int with_user_key, char *err, size_t err_len) {
  kobo_ensure_device_ids(cfg);
  char client_key[80];
  b64_platform(client_key, sizeof(client_key));

  cJSON *post = cJSON_CreateObject();
  cJSON_AddStringToObject(post, "AffiliateName", AFFILIATE);
  cJSON_AddStringToObject(post, "AppVersion", APP_VERSION);
  cJSON_AddStringToObject(post, "ClientKey", client_key);
  cJSON_AddStringToObject(post, "DeviceId", cfg->device_id);
  cJSON_AddStringToObject(post, "PlatformId", PLATFORM_ID);
  cJSON_AddStringToObject(post, "SerialNumber", cfg->serial_number);
  if (with_user_key && cfg->user_key[0]) {
    cJSON_AddStringToObject(post, "UserKey", cfg->user_key);
  }
  char *body = cJSON_PrintUnformatted(post);
  cJSON_Delete(post);
  if (!body) {
    snprintf(err, err_len, "json encode failed");
    return -1;
  }

  HttpResponse r;
  int rc = http_request("POST", "https://storeapi.kobo.com/v1/auth/device", NULL,
                        NULL, NULL, body, 45, &r);
  free(body);
  if (rc != 0) {
    snprintf(err, err_len, "%s", r.err);
    http_response_free(&r);
    return -1;
  }
  if (r.status < 200 || r.status >= 300) {
    snprintf(err, err_len, "auth/device HTTP %ld", r.status);
    http_response_free(&r);
    return -1;
  }

  cJSON *root = cJSON_Parse(r.body ? r.body : "");
  http_response_free(&r);
  if (!root) {
    snprintf(err, err_len, "auth/device bad json");
    return -1;
  }
  cJSON *tok = cJSON_GetObjectItemCaseSensitive(root, "AccessToken");
  cJSON *ref = cJSON_GetObjectItemCaseSensitive(root, "RefreshToken");
  cJSON *uk = cJSON_GetObjectItemCaseSensitive(root, "UserKey");
  if (!cJSON_IsString(tok) || !cJSON_IsString(ref)) {
    cJSON_Delete(root);
    snprintf(err, err_len, "auth/device missing tokens");
    return -1;
  }
  snprintf(cfg->access_token, sizeof(cfg->access_token), "%s", tok->valuestring);
  snprintf(cfg->refresh_token, sizeof(cfg->refresh_token), "%s", ref->valuestring);
  if (cJSON_IsString(uk) && uk->valuestring[0]) {
    snprintf(cfg->user_key, sizeof(cfg->user_key), "%s", uk->valuestring);
  }
  cJSON_Delete(root);
  return 0;
}

int kobo_refresh(AppConfig *cfg, char *err, size_t err_len) {
  char client_key[80];
  b64_platform(client_key, sizeof(client_key));
  cJSON *post = cJSON_CreateObject();
  cJSON_AddStringToObject(post, "AppVersion", APP_VERSION);
  cJSON_AddStringToObject(post, "ClientKey", client_key);
  cJSON_AddStringToObject(post, "PlatformId", PLATFORM_ID);
  cJSON_AddStringToObject(post, "RefreshToken", cfg->refresh_token);
  char *body = cJSON_PrintUnformatted(post);
  cJSON_Delete(post);
  if (!body) {
    snprintf(err, err_len, "json encode failed");
    return -1;
  }
  HttpResponse r;
  int rc = http_request("POST", "https://storeapi.kobo.com/v1/auth/refresh",
                        cfg->access_token, NULL, NULL, body, 45, &r);
  free(body);
  if (rc != 0) {
    snprintf(err, err_len, "%s", r.err);
    http_response_free(&r);
    return -1;
  }
  cJSON *root = cJSON_Parse(r.body ? r.body : "");
  http_response_free(&r);
  if (!root) {
    snprintf(err, err_len, "refresh bad json");
    return -1;
  }
  cJSON *tok = cJSON_GetObjectItemCaseSensitive(root, "AccessToken");
  cJSON *ref = cJSON_GetObjectItemCaseSensitive(root, "RefreshToken");
  if (!cJSON_IsString(tok) || !cJSON_IsString(ref)) {
    cJSON_Delete(root);
    snprintf(err, err_len, "refresh missing tokens");
    return -1;
  }
  snprintf(cfg->access_token, sizeof(cfg->access_token), "%s", tok->valuestring);
  snprintf(cfg->refresh_token, sizeof(cfg->refresh_token), "%s", ref->valuestring);
  cJSON_Delete(root);
  return 0;
}

static int get_authed(AppConfig *cfg, const char *url, const char *extra_name,
                      const char *extra_val, HttpResponse *out, char *err,
                      size_t err_len) {
  int timeout = 120; /* library_sync peut être lourd */
  int rc = http_request("GET", url, cfg->access_token, extra_name, extra_val, NULL,
                        timeout, out);
  if (rc != 0) {
    snprintf(err, err_len, "%s", out->err);
    return -1;
  }
  if (out->status == 401) {
    http_response_free(out);
    if (kobo_refresh(cfg, err, err_len) != 0) {
      return -1;
    }
    config_save(cfg);
    rc = http_request("GET", url, cfg->access_token, extra_name, extra_val, NULL,
                      timeout, out);
    if (rc != 0) {
      snprintf(err, err_len, "%s", out->err);
      return -1;
    }
  }
  if (out->status < 200 || out->status >= 300) {
    const char *body = out->body ? out->body : "";
    char snip[120];
    size_t n = 0;
    while (body[n] && n + 1 < sizeof(snip) && n < 100) {
      char c = body[n];
      snip[n] = (c >= 32 && c < 127) ? c : ' ';
      n++;
    }
    snip[n] = 0;
    if (snip[0]) {
      snprintf(err, err_len, "HTTP %ld: %s", out->status, snip);
    } else {
      snprintf(err, err_len, "HTTP %ld (%s)", out->status, url);
    }
    return -1;
  }
  return 0;
}

static int extract_attr(const char *html, const char *key, char *out, size_t out_len) {
  char pat[128];
  snprintf(pat, sizeof(pat), "%s=\"", key);
  const char *p = strstr(html, pat);
  if (!p) {
    return -1;
  }
  p += strlen(pat);
  const char *e = strchr(p, '"');
  if (!e) {
    return -1;
  }
  size_t n = (size_t)(e - p);
  if (n >= out_len) {
    n = out_len - 1;
  }
  memcpy(out, p, n);
  out[n] = 0;
  return 0;
}

static void html_unescape_amp(char *s) {
  char *r = s, *w = s;
  while (*r) {
    if (strncmp(r, "&amp;", 5) == 0) {
      *w++ = '&';
      r += 5;
    } else {
      *w++ = *r++;
    }
  }
  *w = 0;
}

int kobo_activation_start(AppConfig *cfg, KoboActivation *act) {
  memset(act, 0, sizeof(*act));
  if (kobo_auth_device(cfg, 0, act->err, sizeof(act->err)) != 0) {
    return -1;
  }
  config_save(cfg);

  char url[KOBO_SHOP_PATH];
  snprintf(url, sizeof(url),
           "https://auth.kobobooks.com/ActivateOnWeb?pwspid=%s&wsa=%s&pwsdid=%s"
           "&pwsav=%s&pwsdm=%s&pwspos=3.0.35%%2B&pwspov=NA",
           PLATFORM_ID, AFFILIATE, cfg->device_id, APP_VERSION, PLATFORM_ID);

  HttpResponse r;
  if (http_request("GET", url, NULL, NULL, NULL, NULL, 45, &r) != 0) {
    snprintf(act->err, sizeof(act->err), "%s", r.err);
    http_response_free(&r);
    return -1;
  }
  if (!r.body) {
    snprintf(act->err, sizeof(act->err), "empty ActivateOnWeb");
    http_response_free(&r);
    return -1;
  }

  char poll_path[KOBO_SHOP_PATH];
  if (extract_attr(r.body, "data-poll-endpoint", poll_path, sizeof(poll_path)) !=
      0) {
    snprintf(act->err, sizeof(act->err), "poll endpoint not found");
    http_response_free(&r);
    return -1;
  }
  html_unescape_amp(poll_path);
  if (poll_path[0] == '/') {
    snprintf(act->poll_url, sizeof(act->poll_url), "https://auth.kobobooks.com%s",
             poll_path);
  } else {
    snprintf(act->poll_url, sizeof(act->poll_url), "%s", poll_path);
  }

  /* Prefer …%26code%3D123456 (qrcodegenerator), else code= */
  const char *c = strstr(r.body, "%26code%3D");
  if (c) {
    c += strlen("%26code%3D");
  } else if ((c = strstr(r.body, "code%3D")) != NULL) {
    c += strlen("code%3D");
  } else if ((c = strstr(r.body, "code=")) != NULL) {
    c += strlen("code=");
  } else {
    snprintf(act->err, sizeof(act->err), "activation code not found");
    http_response_free(&r);
    return -1;
  }
  size_t i = 0;
  while (c[i] && isdigit((unsigned char)c[i]) && i + 1 < sizeof(act->code)) {
    act->code[i] = c[i];
    i++;
  }
  act->code[i] = 0;
  http_response_free(&r);
  if (!act->code[0]) {
    snprintf(act->err, sizeof(act->err), "empty code");
    return -1;
  }
  return 0;
}

static int hex_nibble(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

static void url_decode_inplace(char *s) {
  char *r = s, *w = s;
  while (*r) {
    if (*r == '%' && hex_nibble(r[1]) >= 0 && hex_nibble(r[2]) >= 0) {
      *w++ = (char)((hex_nibble(r[1]) << 4) | hex_nibble(r[2]));
      r += 3;
    } else if (*r == '+') {
      *w++ = ' ';
      r++;
    } else {
      *w++ = *r++;
    }
  }
  *w = 0;
}

static int parse_query_param(const char *url, const char *key, char *out,
                             size_t out_len) {
  char pat[64];
  snprintf(pat, sizeof(pat), "%s=", key);
  const char *p = strstr(url, pat);
  if (!p) {
    return -1;
  }
  p += strlen(pat);
  size_t i = 0;
  while (p[i] && p[i] != '&' && i + 1 < out_len) {
    out[i] = p[i];
    i++;
  }
  out[i] = 0;
  url_decode_inplace(out);
  return i > 0 ? 0 : -1;
}

int kobo_activation_poll(AppConfig *cfg, KoboActivation *act) {
  HttpResponse r;
  /* Timeout court : l'UI doit pouvoir annuler entre deux polls. */
  if (http_request("POST", act->poll_url, NULL, NULL, NULL, "", 8, &r) != 0) {
    snprintf(act->err, sizeof(act->err), "%s", r.err);
    http_response_free(&r);
    return -1;
  }
  cJSON *root = cJSON_Parse(r.body ? r.body : "");
  http_response_free(&r);
  if (!root) {
    snprintf(act->err, sizeof(act->err), "poll bad json");
    return -1;
  }
  cJSON *st = cJSON_GetObjectItemCaseSensitive(root, "Status");
  if (!cJSON_IsString(st) || strcmp(st->valuestring, "Complete") != 0) {
    cJSON_Delete(root);
    return 0; /* pending */
  }
  cJSON *redir = cJSON_GetObjectItemCaseSensitive(root, "RedirectUrl");
  if (!cJSON_IsString(redir)) {
    cJSON_Delete(root);
    snprintf(act->err, sizeof(act->err), "no RedirectUrl");
    return -1;
  }
  if (parse_query_param(redir->valuestring, "userId", cfg->user_id,
                        sizeof(cfg->user_id)) != 0 ||
      parse_query_param(redir->valuestring, "userKey", cfg->user_key,
                        sizeof(cfg->user_key)) != 0) {
    cJSON_Delete(root);
    snprintf(act->err, sizeof(act->err), "userId/userKey missing");
    return -1;
  }
  cJSON_Delete(root);
  if (kobo_auth_device(cfg, 1, act->err, sizeof(act->err)) != 0) {
    return -1;
  }
  config_save(cfg);
  return 1;
}

int kobo_initialization(AppConfig *cfg, char *err, size_t err_len) {
  HttpResponse r;
  if (get_authed(cfg, "https://storeapi.kobo.com/v1/initialization", NULL, NULL,
                 &r, err, err_len) != 0) {
    http_response_free(&r);
    return -1;
  }
  cJSON *root = cJSON_Parse(r.body ? r.body : "");
  http_response_free(&r);
  if (!root) {
    snprintf(err, err_len, "initialization bad json");
    return -1;
  }
  cJSON *res = cJSON_GetObjectItemCaseSensitive(root, "Resources");
  cJSON *ls = cJSON_GetObjectItemCaseSensitive(res, "library_sync");
  cJSON *ca = cJSON_GetObjectItemCaseSensitive(res, "content_access_book");
  cJSON *img = cJSON_GetObjectItemCaseSensitive(res, "image_url_template");
  if (!cJSON_IsString(ls) || !cJSON_IsString(ca)) {
    cJSON_Delete(root);
    snprintf(err, err_len, "Resources URLs missing");
    return -1;
  }
  snprintf(cfg->library_sync_url, sizeof(cfg->library_sync_url), "%s",
           ls->valuestring);
  snprintf(cfg->content_access_url, sizeof(cfg->content_access_url), "%s",
           ca->valuestring);
  if (cJSON_IsString(img) && img->valuestring[0]) {
    snprintf(cfg->image_url_template, sizeof(cfg->image_url_template), "%s",
             img->valuestring);
  } else {
    snprintf(cfg->image_url_template, sizeof(cfg->image_url_template),
             "https://cdn.kobo.com/book-images/{ImageId}/{Width}/{Height}/false/"
             "image.jpg");
  }
  cJSON_Delete(root);
  config_save(cfg);
  return 0;
}

static cJSON *entitlement_meta(cJSON *entry) {
  const char *keys[] = {"NewEntitlement", "ChangedEntitlement", "Entitlement", NULL};
  for (int i = 0; keys[i]; i++) {
    cJSON *ent = cJSON_GetObjectItemCaseSensitive(entry, keys[i]);
    if (cJSON_IsObject(ent)) {
      cJSON *meta = cJSON_GetObjectItemCaseSensitive(ent, "BookMetadata");
      if (cJSON_IsObject(meta)) {
        return ent;
      }
    }
  }
  return NULL;
}

static void copy_iso_prefix(char *dst, size_t dst_len, const char *src) {
  if (!src || !src[0]) {
    dst[0] = 0;
    return;
  }
  snprintf(dst, dst_len, "%s", src);
  /* Tronque aux ms pour un tri strcmp stable */
  for (size_t i = 0; dst[i]; i++) {
    if (dst[i] == '.') {
      dst[i] = 0;
      break;
    }
  }
}

int kobo_library_sync(AppConfig *cfg, BookList *list, char *err, size_t err_len) {
  /* Préserver téléchargements / DRM / last_opened locaux */
  BookList prev;
  memcpy(&prev, list, sizeof(prev));

  memset(list, 0, sizeof(*list));
  if (!cfg->library_sync_url[0]) {
    if (kobo_initialization(cfg, err, err_len) != 0) {
      return -1;
    }
  }

  /* Token de pagination Kobo : souvent > 4 Ko */
  char *sync_token = (char *)calloc(1, 16384);
  if (!sync_token) {
    snprintf(err, err_len, "oom synctoken");
    return -1;
  }
  for (int page = 0; page < 100; page++) {
    HttpResponse r;
    const char *hn = sync_token[0] ? "x-kobo-synctoken" : NULL;
    const char *hv = sync_token[0] ? sync_token : NULL;
    if (get_authed(cfg, cfg->library_sync_url, hn, hv, &r, err, err_len) != 0) {
      http_response_free(&r);
      free(sync_token);
      return -1;
    }
    cJSON *batch = cJSON_Parse(r.body ? r.body : "");
    int cont = r.header_sync && strcmp(r.header_sync, "continue") == 0;
    if (r.header_synctoken) {
      snprintf(sync_token, 16384, "%s", r.header_synctoken);
    } else {
      sync_token[0] = 0;
    }
    http_response_free(&r);
    if (!cJSON_IsArray(batch)) {
      cJSON_Delete(batch);
      free(sync_token);
      snprintf(err, err_len, "library_sync not array");
      return -1;
    }

    cJSON *entry;
    cJSON_ArrayForEach(entry, batch) {
      cJSON *ent = entitlement_meta(entry);
      if (!ent) {
        continue;
      }
      cJSON *meta = cJSON_GetObjectItemCaseSensitive(ent, "BookMetadata");
      cJSON *rid = cJSON_GetObjectItemCaseSensitive(meta, "RevisionId");
      if (!cJSON_IsString(rid)) {
        rid = cJSON_GetObjectItemCaseSensitive(meta, "CrossRevisionId");
      }
      if (!cJSON_IsString(rid)) {
        continue;
      }
      /* dedupe */
      int exists = 0;
      for (int i = 0; i < list->count; i++) {
        if (strcmp(list->items[i].product_id, rid->valuestring) == 0) {
          exists = 1;
          break;
        }
      }
      if (exists || list->count >= KOBO_SHOP_MAX_BOOKS) {
        continue;
      }
      BookEntry *b = &list->items[list->count];
      memset(b, 0, sizeof(*b));
      snprintf(b->product_id, sizeof(b->product_id), "%s", rid->valuestring);
      cJSON *title = cJSON_GetObjectItemCaseSensitive(meta, "Title");
      snprintf(b->title, sizeof(b->title), "%s",
               cJSON_IsString(title) ? title->valuestring : "?");
      cJSON *roles = cJSON_GetObjectItemCaseSensitive(meta, "ContributorRoles");
      b->author[0] = '?';
      b->author[1] = 0;
      if (cJSON_IsArray(roles) && cJSON_GetArraySize(roles) > 0) {
        cJSON *first = cJSON_GetArrayItem(roles, 0);
        cJSON *name = cJSON_GetObjectItemCaseSensitive(first, "Name");
        if (cJSON_IsString(name)) {
          snprintf(b->author, sizeof(b->author), "%s", name->valuestring);
        }
      }
      cJSON *cover = cJSON_GetObjectItemCaseSensitive(meta, "CoverImageId");
      if (cJSON_IsString(cover)) {
        snprintf(b->cover_id, sizeof(b->cover_id), "%s", cover->valuestring);
      }
      cJSON *bent = cJSON_GetObjectItemCaseSensitive(ent, "BookEntitlement");
      cJSON *rem = cJSON_GetObjectItemCaseSensitive(bent, "IsRemoved");
      b->archived = cJSON_IsTrue(rem) || (cJSON_IsNumber(rem) && rem->valueint);
      cJSON *created = cJSON_GetObjectItemCaseSensitive(bent, "Created");
      if (cJSON_IsString(created)) {
        copy_iso_prefix(b->date_added, sizeof(b->date_added),
                        created->valuestring);
      }
      cJSON *rs = cJSON_GetObjectItemCaseSensitive(ent, "ReadingState");
      cJSON *si = cJSON_GetObjectItemCaseSensitive(rs, "StatusInfo");
      cJSON *lor = cJSON_GetObjectItemCaseSensitive(si, "LastTimeStartedReading");
      if (cJSON_IsString(lor)) {
        copy_iso_prefix(b->last_opened, sizeof(b->last_opened),
                        lor->valuestring);
      }
      b->drm = BOOK_DRM_UNKNOWN;

      /* Fusion avec cache local */
      for (int i = 0; i < prev.count; i++) {
        if (strcmp(prev.items[i].product_id, b->product_id) != 0) {
          continue;
        }
        if (prev.items[i].downloaded && prev.items[i].local_path[0]) {
          snprintf(b->local_path, sizeof(b->local_path), "%s",
                   prev.items[i].local_path);
          b->downloaded = prev.items[i].downloaded;
        }
        if (prev.items[i].drm != BOOK_DRM_UNKNOWN) {
          b->drm = prev.items[i].drm;
          snprintf(b->url_format, sizeof(b->url_format), "%s",
                   prev.items[i].url_format);
        }
        /* Garder le plus récent des last_opened */
        if (prev.items[i].last_opened[0] &&
            strcmp(prev.items[i].last_opened, b->last_opened) > 0) {
          snprintf(b->last_opened, sizeof(b->last_opened), "%s",
                   prev.items[i].last_opened);
        }
        break;
      }
      list->count++;
    }
    cJSON_Delete(batch);
    if (!cont || !sync_token[0]) {
      break;
    }
  }
  free(sync_token);
  library_cache_save(list);
  return 0;
}

int kobo_cover_url(const AppConfig *cfg, const BookEntry *book, int w, int h,
                   char *out, size_t out_len) {
  if (!book->cover_id[0] || !out || out_len < 32) {
    return -1;
  }
  const char *tmpl = cfg->image_url_template[0]
                         ? cfg->image_url_template
                         : "https://cdn.kobo.com/book-images/{ImageId}/{Width}/"
                           "{Height}/false/image.jpg";
  char buf[KOBO_SHOP_PATH];
  snprintf(buf, sizeof(buf), "%s", tmpl);
  /* Remplacements simples */
  char *p;
  while ((p = strstr(buf, "{ImageId}")) != NULL) {
    char tmp[KOBO_SHOP_PATH];
    *p = 0;
    snprintf(tmp, sizeof(tmp), "%s%s%s", buf, book->cover_id,
             p + strlen("{ImageId}"));
    snprintf(buf, sizeof(buf), "%s", tmp);
  }
  char ws[16], hs[16];
  snprintf(ws, sizeof(ws), "%d", w);
  snprintf(hs, sizeof(hs), "%d", h);
  while ((p = strstr(buf, "{Width}")) != NULL) {
    char tmp[KOBO_SHOP_PATH];
    *p = 0;
    snprintf(tmp, sizeof(tmp), "%s%s%s", buf, ws, p + strlen("{Width}"));
    snprintf(buf, sizeof(buf), "%s", tmp);
  }
  while ((p = strstr(buf, "{Height}")) != NULL) {
    char tmp[KOBO_SHOP_PATH];
    *p = 0;
    snprintf(tmp, sizeof(tmp), "%s%s%s", buf, hs, p + strlen("{Height}"));
    snprintf(buf, sizeof(buf), "%s", tmp);
  }
  /* Quality / IsGreyscale éventuellement présents */
  while ((p = strstr(buf, "{Quality}")) != NULL) {
    char tmp[KOBO_SHOP_PATH];
    *p = 0;
    snprintf(tmp, sizeof(tmp), "%s80%s", buf, p + strlen("{Quality}"));
    snprintf(buf, sizeof(buf), "%s", tmp);
  }
  while ((p = strstr(buf, "{IsGreyscale}")) != NULL) {
    char tmp[KOBO_SHOP_PATH];
    *p = 0;
    /* false = image couleur (mieux ditherée sur e-ink que le JPEG déjà gris) */
    snprintf(tmp, sizeof(tmp), "%sfalse%s", buf, p + strlen("{IsGreyscale}"));
    snprintf(buf, sizeof(buf), "%s", tmp);
  }
  if (strncmp(buf, "//", 2) == 0) {
    snprintf(out, out_len, "https:%s", buf);
  } else {
    snprintf(out, out_len, "%s", buf);
  }
  return 0;
}

int kobo_ensure_cover(AppConfig *cfg, const BookEntry *book, int w, int h,
                      char *path_out, size_t path_len) {
  if (!book->cover_id[0]) {
    return -1;
  }
  snprintf(path_out, path_len, "%s/%s_%dx%d_c.jpg", app_covers_dir(),
           book->cover_id, w, h);
  if (access(path_out, R_OK) == 0) {
    return 0;
  }
  char url[KOBO_SHOP_PATH];
  if (kobo_cover_url(cfg, book, w, h, url, sizeof(url)) != 0) {
    return -1;
  }
  char err[128];
  if (http_download_file(url, NULL, path_out, 45, err, sizeof(err)) != 0) {
    remove(path_out);
    return -1;
  }
  return 0;
}

static void sanitize_filename(const char *in, char *out, size_t out_len) {
  size_t j = 0;
  for (size_t i = 0; in[i] && j + 1 < out_len; i++) {
    unsigned char c = (unsigned char)in[i];
    /* Apostrophes / quotes cassent aussi certains outils ; ASCII sûr. */
    if (c < 32 || strchr("/\\:*?\"<>|' `&$;", c)) {
      out[j++] = '_';
    } else {
      out[j++] = (char)c;
    }
  }
  out[j] = 0;
  if (j == 0) {
    snprintf(out, out_len, "book");
  }
}

static BookDrmKind classify_drm(const char *drm) {
  if (!drm) {
    return BOOK_DRM_UNKNOWN;
  }
  if (strcmp(drm, "SignedNoDrm") == 0) {
    return BOOK_DRM_SIGNED_NO_DRM;
  }
  if (strcmp(drm, "AdobeDrm") == 0) {
    return BOOK_DRM_ADOBE;
  }
  if (strcmp(drm, "KDRM") == 0) {
    return BOOK_DRM_KDRM;
  }
  if (strcmp(drm, "None") == 0) {
    return BOOK_DRM_NONE_SAMPLE;
  }
  return BOOK_DRM_OTHER;
}

int kobo_book_downloadable(const BookEntry *book) {
  return book->drm == BOOK_DRM_SIGNED_NO_DRM || book->drm == BOOK_DRM_ADOBE;
}

typedef struct {
  BookDrmKind drm;
  char *url; /* malloc, may be NULL */
  char format[32];
} ContentPick;

static void content_pick_free(ContentPick *p) {
  free(p->url);
  p->url = NULL;
}

/* Interroge content_access pour un DisplayProfile. 0 = OK (même sans URL). */
static int content_access_profile(AppConfig *cfg, BookEntry *book,
                                  const char *profile, ContentPick *out,
                                  char *err, size_t err_len) {
  memset(out, 0, sizeof(*out));
  out->drm = BOOK_DRM_UNKNOWN;
  if (!cfg->content_access_url[0]) {
    if (kobo_initialization(cfg, err, err_len) != 0) {
      return -1;
    }
  }
  char url[KOBO_SHOP_PATH];
  const char *tmpl = cfg->content_access_url;
  const char *ph = strstr(tmpl, "{ProductId}");
  if (!ph) {
    snprintf(err, err_len, "bad content_access template");
    return -1;
  }
  snprintf(url, sizeof(url), "%.*s%s%s?DisplayProfile=%s", (int)(ph - tmpl),
           tmpl, book->product_id, ph + strlen("{ProductId}"), profile);

  HttpResponse r;
  if (get_authed(cfg, url, NULL, NULL, &r, err, err_len) != 0) {
    http_response_free(&r);
    return -1;
  }
  cJSON *root = cJSON_Parse(r.body ? r.body : "");
  http_response_free(&r);
  if (!root) {
    snprintf(err, err_len, "content_access bad json");
    return -1;
  }

  cJSON *urls = cJSON_GetObjectItemCaseSensitive(root, "ContentUrls");
  if (!cJSON_IsArray(urls)) {
    urls = cJSON_GetObjectItemCaseSensitive(root, "DownloadUrls");
  }
  const char *best_url = NULL;
  const char *best_fmt = NULL;
  const char *kdrm_url = NULL;
  const char *kdrm_fmt = NULL;
  BookDrmKind prefer = BOOK_DRM_UNKNOWN;
  BookDrmKind any_drm = BOOK_DRM_UNKNOWN;

  if (cJSON_IsArray(urls)) {
    cJSON *u;
    cJSON_ArrayForEach(u, urls) {
      cJSON *drmj = cJSON_GetObjectItemCaseSensitive(u, "DRMType");
      if (!cJSON_IsString(drmj)) {
        drmj = cJSON_GetObjectItemCaseSensitive(u, "DrmType");
      }
      cJSON *fmtj = cJSON_GetObjectItemCaseSensitive(u, "UrlFormat");
      if (!cJSON_IsString(fmtj)) {
        fmtj = cJSON_GetObjectItemCaseSensitive(u, "Format");
      }
      cJSON *du = cJSON_GetObjectItemCaseSensitive(u, "DownloadUrl");
      if (!cJSON_IsString(du)) {
        du = cJSON_GetObjectItemCaseSensitive(u, "Url");
      }
      BookDrmKind kind =
          classify_drm(cJSON_IsString(drmj) ? drmj->valuestring : NULL);
      if (any_drm == BOOK_DRM_UNKNOWN) {
        any_drm = kind;
      }
      if (!cJSON_IsString(du)) {
        continue;
      }
      if (kind == BOOK_DRM_KDRM && !kdrm_url) {
        kdrm_url = du->valuestring;
        kdrm_fmt = cJSON_IsString(fmtj) ? fmtj->valuestring : NULL;
      }
      int rank = 0;
      if (kind == BOOK_DRM_SIGNED_NO_DRM) {
        rank = 3;
      } else if (kind == BOOK_DRM_ADOBE) {
        rank = 2;
      } else if (kind == BOOK_DRM_NONE_SAMPLE) {
        rank = 1;
      }
      int pref_rank =
          (prefer == BOOK_DRM_SIGNED_NO_DRM) ? 3
          : (prefer == BOOK_DRM_ADOBE)       ? 2
          : (prefer == BOOK_DRM_NONE_SAMPLE) ? 1
                                             : 0;
      if (rank > pref_rank) {
        prefer = kind;
        best_url = du->valuestring;
        best_fmt = cJSON_IsString(fmtj) ? fmtj->valuestring : NULL;
      }
    }
  }

  if (prefer == BOOK_DRM_UNKNOWN) {
    prefer = (any_drm != BOOK_DRM_UNKNOWN) ? any_drm : BOOK_DRM_UNKNOWN;
  }
  out->drm = prefer;
  if (best_fmt) {
    snprintf(out->format, sizeof(out->format), "%s", best_fmt);
  }
  if (best_url &&
      (prefer == BOOK_DRM_SIGNED_NO_DRM || prefer == BOOK_DRM_ADOBE)) {
    out->url = strdup(best_url);
    if (!out->url) {
      cJSON_Delete(root);
      snprintf(err, err_len, "oom");
      return -1;
    }
  } else if (prefer == BOOK_DRM_KDRM && kdrm_url) {
    /* Archivage local — fichier chiffré, pas de retrait DRM */
    out->url = strdup(kdrm_url);
    if (kdrm_fmt && !out->format[0]) {
      snprintf(out->format, sizeof(out->format), "%s", kdrm_fmt);
    }
    if (!out->url) {
      cJSON_Delete(root);
      snprintf(err, err_len, "oom");
      return -1;
    }
  }
  cJSON_Delete(root);
  return 0;
}

/*
 * Android = SignedNoDrm / KDRM appareil.
 * Desktop = export Adobe ACSM (comme kobo.com → télécharger).
 * On privilégie free, sinon ACSM Desktop.
 */
static int content_access_pick(AppConfig *cfg, BookEntry *book, char **out_url,
                               char *err, size_t err_len) {
  *out_url = NULL;
  ContentPick android = {0};
  ContentPick desktop = {0};

  if (content_access_profile(cfg, book, "Android", &android, err, err_len) !=
      0) {
    return -1;
  }

  if (android.drm == BOOK_DRM_SIGNED_NO_DRM && android.url) {
    book->drm = BOOK_DRM_SIGNED_NO_DRM;
    if (android.format[0]) {
      snprintf(book->url_format, sizeof(book->url_format), "%s", android.format);
    }
    *out_url = android.url;
    android.url = NULL;
    content_pick_free(&android);
    content_pick_free(&desktop);
    return 0;
  }

  /* Export Adobe (ACSM) — même titres souvent étiquetés KDRM en Android */
  if (content_access_profile(cfg, book, "Desktop", &desktop, err, err_len) !=
      0) {
    content_pick_free(&android);
    return -1;
  }

  if (desktop.drm == BOOK_DRM_ADOBE && desktop.url) {
    book->drm = BOOK_DRM_ADOBE;
    if (desktop.format[0]) {
      snprintf(book->url_format, sizeof(book->url_format), "%s", desktop.format);
    } else {
      snprintf(book->url_format, sizeof(book->url_format), "ACSM");
    }
    *out_url = desktop.url;
    desktop.url = NULL;
    content_pick_free(&android);
    content_pick_free(&desktop);
    return 0;
  }

  if (desktop.drm == BOOK_DRM_SIGNED_NO_DRM && desktop.url) {
    book->drm = BOOK_DRM_SIGNED_NO_DRM;
    if (desktop.format[0]) {
      snprintf(book->url_format, sizeof(book->url_format), "%s", desktop.format);
    }
    *out_url = desktop.url;
    desktop.url = NULL;
    content_pick_free(&android);
    content_pick_free(&desktop);
    return 0;
  }

  /* Pas d'Adobe / free : KDRM Android (fichier chiffré archivable) */
  if (android.drm == BOOK_DRM_NONE_SAMPLE) {
    book->drm = BOOK_DRM_NONE_SAMPLE;
  } else if (android.drm == BOOK_DRM_KDRM || android.drm == BOOK_DRM_OTHER ||
             android.drm == BOOK_DRM_UNKNOWN) {
    book->drm = BOOK_DRM_KDRM;
    if (android.url) {
      *out_url = android.url;
      android.url = NULL;
    }
  } else {
    book->drm = android.drm;
  }
  if (android.format[0]) {
    snprintf(book->url_format, sizeof(book->url_format), "%s", android.format);
  }
  content_pick_free(&android);
  content_pick_free(&desktop);
  return 0;
}

static int file_contains(const char *path, const char *needle) {
  FILE *fp = fopen(path, "rb");
  if (!fp) {
    return 0;
  }
  char buf[4096];
  size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
  fclose(fp);
  buf[n] = 0;
  return strstr(buf, needle) != NULL;
}

int kobo_probe_book(AppConfig *cfg, BookEntry *book, char *err, size_t err_len) {
  char *url = NULL;
  int rc = content_access_pick(cfg, book, &url, err, err_len);
  free(url);
  return rc;
}

int kobo_download_book(AppConfig *cfg, BookEntry *book, char *err, size_t err_len) {
  char *best_url = NULL;
  if (content_access_pick(cfg, book, &best_url, err, err_len) != 0) {
    return -1;
  }

  /* KDRM : archiver l'EPUB chiffré dans kobo_shop/kdrm/ (pas de retrait DRM) */
  if (book->drm == BOOK_DRM_KDRM && best_url) {
    char fname[KOBO_SHOP_STR];
    sanitize_filename(book->title, fname, sizeof(fname));
    char path[KOBO_SHOP_PATH];
    snprintf(path, sizeof(path), "%s/%s.epub", app_kdrm_dir(), fname);
    char dlerr[256];
    if (http_download_file(best_url, cfg->access_token, path, 300, dlerr,
                           sizeof(dlerr)) != 0) {
      free(best_url);
      snprintf(err, err_len, "KDRM download: %s", dlerr);
      return -1;
    }
    free(best_url);
    /*
     * TODO: trouver une solution pour les KDRM — fichier archivé ici mais
     * non lisible sur PocketBook (chiffrement appareil). Pistes hors retrait
     * DRM : export ACSM via site (kobo_shop/acsm/), éventuel client web
     * automatisé pour mint WebDownloadToken, ou autre interop officielle.
     */
    snprintf(book->local_path, sizeof(book->local_path), "%s", path);
    book->downloaded = 0; /* non ouvrable par le lecteur PB */
    snprintf(err, err_len,
             "EPUB KDRM enregistré (chiffré) :\n%s\n\n"
             "Non lisible sur PocketBook.\n"
             "Pour lire : ACSM depuis kobo.com → kobo_shop/acsm/ "
             "→ Menu Importer ACSM.",
             path);
    return 1; /* succès partiel : fichier sauvé, message informatif */
  }

  if (!best_url || !kobo_book_downloadable(book)) {
    free(best_url);
    if (book->drm == BOOK_DRM_NONE_SAMPLE) {
      snprintf(err, err_len, "Échantillon uniquement — pas le livre complet.");
    } else {
      snprintf(err, err_len,
               "Pas d'ACSM via l'API pour ce titre.\n"
               "1) kobo.com → télécharger (Adobe) → .acsm\n"
               "2) USB → dossier kobo_shop/acsm/\n"
               "3) Menu → Importer ACSM");
    }
    return -1;
  }

  char fname[KOBO_SHOP_STR];
  sanitize_filename(book->title, fname, sizeof(fname));
  char path[KOBO_SHOP_PATH];
  int is_acsm = (book->drm == BOOK_DRM_ADOBE &&
                 (strstr(best_url, ".acsm") || strstr(best_url, "acsm") ||
                  strstr(best_url, "acs4.kobo.com")));
  const char *ext = is_acsm ? ".acsm" : ".epub";
  snprintf(path, sizeof(path), "%s/%s%s", app_books_dir(), fname, ext);

  char dlerr[256];
  /* ACSM : pas de Bearer — libcurl ne doit pas réécrire la query (auth GBLink). */
  const char *bearer = is_acsm ? NULL : cfg->access_token;
  if (http_download_file(best_url, bearer, path, 300, dlerr, sizeof(dlerr)) !=
      0) {
    free(best_url);
    snprintf(err, err_len, "download: %s", dlerr);
    return -1;
  }
  free(best_url);

  if (is_acsm && !file_contains(path, "fulfillmentToken")) {
    int auth_err = file_contains(path, "E_URLLINK");
    remove(path);
    snprintf(err, err_len,
             auth_err ? "ACSM rejeté (E_URLLINK). Réessaie dans un instant."
                      : "ACSM invalide. Réessaie ou exporte depuis kobo.com.");
    return -1;
  }

  snprintf(book->local_path, sizeof(book->local_path), "%s", path);
  book->downloaded = 1;
  return 0;
}