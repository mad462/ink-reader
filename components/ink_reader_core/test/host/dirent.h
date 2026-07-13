#pragma once

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

struct dirent {
  char d_name[MAX_PATH];
};

typedef struct {
  HANDLE handle;
  WIN32_FIND_DATAA data;
  int first;
  struct dirent entry;
} DIR;

static DIR *opendir(const char *path) {
  char pattern[MAX_PATH];
  DIR *dir = (DIR *)calloc(1, sizeof(*dir));
  if (!dir) return NULL;
  if (snprintf(pattern, sizeof(pattern), "%s/*", path) >= sizeof(pattern)) {
    free(dir);
    errno = ENAMETOOLONG;
    return NULL;
  }
  dir->handle = FindFirstFileA(pattern, &dir->data);
  if (dir->handle == INVALID_HANDLE_VALUE) {
    const DWORD error = GetLastError();
    free(dir);
    errno = error == ERROR_PATH_NOT_FOUND || error == ERROR_FILE_NOT_FOUND
                ? ENOENT
                : EIO;
    return NULL;
  }
  dir->first = 1;
  return dir;
}

static struct dirent *readdir(DIR *dir) {
  if (!dir) return NULL;
  if (!dir->first && !FindNextFileA(dir->handle, &dir->data)) return NULL;
  dir->first = 0;
  snprintf(dir->entry.d_name, sizeof(dir->entry.d_name), "%s",
           dir->data.cFileName);
  return &dir->entry;
}

static int closedir(DIR *dir) {
  if (!dir) return -1;
  FindClose(dir->handle);
  free(dir);
  return 0;
}
