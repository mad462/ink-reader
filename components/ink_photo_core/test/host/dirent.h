#pragma once

#include <stddef.h>

typedef struct host_dir DIR;
struct dirent {
  char d_name[260];
};

static inline DIR *opendir(const char *path) {
  (void)path;
  return NULL;
}

static inline struct dirent *readdir(DIR *dir) {
  (void)dir;
  return NULL;
}

static inline int closedir(DIR *dir) {
  (void)dir;
  return 0;
}
