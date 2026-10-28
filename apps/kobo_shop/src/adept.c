#include "adept.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static char g_tools[KOBO_SHOP_PATH];

void adept_set_tools_dir(const char *dir) {
  if (dir && dir[0]) {
    snprintf(g_tools, sizeof(g_tools), "%s", dir);
  } else {
    g_tools[0] = 0;
  }
}

static int find_tool(const char *name, char *out, size_t out_len) {
  char a[KOBO_SHOP_PATH], b[KOBO_SHOP_PATH], c[KOBO_SHOP_PATH];
  char d[KOBO_SHOP_PATH], e[KOBO_SHOP_PATH], f[KOBO_SHOP_PATH];
  /* Bundle recommandé : applications/kobo_shop/<outil> */
  snprintf(a, sizeof(a), "%s/%s", g_tools[0] ? g_tools : ".", name);
  snprintf(b, sizeof(b), "/mnt/ext1/applications/kobo_shop/%s", name);
  snprintf(c, sizeof(c), "%s/bin/%s", app_data_dir(), name);
  snprintf(d, sizeof(d), "./%s", name);
  /* Ancien déploiement à plat (compat) */
  snprintf(e, sizeof(e), "/mnt/ext1/applications/%s", name);
  snprintf(f, sizeof(f), "/mnt/ext1/system/bin/%s", name);
  const char *cands[] = {a, b, c, d, e, f, name, NULL};
  for (int i = 0; cands[i]; i++) {
    if (!cands[i][0]) {
      continue;
    }
    /* VFAT: X_OK échoue souvent — R_OK suffit si le fichier existe. */
    if (access(cands[i], R_OK) == 0) {
      snprintf(out, out_len, "%s", cands[i]);
      return 0;
    }
  }
  return -1;
}

int adept_tools_available(void) {
  char p[KOBO_SHOP_PATH];
  return find_tool("adept_activate", p, sizeof(p)) == 0 &&
         find_tool("acsmdownloader", p, sizeof(p)) == 0;
}

static void read_log_tail(const char *path, char *dst, size_t dst_len) {
  dst[0] = 0;
  if (!dst_len) {
    return;
  }
  FILE *fp = fopen(path, "rb");
  if (!fp) {
    return;
  }
  if (fseek(fp, 0, SEEK_END) != 0) {
    fclose(fp);
    return;
  }
  long sz = ftell(fp);
  if (sz < 0) {
    fclose(fp);
    return;
  }
  long keep = (long)(dst_len > 1 ? dst_len - 1 : 0);
  if (keep > 400) {
    keep = 400;
  }
  long start = (sz > keep) ? (sz - keep) : 0;
  if (fseek(fp, start, SEEK_SET) != 0) {
    fclose(fp);
    return;
  }
  size_t n = fread(dst, 1, (size_t)keep, fp);
  fclose(fp);
  dst[n] = 0;
  /* compact newlines for DialogSynchro */
  for (size_t i = 0; i < n; i++) {
    if (dst[i] == '\n' || dst[i] == '\r') {
      dst[i] = ' ';
    }
  }
}

