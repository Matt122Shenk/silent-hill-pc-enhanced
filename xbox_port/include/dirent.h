/*
 * dirent.h - Xbox shim: the POSIX directory scan the PC port's language packs,
 * text overrides and mod registry use (opendir/readdir/closedir + d_name).
 * Backed by FindFirstFile in fs_path_xbox.c, which also resolves the relative
 * "gamedata/..." paths those callers pass against the xbe's own directory.
 */
#ifndef SH_XBOX_DIRENT_H
#define SH_XBOX_DIRENT_H

#ifdef __cplusplus
extern "C" {
#endif

struct dirent {
    unsigned char d_type;
    char          d_name[260];
};

#define DT_UNKNOWN 0
#define DT_DIR     4
#define DT_REG     8

typedef struct XboxDir DIR;

DIR*           opendir(const char* name);
struct dirent* readdir(DIR* dir);
int            closedir(DIR* dir);

#ifdef __cplusplus
}
#endif

#endif /* SH_XBOX_DIRENT_H */
