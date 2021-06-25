/********************************************************************
 * Description: interp_i18n.cc
 *
 *  message internationalization support
 *
 * Author: Sascha Ittner
 * License: GPL Version 2
 * System: Linux
 *
 * Copyright (c) 2015 All rights reserved.
 *
 ********************************************************************/

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "rs274ngc.hh"
#include "rs274ngc_interp.hh"

#include <stdio.h>
#include <stdlib.h>
#include <pcre.h>
#include <string.h>

void Interp::i18n_init() {
  const char *errmsg;
  int errpos;

  i18n_cleanup();

  const char *lang = getenv("LANG");
  if (lang != NULL) {
    char *p;
    char lpart[PATH_MAX+1];
    strncpy(lpart, lang, sizeof(lpart));
    lpart[sizeof(lpart) - 1] = 0;
    if ((p = strchr(lpart, '.')) != NULL) *p = 0;
    if ((p = strchr(lpart, '_')) != NULL) *p = 0;

    char fname[PATH_MAX+1];
    snprintf(fname, sizeof(fname), "lang.%s", lpart);
    fname[sizeof(lpart) - 1] = 0;

    FILE *fp = find_file(&_setup, fname);
    if (fp != NULL) {
      pcre *re = pcre_compile("(?:[^\\s\"]+|\\\"[^\"]*\\\")", 0, &errmsg, &errpos, 0);
      if (re != NULL) {
        char *line = NULL;
        size_t len = 0;
        ssize_t read;
        while ((read = getline(&line, &len, fp)) != -1) {
          int pos = 0;
          char *from, *to;
          if (!(from = i18n_map_token(re, line, read, &pos))) {
            continue;
          }
          if (!(to = i18n_map_token(re, line, read, &pos))) {
            continue;
          }

          i18n_map[std::string(from)] = std::string(to);
        }
        free(line);
        free(re);
      }
      fclose(fp);
    }
  }

  i18n_re = pcre_compile("_{[^}]*}", 0, &errmsg, &errpos, 0);
}

void Interp::i18n_cleanup() {
  free(i18n_re);
  i18n_re = NULL;
  i18n_map.clear();
}

char *Interp::i18n_map_token(const pcre *code, char *subject, int length, int *offset) {
  int vect[3];
  char *start;
  char *end;

  if (*offset >= length) {
    return NULL;
  }

  if (pcre_exec(code, NULL, subject, length, *offset, 0, vect, 3) <= 0) {
    return NULL;
  }

  start = subject + vect[0];
  end = subject + vect[1];

  while (end > start && *(end - 1) == '"') end--;
  *end = 0;
  while (*start == '"') start++;

  *offset = vect[1] + 1;
  return start;
}

int Interp::i18n_translate(const char *from, char *to, int to_len) {
  int from_len = strlen(from);
  int offset = 0;
  int vect[3];
  std::string res;

  while (*from == ' ') from++;

  if (i18n_re == NULL) {
    strncpy(to, from, to_len);
    to[to_len - 1] = 0;
    return from_len;
  }

  while (offset < from_len && pcre_exec(i18n_re, NULL, from, from_len, offset, 0, vect, 3) > 0) {
    if (vect[0] > offset) {
      res.append(from + offset, vect[0] - offset);
    }

    std::string tok(from + vect[0] + 2, vect[1] - vect[0] - 3);
    if (i18n_map.find(tok) != i18n_map.end()) {
      res.append(i18n_map[tok]);
    } else {
      if (tok.c_str()[0] != '_') {
        res.append(tok);
      }
    }

    offset = vect[1];
  }

  if (offset < from_len) {
    res.append(from + offset, from_len - offset);
  }

  strncpy(to, res.c_str(), to_len);
  to[to_len - 1] = 0;

  return res.length();
}

