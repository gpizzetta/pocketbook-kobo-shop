#ifndef KOBO_SHOP_HTTP_H
#define KOBO_SHOP_HTTP_H

#include <stddef.h> /* size_t */

typedef struct {
  char *body;
  size_t body_len;
  long status;
  char *header_sync;      /* x-kobo-sync (owned) */
  char *header_synctoken; /* x-kobo-synctoken (owned) */
  char err[256];
} HttpResponse;

void http_global_init(void);
void http_global_cleanup(void);

void http_response_free(HttpResponse *r);

/* GET/POST with optional Bearer token and extra request header. */
int http_request(const char *method, const char *url, const char *bearer,
                 const char *extra_header_name, const char *extra_header_value,
                 const char *json_body, int timeout_sec, HttpResponse *out);

int http_download_file(const char *url, const char *bearer, const char *path,
                       int timeout_sec, char *err, size_t err_len);

#endif