/* fork/exec — pas de shell (évite casser les chemins avec apostrophes). */
static int run_argv(char *const argv[], char *err, size_t err_len) {
  char logpath[KOBO_SHOP_PATH];
  snprintf(logpath, sizeof(logpath), "%s/adept_last.log", app_data_dir());
  unlink(logpath);

  pid_t pid = fork();
  if (pid < 0) {
    snprintf(err, err_len, "fork: %s", strerror(errno));
    return -1;
  }
  if (pid == 0) {
    int fd = open(logpath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
      dup2(fd, STDOUT_FILENO);
      dup2(fd, STDERR_FILENO);
      if (fd > 2) {
        close(fd);
      }
    }
    /* Libs ADEPT dans le bundle applications/kobo_shop/adept_libs */
    char libs[KOBO_SHOP_PATH * 3];
    const char *base =
        g_tools[0] ? g_tools : "/mnt/ext1/applications/kobo_shop";
    snprintf(libs, sizeof(libs),
             "%s/adept_libs:/mnt/ext1/applications/kobo_shop/adept_libs:"
             "/mnt/ext1/applications/adept_libs",
             base);
    setenv("LD_LIBRARY_PATH", libs, 1);
    setenv("ADEPT_DIR", app_adept_dir(), 1);
    /* Certificats HTTPS pour libcurl ADEPT */
    char bundled_ca[KOBO_SHOP_PATH];
    snprintf(bundled_ca, sizeof(bundled_ca), "%s/ca-bundle.crt", base);
    const char *certs[] = {
        bundled_ca,
        "/mnt/ext1/applications/kobo_shop/ca-bundle.crt",
        "/mnt/ext1/applications/ca-bundle.crt",
        "/etc/ssl/certs/ca-certificates.crt",
        "/etc/ssl/cert.pem",
        NULL,
    };
    for (int i = 0; certs[i]; i++) {
      if (access(certs[i], R_OK) == 0) {
        setenv("SSL_CERT_FILE", certs[i], 1);
        setenv("CURL_CA_BUNDLE", certs[i], 1);
        break;
      }
    }
    execv(argv[0], argv);
    /* exec failed */
    dprintf(STDERR_FILENO, "exec %s: %s\n", argv[0], strerror(errno));
    _exit(127);
  }

  int status = 0;
  if (waitpid(pid, &status, 0) < 0) {
    snprintf(err, err_len, "waitpid: %s", strerror(errno));
    return -1;
  }

  char tail[420];
  read_log_tail(logpath, tail, sizeof(tail));

  if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
    return 0;
  }

  int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  if (tail[0]) {
    snprintf(err, err_len, "ADEPT exit %d: %s", code, tail);
  } else {
    snprintf(err, err_len, "ADEPT exit %d (voir adept_last.log)", code);
  }
  return -1;
}

int adept_activate(AppConfig *cfg, char *err, size_t err_len) {
  if (!config_bytebooks_ready(cfg)) {
    snprintf(err, err_len, "Compte ByteBooks manquant");
    return -1;
  }
  char tool[KOBO_SHOP_PATH];
  if (find_tool("adept_activate", tool, sizeof(tool)) != 0) {
    snprintf(err, err_len,
             "adept_activate introuvable. Copie le binaire ARM dans "
             "applications/ (voir README).");
    return -1;
  }

  config_ensure_dirs();
  char tmp[KOBO_SHOP_PATH];
  snprintf(tmp, sizeof(tmp), "%s/adept.new.%d", app_data_dir(), (int)getpid());
  char rmcmd[KOBO_SHOP_PATH + 64];
  snprintf(rmcmd, sizeof(rmcmd), "rm -rf '%s'", tmp);
  system(rmcmd);

  char *argv[] = {tool,
                  (char *)"--random-serial",
                  (char *)"-u",
                  cfg->bytebooks_email,
                  (char *)"-p",
                  cfg->bytebooks_password,
                  (char *)"--output-dir",
                  tmp,
                  NULL};
  if (run_argv(argv, err, err_len) != 0) {
    snprintf(rmcmd, sizeof(rmcmd), "rm -rf '%s'", tmp);
    system(rmcmd);
    return -1;
  }

  char actfile[KOBO_SHOP_PATH];
  snprintf(actfile, sizeof(actfile), "%s/activation.xml", tmp);
  if (access(actfile, R_OK) != 0) {
    snprintf(err, err_len, "activation.xml non créé");
    snprintf(rmcmd, sizeof(rmcmd), "rm -rf '%s'", tmp);
    system(rmcmd);
    return -1;
  }

  snprintf(rmcmd, sizeof(rmcmd), "rm -rf '%s' && mv '%s' '%s'", app_adept_dir(),
           tmp, app_adept_dir());
  if (system(rmcmd) != 0) {
    snprintf(err, err_len, "impossible d'installer le dossier ADEPT");
    return -1;
  }
  cfg->adept_activated = 1;
  config_save(cfg);
  return 0;
}

