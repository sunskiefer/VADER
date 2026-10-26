/* VADER desktop: what the engine's POSIX calls need on Windows (mingw-w64). Force-included for the engine sources
 * (src/*.c) only, so src/ itself stays the same file as in the Force build. */
#pragma once
#ifdef _WIN32
/* every system header the engine uses comes first (their include guards then keep the macros below out of their own
 * declarations of access, mkdir, ...) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <direct.h>
#include <io.h>
#include <unistd.h>
#include <dirent.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <windows.h>
#define lstat(p, st) stat((p), (st))
#define mkdir(p, mode) _mkdir(p)
#ifndef W_OK
#define W_OK 2
#endif
#ifndef R_OK
#define R_OK 4
#endif
#ifndef F_OK
#define F_OK 0
#endif
#define access(p, m) _access((p), (m))
#define usleep(us) Sleep((DWORD)(((us) + 999) / 1000))
#ifndef S_ISLNK
#define S_ISLNK(m) 0
#endif
#endif
