#ifndef NATIVE_DMOD_H
#define NATIVE_DMOD_H
#include <stddef.h>
#include <stdio.h>
#include "dmod_types.h"
void *Dmod_Malloc(size_t size);
void Dmod_Free(void *ptr);
void *Dmod_FileOpen(const char *path, const char *mode);
size_t Dmod_FileRead(void *buffer, size_t size, size_t count, void *file);
size_t Dmod_FileWrite(const void *buffer, size_t size, size_t count, void *file);
Dmod_FileSize_t Dmod_FileSize(void *file);
void Dmod_FileClose(void *file);
int Dmod_FileRemove(const char *path);
#define Dmod_Printf printf
#endif
