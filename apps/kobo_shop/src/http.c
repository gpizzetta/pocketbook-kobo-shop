#include "http.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef struct {
  char *data;
  size_t len;
} Buf;

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
  Buf *b = (Buf *)userdata;
  size_t n = size * nmemb;
  char *p = (char *)realloc(b->data, b->len + n + 1);
  if (!p) {
    return 0;
  }
  b->data = p;
  memcpy(b->data + b->len, ptr, n);
  b->len += n;
  b->data[b->len] = '\0';
  return n;
}

/* x-kobo-synctoken peut dépasser 4 Ko (JWT imbriqué) */
#define KOBO_SYNC_TOKEN_MAX 16384

typedef struct {
  char sync[64];
  char synctoken[KOBO_SYNC_TOKEN_MAX];
} HdrCatch;

static void copy_header_value(char *dst, size_t dst_len, const char *v,
                              size_t avail) {
  /* retire CRLF final */
  while (avail > 0 && (v[avail - 1] == '\r' || v[avail - 1] == '\n' ||
                       v[avail - 1] == ' ')) {
    avail--;
  }
  if (avail >= dst_len) {
    avail = dst_len - 1;
  }
  memcpy(dst, v, avail);
  dst[avail] = 0;
}

static size_t header_cb(char *buffer, size_t size, size_t nitems, void *userdata) {
  HdrCatch *h = (HdrCatch *)userdata;
  size_t n = size * nitems;
  if (n > 12 && strncasecmp(buffer, "x-kobo-sync:", 12) == 0) {
    const char *v = buffer + 12;
    size_t rem = n - 12;
    while (rem > 0 && *v == ' ') {
      v++;
      rem--;
    }
    copy_header_value(h->sync, sizeof(h->sync), v, rem);
  } else if (n > 17 && strncasecmp(buffer, "x-kobo-synctoken:", 17) == 0) {
    const char *v = buffer + 17;
    size_t rem = n - 17;
    while (rem > 0 && *v == ' ') {
      v++;
      rem--;
    }
    copy_header_value(h->synctoken, sizeof(h->synctoken), v, rem);
  }
  return n;
}

void http_global_init(void) { curl_global_init(CURL_GLOBAL_DEFAULT); }
void http_global_cleanup(void) { curl_global_cleanup(); }

void http_response_free(HttpResponse *r) {
  if (!r) {
    return;
  }
  free(r->body);
  free(r->header_sync);
  free(r->header_synctoken);
  memset(r, 0, sizeof(*r));
}

int http_request(const char *method, const char *url, const char *bearer,
                 const char *extra_header_name, const char *extra_header_value,
                 const char *json_body, int timeout_sec, HttpResponse *out) {
  memset(out, 0, sizeof(*out));
  CURL *curl = curl_easy_init();
  if (!curl) {
    snprintf(out->err, sizeof(out->err), "curl_easy_init failed");
    return -1;
  }

  Buf body = {0};
  HdrCatch hdr = {{0}, {0}};
  struct curl_slist *headers = NULL;
  headers = curl_slist_append(headers, "Accept: application/json");
  headers = curl_slist_append(
      headers, "User-Agent: Mozilla/5.0 (Linux; U; Android 2.0; en-us;) "
               "AppleWebKit/538.1 (KHTML, like Gecko) Version/4.0 Mobile "
               "Safari/538.1 (Kobo Touch 0373/4.38.23171)");

  char auth[2200];
  if (bearer && bearer[0]) {
    snprintf(auth, sizeof(auth), "Authorization: Bearer %s", bearer);
    headers = curl_slist_append(headers, auth);
  }
  if (json_body) {
    headers = curl_slist_append(headers, "Content-Type: application/json");
  }
  if (extra_header_name && extra_header_value && extra_header_name[0]) {
    size_t need =
        strlen(extra_header_name) + 2 + strlen(extra_header_value) + 1;
    char *line = (char *)malloc(need);
    if (line) {
      snprintf(line, need, "%s: %s", extra_header_name, extra_header_value);
      headers = curl_slist_append(headers, line);
      free(line);
    }
  }

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_cb);
  curl_easy_setopt(curl, CURLOPT_HEADERDATA, &hdr);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)timeout_sec);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

  if (method && strcmp(method, "POST") == 0) {
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_body ? json_body : "");
  }

  CURLcode rc = curl_easy_perform(curl);
  if (rc != CURLE_OK) {
    snprintf(out->err, sizeof(out->err), "%s", curl_easy_strerror(rc));
    free(body.data);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return -1;
  }

  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &out->status);
  out->body = body.data;
  out->body_len = body.len;
  if (hdr.sync[0]) {
    out->header_sync = strdup(hdr.sync);
  }
  if (hdr.synctoken[0]) {
    out->header_synctoken = strdup(hdr.synctoken);
  }

  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);
  return 0;
}

int http_download_file(const char *url, const char *bearer, const char *path,
                       int timeout_sec, char *err, size_t err_len) {
  FILE *fp = fopen(path, "wb");
  if (!fp) {
    snprintf(err, err_len, "cannot open %s", path);
    return -1;
  }
  CURL *curl = curl_easy_init();
  if (!curl) {
    fclose(fp);
    snprintf(err, err_len, "curl init failed");
    return -1;
  }
  struct curl_slist *headers = NULL;
  char auth[2200];
  if (bearer && bearer[0]) {
    snprintf(auth, sizeof(auth), "Authorization: Bearer %s", bearer);
    headers = curl_slist_append(headers, auth);
  }
  headers = curl_slist_append(
      headers, "User-Agent: Mozilla/5.0 (Linux; U; Android 2.0; en-us;) "
               "AppleWebKit/538.1 (KHTML, like Gecko) Version/4.0 Mobile "
               "Safari/538.1 (Kobo Touch 0373/4.38.23171)");

  /* PATH_AS_IS : ne pas normaliser l'URL — les GBLink ACSM cassent si
   * `+` / %xx de la query sont réécrits (E_URLLINK_AUTH). */
  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_PATH_AS_IS, 1L);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)timeout_sec);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  /* Pas de FAILONERROR : ACSM renvoie parfois 200 + XML d'erreur. */
  curl_easy_setopt(curl, CURLOPT_FAILONERROR, 0L);

  CURLcode rc = curl_easy_perform(curl);
  long status = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);
  fclose(fp);
  if (rc != CURLE_OK || status < 200 || status >= 300) {
    snprintf(err, err_len, "%s (HTTP %ld)",
             rc != CURLE_OK ? curl_easy_strerror(rc) : "HTTP error", status);
    remove(path);
    return -1;
  }
  return 0;
}
