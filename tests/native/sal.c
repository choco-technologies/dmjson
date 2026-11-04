#include "dmod.h"
#include "sal.h"
#include <stdint.h>
#include <stdlib.h>

long native_fail_after = -1;
size_t native_live_blocks, native_live_bytes;
size_t native_read_chunk = SIZE_MAX, native_write_chunk = SIZE_MAX;
int native_read_fail;
/* Heap bookkeeping with malloc's alignment retained. */
typedef union { max_align_t alignment; size_t size; } allocation_t;

void *Dmod_Malloc(size_t size)
{
    allocation_t *a;
    if (native_fail_after == 0) return NULL;
    if (native_fail_after > 0) --native_fail_after;
    if (size > SIZE_MAX - sizeof(*a)) return NULL;
    a = malloc(sizeof(*a) + size);
    if (!a) return NULL;
    a->size = size;
    ++native_live_blocks;
    native_live_bytes += size;
    return a + 1;
}

void Dmod_Free(void *ptr)
{
    allocation_t *a;
    if (!ptr) return;
    a = (allocation_t*)ptr - 1;
    --native_live_blocks;
    native_live_bytes -= a->size;
    free(a);
}

void *Dmod_FileOpen(const char *path, const char *mode) { return fopen(path, mode); }
void Dmod_FileClose(void *file) { fclose(file); }
int Dmod_FileRemove(const char *path) { return remove(path); }
size_t Dmod_FileRead(void *buffer, size_t size, size_t count, void *file)
{
    if (native_read_fail) return 0;
    if (count > native_read_chunk) count = native_read_chunk;
    return fread(buffer, size, count, file);
}
size_t Dmod_FileWrite(const void *buffer, size_t size, size_t count, void *file)
{
    if (count > native_write_chunk) count = native_write_chunk;
    return fwrite(buffer, size, count, file);
}
Dmod_FileSize_t Dmod_FileSize(void *file)
{
    long size;
    if (fseek(file, 0, SEEK_END)) return UINT64_MAX;
    size = ftell(file);
    if (size < 0 || fseek(file, 0, SEEK_SET)) return UINT64_MAX;
    return (Dmod_FileSize_t)size;
}
