#include "sqlite3.h"

#include <pspiofilemgr.h>
#include <pspkernel.h>
#include <pspthreadman.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifndef PSP_CHSTAT_SIZE
#define PSP_CHSTAT_SIZE 0x04
#endif

#define PSP_SQLITE_MAX_PATH 512
#define PSP_SQLITE_MODE 0777

typedef struct PspSqliteFile {
    sqlite3_file base;
    SceUID fd;
    int sqlite_flags;
    int lock_level;
    int delete_on_close;
    char path[PSP_SQLITE_MAX_PATH];
} PspSqliteFile;

static int psp_sqlite_close(sqlite3_file *file)
{
    PspSqliteFile *psp = (PspSqliteFile *)file;
    int result = SQLITE_OK;

    if (psp->fd >= 0 && sceIoClose(psp->fd) < 0)
        result = SQLITE_IOERR_CLOSE;
    psp->fd = -1;

    if (psp->delete_on_close && psp->path[0] != '\0' &&
        sceIoRemove(psp->path) < 0) {
        result = SQLITE_IOERR_DELETE;
    }

    return result;
}

static int psp_sqlite_read(sqlite3_file *file, void *buffer, int amount,
                           sqlite3_int64 offset)
{
    PspSqliteFile *psp = (PspSqliteFile *)file;
    int got;

    if (sceIoLseek(psp->fd, (SceOff)offset, PSP_SEEK_SET) != (SceOff)offset)
        return SQLITE_IOERR_SEEK;

    got = sceIoRead(psp->fd, buffer, (SceSize)amount);
    if (got < 0)
        return SQLITE_IOERR_READ;
    if (got < amount) {
        memset((unsigned char *)buffer + got, 0, (size_t)(amount - got));
        return SQLITE_IOERR_SHORT_READ;
    }
    return SQLITE_OK;
}

static int psp_sqlite_write(sqlite3_file *file, const void *buffer, int amount,
                            sqlite3_int64 offset)
{
    PspSqliteFile *psp = (PspSqliteFile *)file;
    const unsigned char *bytes = (const unsigned char *)buffer;
    int written = 0;

    if (sceIoLseek(psp->fd, (SceOff)offset, PSP_SEEK_SET) != (SceOff)offset)
        return SQLITE_IOERR_SEEK;

    while (written < amount) {
        int chunk = sceIoWrite(psp->fd, bytes + written,
                               (SceSize)(amount - written));
        if (chunk <= 0)
            return SQLITE_IOERR_WRITE;
        written += chunk;
    }
    return SQLITE_OK;
}

static int psp_sqlite_truncate(sqlite3_file *file, sqlite3_int64 size)
{
    PspSqliteFile *psp = (PspSqliteFile *)file;
    SceIoStat stat;

    memset(&stat, 0, sizeof(stat));
    stat.st_size = (SceOff)size;
    return sceIoChstat(psp->path, &stat, PSP_CHSTAT_SIZE) < 0
        ? SQLITE_IOERR_TRUNCATE : SQLITE_OK;
}

static int psp_sqlite_sync(sqlite3_file *file, int flags)
{
    PspSqliteFile *psp = (PspSqliteFile *)file;
    const char *colon;
    char device[16];
    size_t length;

    (void)flags;
    colon = strchr(psp->path, ':');
    if (colon == NULL)
        return SQLITE_OK;

    length = (size_t)(colon - psp->path) + 1u;
    if (length >= sizeof(device))
        return SQLITE_IOERR_FSYNC;

    memcpy(device, psp->path, length);
    device[length] = '\0';
    return sceIoSync(device, 0) < 0 ? SQLITE_IOERR_FSYNC : SQLITE_OK;
}

static int psp_sqlite_file_size(sqlite3_file *file, sqlite3_int64 *size)
{
    PspSqliteFile *psp = (PspSqliteFile *)file;
    SceOff current;
    SceOff end;

    current = sceIoLseek(psp->fd, 0, PSP_SEEK_CUR);
    if (current < 0)
        return SQLITE_IOERR_FSTAT;
    end = sceIoLseek(psp->fd, 0, PSP_SEEK_END);
    if (end < 0)
        return SQLITE_IOERR_FSTAT;
    if (sceIoLseek(psp->fd, current, PSP_SEEK_SET) != current)
        return SQLITE_IOERR_SEEK;

    *size = (sqlite3_int64)end;
    return SQLITE_OK;
}

static int psp_sqlite_lock(sqlite3_file *file, int lock)
{
    ((PspSqliteFile *)file)->lock_level = lock;
    return SQLITE_OK;
}

static int psp_sqlite_unlock(sqlite3_file *file, int lock)
{
    ((PspSqliteFile *)file)->lock_level = lock;
    return SQLITE_OK;
}

static int psp_sqlite_check_reserved(sqlite3_file *file, int *out)
{
    PspSqliteFile *psp = (PspSqliteFile *)file;
    *out = psp->lock_level >= SQLITE_LOCK_RESERVED;
    return SQLITE_OK;
}

static int psp_sqlite_file_control(sqlite3_file *file, int op, void *arg)
{
    if (op == SQLITE_FCNTL_LOCKSTATE) {
        *(int *)arg = ((PspSqliteFile *)file)->lock_level;
        return SQLITE_OK;
    }
    return SQLITE_NOTFOUND;
}

static int psp_sqlite_sector_size(sqlite3_file *file)
{
    (void)file;
    return 512;
}

static int psp_sqlite_device_characteristics(sqlite3_file *file)
{
    (void)file;
    return 0;
}

