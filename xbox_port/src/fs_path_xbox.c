/*
 * fs_path_xbox.c - give the Xbox a working directory: the xbe's own folder.
 *
 * nxdk has no current directory. CreateFileA opens every name relative to the
 * \?? DOS-devices object directory, so "gamedata/lang/ru.lang" (and anything
 * with a forward slash) can never open. The PC port's newer features all read
 * that way -- language packs, text overrides, voice files, mod lists -- and so
 * did older ones, which is why Loose_FOpen and the config path each grew their
 * own Q: rewrite. This TU fixes it once, underneath stdio:
 *
 *   - _PDCLIB_open is pdclib's own implementation (platform/xbox) with one
 *     added step: a relative path is resolved against Q:\ (the xbe directory
 *     cd_xbox.c mounts; D:\ when that mount failed) and '/' becomes '\'. Being
 *     an object on the link line it is taken before the libpdclib.lib member.
 *   - opendir/readdir/closedir (dirent.h shim) resolve the same way.
 *
 * Absolute paths ("D:\...", "\Device\...") pass through untouched apart from
 * the slash conversion, so every existing caller behaves exactly as before.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <windows.h>
#include "pdclib/_PDCLIB_glue.h"
#include "dirent.h"

int _PDCLIB_w32errno(DWORD werror);
extern int Cd_XboxMountHome(void);   /* cd_xbox.c: mounts Q: = the xbe directory */

/* Resolve `in` into `out`. Returns out, or NULL when it does not fit. */
static const char* XboxFs_Resolve(const char* in, char* out, size_t cap)
{
    size_t d = 0;
    const char* s = in;

    if (in == NULL)
        return NULL;

    if (!(((in[0] >= 'A' && in[0] <= 'Z') || (in[0] >= 'a' && in[0] <= 'z')) && in[1] == ':') &&
        in[0] != '\\' && in[0] != '/')
    {
        const char* home = Cd_XboxMountHome() ? "Q:\\" : "D:\\";
        while (s[0] == '.' && (s[1] == '/' || s[1] == '\\'))
            s += 2;
        while (*home && d + 1 < cap)
            out[d++] = *home++;
    }
    for (; *s != '\0'; s++)
    {
        if (d + 1 >= cap)
            return NULL;
        out[d++] = (*s == '/') ? '\\' : *s;
    }
    out[d] = '\0';
    return out;
}

_PDCLIB_fd_t _PDCLIB_open(const char* const filename, unsigned int mode)
{
    int    access_flags = 0;
    int    create_flags = 0;
    char   path[MAX_PATH];
    HANDLE handle;

    switch (mode & (_PDCLIB_FREAD | _PDCLIB_FWRITE | _PDCLIB_FAPPEND | _PDCLIB_FRW))
    {
        case _PDCLIB_FREAD: /* "r" */
            access_flags = GENERIC_READ;
            create_flags = OPEN_EXISTING;
            break;
        case _PDCLIB_FWRITE: /* "w" */
            access_flags = GENERIC_WRITE;
            create_flags = CREATE_ALWAYS;
            break;
        case _PDCLIB_FWRITE | _PDCLIB_FAPPEND: /* "a" */
            access_flags = GENERIC_WRITE;
            create_flags = OPEN_ALWAYS;
            break;
        case _PDCLIB_FREAD | _PDCLIB_FRW: /* "r+" */
            access_flags = GENERIC_READ | GENERIC_WRITE;
            create_flags = OPEN_EXISTING;
            break;
        case _PDCLIB_FWRITE | _PDCLIB_FRW: /* "w+" */
            access_flags = GENERIC_READ | GENERIC_WRITE;
            create_flags = CREATE_ALWAYS | TRUNCATE_EXISTING;
            break;
        case _PDCLIB_FAPPEND | _PDCLIB_FRW: /* "a+" */
            access_flags = GENERIC_READ | GENERIC_WRITE;
            create_flags = OPEN_ALWAYS;
            break;
        default: /* Invalid mode */
            return INVALID_HANDLE_VALUE;
    }

    if (XboxFs_Resolve(filename, path, sizeof(path)) == NULL)
    {
        *_PDCLIB_errno_func() = _PDCLIB_ENAMETOOLONG;
        return INVALID_HANDLE_VALUE;
    }

    handle = CreateFileA(path, access_flags, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL,
                         create_flags, FILE_ATTRIBUTE_NORMAL, NULL);

    if (handle != INVALID_HANDLE_VALUE)
    {
        if (mode & _PDCLIB_FAPPEND)
        {
            LARGE_INTEGER new_pos;
            BOOL          ok;
            new_pos.QuadPart = 0;
            ok = SetFilePointerEx(handle, new_pos, &new_pos, FILE_END);
            if (!ok)
            {
                *_PDCLIB_errno_func() = _PDCLIB_w32errno(GetLastError());
                CloseHandle(handle);
                return INVALID_HANDLE_VALUE;
            }
        }
        return handle;
    }

    *_PDCLIB_errno_func() = _PDCLIB_w32errno(GetLastError());
    return INVALID_HANDLE_VALUE;
}

struct XboxDir
{
    HANDLE           find;
    int              pending;   /* `data` holds an entry readdir has not returned yet */
    WIN32_FIND_DATAA data;
    struct dirent    ent;
};

DIR* opendir(const char* name)
{
    char path[MAX_PATH];
    size_t n;
    DIR* d;

    if (XboxFs_Resolve(name, path, sizeof(path) - 3) == NULL)
        return NULL;
    n = strlen(path);
    if (n > 0 && path[n - 1] != '\\')
        path[n++] = '\\';
    path[n++] = '*';
    path[n]   = '\0';

    d = (DIR*)calloc(1, sizeof(DIR));
    if (d == NULL)
        return NULL;
    d->find = FindFirstFileA(path, &d->data);
    if (d->find == INVALID_HANDLE_VALUE)
    {
        free(d);
        return NULL;
    }
    d->pending = 1;
    return d;
}

struct dirent* readdir(DIR* d)
{
    if (d == NULL)
        return NULL;
    if (!d->pending && !FindNextFileA(d->find, &d->data))
        return NULL;
    d->pending = 0;
    strncpy(d->ent.d_name, d->data.cFileName, sizeof(d->ent.d_name) - 1);
    d->ent.d_name[sizeof(d->ent.d_name) - 1] = '\0';
    d->ent.d_type = (d->data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? DT_DIR : DT_REG;
    return &d->ent;
}

int closedir(DIR* d)
{
    if (d == NULL)
        return -1;
    if (d->find != INVALID_HANDLE_VALUE)
        FindClose(d->find);
    free(d);
    return 0;
}