int adept_fulfill_acsm(const char *acsm_path, const char *out_epub, char *err,
                       size_t err_len) {
  char tool[KOBO_SHOP_PATH];
  if (find_tool("acsmdownloader", tool, sizeof(tool)) != 0) {
    snprintf(err, err_len, "acsmdownloader introuvable (binaire ADEPT ARM)");
    return -1;
  }
  char act[KOBO_SHOP_PATH];
  snprintf(act, sizeof(act), "%s/activation.xml", app_adept_dir());
  if (access(act, R_OK) != 0) {
    snprintf(err, err_len, "ADEPT non activé — configure ByteBooks d'abord");
    return -1;
  }
  if (access(acsm_path, R_OK) != 0) {
    snprintf(err, err_len, "ACSM introuvable");
    return -1;
  }

  /* -o refuse si le fichier existe déjà */
  unlink(out_epub);

  char *argv[] = {tool,
                  (char *)"--adept-directory",
                  (char *)app_adept_dir(),
                  (char *)"-o",
                  (char *)out_epub,
                  (char *)acsm_path,
                  NULL};
  if (run_argv(argv, err, err_len) != 0) {
    return -1;
  }
  if (access(out_epub, R_OK) != 0) {
    snprintf(err, err_len, "EPUB non produit");
    return -1;
  }
  return 0;
}

int adept_fulfill_book(BookEntry *book, char *err, size_t err_len) {
  if (!book->local_path[0]) {
    snprintf(err, err_len, "pas de fichier local");
    return -1;
  }
  size_t n = strlen(book->local_path);
  if (n < 5 || strcasecmp(book->local_path + n - 5, ".acsm") != 0) {
    snprintf(err, err_len, "pas un ACSM");
    return -1;
  }
  char out[KOBO_SHOP_PATH];
  snprintf(out, sizeof(out), "%.*s.epub", (int)(n - 5), book->local_path);
  if (adept_fulfill_acsm(book->local_path, out, err, err_len) != 0) {
    return -1;
  }
  /* Ne plus pointer vers l'ACSM (le lecteur PB ne l'ouvre pas correctement). */
  unlink(book->local_path);
  snprintf(book->local_path, sizeof(book->local_path), "%s", out);
  book->downloaded = 1;
  book->drm = BOOK_DRM_ADOBE;
  return 0;
}

static void norm_title(const char *in, char *out, size_t out_len) {
  size_t j = 0;
  for (size_t i = 0; in[i] && j + 1 < out_len; i++) {
    unsigned char c = (unsigned char)in[i];
    if (isalnum(c)) {
      out[j++] = (char)tolower(c);
    } else if (c == ' ' || c == '-' || c == '\'' || c == ':') {
      if (j > 0 && out[j - 1] != ' ') {
        out[j++] = ' ';
      }
    }
  }
  while (j > 0 && out[j - 1] == ' ') {
    j--;
  }
  out[j] = 0;
}

static int titles_match(const char *a, const char *b) {
  char na[KOBO_SHOP_STR], nb[KOBO_SHOP_STR];
  norm_title(a, na, sizeof(na));
  norm_title(b, nb, sizeof(nb));
  if (!na[0] || !nb[0]) {
    return 0;
  }
  if (strcmp(na, nb) == 0) {
    return 1;
  }
  /* préfixe assez long (titres tronqués côté fichier) */
  size_t la = strlen(na), lb = strlen(nb);
  size_t m = la < lb ? la : lb;
  if (m >= 12 && strncmp(na, nb, m) == 0) {
    return 1;
  }
  if (strstr(na, nb) || strstr(nb, na)) {
    return m >= 10;
  }
  return 0;
}

static int acsm_read_dc_title(const char *path, char *title, size_t title_len) {
  title[0] = 0;
  FILE *fp = fopen(path, "rb");
  if (!fp) {
    return -1;
  }
  char buf[8192];
  size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
  fclose(fp);
  buf[n] = 0;
  const char *p = strstr(buf, "dc:title");
  if (!p) {
    p = strstr(buf, ":title");
  }
  if (!p) {
    return -1;
  }
  p = strchr(p, '>');
  if (!p) {
    return -1;
  }
  p++;
  const char *end = strstr(p, "</");
  if (!end || end <= p) {
    return -1;
  }
  size_t len = (size_t)(end - p);
  if (len >= title_len) {
    len = title_len - 1;
  }
  memcpy(title, p, len);
  title[len] = 0;
  /* trim */
  while (title[0] == ' ' || title[0] == '\n' || title[0] == '\r') {
    memmove(title, title + 1, strlen(title));
  }
  return title[0] ? 0 : -1;
}