static const sqlite3_io_methods g_psp_io_methods = {
    .iVersion = 1,
    .xClose = psp_sqlite_close,
    .xRead = psp_sqlite_read,
    .xWrite = psp_sqlite_write,
    .xTruncate = psp_sqlite_truncate,
    .xSync = psp_sqlite_sync,
    .xFileSize = psp_sqlite_file_size,
    .xLock = psp_sqlite_lock,
    .xUnlock = psp_sqlite_unlock,
    .xCheckReservedLock = psp_sqlite_check_reserved,
    .xFileControl = psp_sqlite_file_control,
    .xSectorSize = psp_sqlite_sector_size,
    .xDeviceCharacteristics = psp_sqlite_device_characteristics
};

static void psp_sqlite_temp_name(char *path, size_t path_size)
{
    static unsigned int serial;
    uint64_t tick = (uint64_t)sceKernelGetSystemTimeWide();

    ++serial;
    snprintf(path, path_size, "ms0:/sqlite-%08X-%08X.tmp",
             (unsigned int)tick, serial);
}

static int psp_sqlite_open(sqlite3_vfs *vfs, const char *name,
                           sqlite3_file *file, int flags, int *out_flags)
{
    PspSqliteFile *psp = (PspSqliteFile *)file;
    int open_flags;

    (void)vfs;
    memset(psp, 0, sizeof(*psp));
    psp->fd = -1;

    if (name == NULL) {
        psp_sqlite_temp_name(psp->path, sizeof(psp->path));
    } else {
        size_t length = strlen(name);
        if (length >= sizeof(psp->path))
            return SQLITE_CANTOPEN;
        memcpy(psp->path, name, length + 1u);
    }

    open_flags = (flags & SQLITE_OPEN_READWRITE) ? PSP_O_RDWR : PSP_O_RDONLY;
    if (flags & SQLITE_OPEN_CREATE)
        open_flags |= PSP_O_CREAT;
    if (flags & SQLITE_OPEN_EXCLUSIVE)
        open_flags |= PSP_O_EXCL;

    psp->fd = sceIoOpen(psp->path, open_flags, PSP_SQLITE_MODE);
    if (psp->fd < 0)
        return SQLITE_CANTOPEN;

    psp->base.pMethods = &g_psp_io_methods;
    psp->sqlite_flags = flags;
    psp->lock_level = SQLITE_LOCK_NONE;
    psp->delete_on_close = (flags & SQLITE_OPEN_DELETEONCLOSE) != 0;

    if (out_flags != NULL) {
        *out_flags = (flags & SQLITE_OPEN_READWRITE)
            ? SQLITE_OPEN_READWRITE : SQLITE_OPEN_READONLY;
    }
    return SQLITE_OK;
}

static int psp_sqlite_delete(sqlite3_vfs *vfs, const char *name, int sync_dir)
{
    SceIoStat stat;

    (void)vfs;
    (void)sync_dir;
    if (sceIoRemove(name) >= 0)
        return SQLITE_OK;

    memset(&stat, 0, sizeof(stat));
    return sceIoGetstat(name, &stat) < 0 ? SQLITE_OK : SQLITE_IOERR_DELETE;
}

static int psp_sqlite_access(sqlite3_vfs *vfs, const char *name,
                             int flags, int *out)
{
    SceIoStat stat;

    (void)vfs;
    (void)flags;
    memset(&stat, 0, sizeof(stat));
    *out = sceIoGetstat(name, &stat) >= 0;
    return SQLITE_OK;
}

static int psp_sqlite_full_pathname(sqlite3_vfs *vfs, const char *name,
                                    int size, char *out)
{
    size_t length;

    (void)vfs;
    if (name == NULL || size <= 0)
        return SQLITE_CANTOPEN;

    length = strlen(name);
    if (length >= (size_t)size)
        return SQLITE_CANTOPEN;
    memcpy(out, name, length + 1u);
    return SQLITE_OK;
}

static int psp_sqlite_randomness(sqlite3_vfs *vfs, int size, char *out)
{
    uint64_t state;
    int i;

    (void)vfs;
    state = (uint64_t)sceKernelGetSystemTimeWide() ^
            (uint64_t)(uintptr_t)out ^ UINT64_C(0x9E3779B97F4A7C15);
    for (i = 0; i < size; ++i) {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        out[i] = (char)state;
    }
    return size;
}

static int psp_sqlite_sleep(sqlite3_vfs *vfs, int microseconds)
{
    (void)vfs;
    if (microseconds > 0)
        sceKernelDelayThread((SceUInt)microseconds);
    return microseconds;
}

static int psp_sqlite_current_time(sqlite3_vfs *vfs, double *julian_day)
{
    time_t now;

    (void)vfs;
    sceKernelLibcTime(&now);
    *julian_day = (double)now / 86400.0 + 2440587.5;
    return SQLITE_OK;
}

static int psp_sqlite_last_error(sqlite3_vfs *vfs, int size, char *out)
{
    (void)vfs;
    if (out != NULL && size > 0)
        out[0] = '\0';
    return 0;
}

static sqlite3_vfs g_psp_vfs = {
    .iVersion = 1,
    .szOsFile = (int)sizeof(PspSqliteFile),
    .mxPathname = PSP_SQLITE_MAX_PATH,
    .zName = "psp",
    .xOpen = psp_sqlite_open,
    .xDelete = psp_sqlite_delete,
    .xAccess = psp_sqlite_access,
    .xFullPathname = psp_sqlite_full_pathname,
    .xRandomness = psp_sqlite_randomness,
    .xSleep = psp_sqlite_sleep,
    .xCurrentTime = psp_sqlite_current_time,
    .xGetLastError = psp_sqlite_last_error
};

int sqlite3_os_init(void)
{
    return sqlite3_vfs_register(&g_psp_vfs, 1);
}

int sqlite3_os_end(void)
{
    sqlite3_vfs_unregister(&g_psp_vfs);
    return SQLITE_OK;
}