static BookEntry *find_book_for_title(BookList *list, const char *title) {
  if (!list || !title || !title[0]) {
    return NULL;
  }
  BookEntry *best = NULL;
  for (int i = 0; i < list->count; i++) {
    BookEntry *b = &list->items[i];
    if (!titles_match(b->title, title)) {
      continue;
    }
    /* préférer un titre pas encore téléchargé */
    if (!b->downloaded) {
      return b;
    }
    if (!best) {
      best = b;
    }
  }
  return best;
}

static void attach_epub(BookEntry *b, const char *epub_path) {
  snprintf(b->local_path, sizeof(b->local_path), "%s", epub_path);
  b->downloaded = 1;
  b->drm = BOOK_DRM_ADOBE;
  snprintf(b->url_format, sizeof(b->url_format), "EPUB");
}

static void sanitize_fname(const char *in, char *out, size_t out_len) {
  size_t j = 0;
  for (size_t i = 0; in[i] && j + 1 < out_len; i++) {
    unsigned char c = (unsigned char)in[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || (c >= 128)) {
      out[j++] = (char)c;
    } else if (c == ' ') {
      out[j++] = '_';
    }
  }
  if (j == 0) {
    snprintf(out, out_len, "livre");
  } else {
    out[j] = 0;
  }
}

int adept_import_books_folder(BookList *list, int *ok, int *fail, char *err,
                              size_t err_len) {
  if (ok) {
    *ok = 0;
  }
  if (fail) {
    *fail = 0;
  }
  if (!adept_tools_available()) {
    snprintf(err, err_len, "acsmdownloader introuvable");
    return -1;
  }
  char act[KOBO_SHOP_PATH];
  snprintf(act, sizeof(act), "%s/activation.xml", app_adept_dir());
  if (access(act, R_OK) != 0) {
    snprintf(err, err_len, "Active ByteBooks (menu Configuration) d'abord.");
    return -1;
  }

  const char *drop = app_acsm_drop_dir();
  const char *books = app_books_dir();
  DIR *d = opendir(drop);
  if (!d) {
    snprintf(err, err_len, "Dossier inaccessible: %s", drop);
    return -1;
  }

  struct dirent *ent;
  char path[KOBO_SHOP_PATH];
  char title[KOBO_SHOP_STR];
  char one_err[256];
  char fname[KOBO_SHOP_STR];

  while ((ent = readdir(d)) != NULL) {
    const char *name = ent->d_name;
    size_t nl = strlen(name);
    if (nl < 6 || strcasecmp(name + nl - 5, ".acsm") != 0) {
      continue;
    }
    snprintf(path, sizeof(path), "%s/%s", drop, name);

    if (acsm_read_dc_title(path, title, sizeof(title)) != 0) {
      snprintf(title, sizeof(title), "%.*s", (int)(nl - 5), name);
    }
    BookEntry *b = find_book_for_title(list, title);
    if (b && b->title[0]) {
      sanitize_fname(b->title, fname, sizeof(fname));
    } else {
      sanitize_fname(title, fname, sizeof(fname));
    }

    char out[KOBO_SHOP_PATH];
    snprintf(out, sizeof(out), "%s/%s.epub", books, fname);
    if (adept_fulfill_acsm(path, out, one_err, sizeof(one_err)) != 0) {
      if (fail) {
        (*fail)++;
      }
      continue;
    }
    /* ACSM traité → retiré du dépôt */
    unlink(path);
    if (b) {
      attach_epub(b, out);
    }
    if (ok) {
      (*ok)++;
    }
  }
  closedir(d);

  /* Rattache aussi des EPUB déjà dans Books/Kobo non liés */
  d = opendir(books);
  if (d && list) {
    while ((ent = readdir(d)) != NULL) {
      const char *name = ent->d_name;
      size_t nl = strlen(name);
      if (nl < 6 || strcasecmp(name + nl - 5, ".epub") != 0) {
        continue;
      }
      snprintf(path, sizeof(path), "%s/%s", books, name);
      snprintf(title, sizeof(title), "%.*s", (int)(nl - 5), name);
      BookEntry *b = find_book_for_title(list, title);
      if (b && !b->downloaded) {
        attach_epub(b, path);
        if (ok) {
          (*ok)++;
        }
      }
    }
    closedir(d);
  }

  err[0] = 0;
  return 0;
}
