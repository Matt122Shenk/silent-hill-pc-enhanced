/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * ios_fonts.m — where iOS keeps the CJK faces the quick menu borrows.
 *
 * No font the port ships has a kana or a hanzi, and a CJK face is far too
 * large to bundle for an overlay, so lang_quick.c reads one off the system.
 * Every other platform can be a list of paths; iOS cannot. Its font files are
 * not at the macOS paths (they sit under /System/Library/Fonts in subfolders
 * that have moved between releases, and the Japanese face is not always
 * present at all), so a hardcoded list on this platform means the quick menu
 * falls back to English for Japanese and Chinese.
 *
 * CoreText already knows. A face is asked for by its PostScript name and its
 * file comes back as a URL -- which is both correct across releases and the
 * only way to find a face iOS downloaded on demand.
 *
 * The caller still probes the file for a glyph of the script it wants, so a
 * name that CoreText substituted something else for is caught there as well as
 * by the identity check here.
 *
 * This target is built WITHOUT ARC, so the CF<->NS casts here are plain ones
 * and every Copy/Create below is released by hand.
 */
#import <CoreText/CoreText.h>
#import <Foundation/Foundation.h>
#include <string.h>

/* LANG_QUICK_SCRIPT_JAPANESE / _CHINESE, which this file cannot include:
 * lang_quick.h is C that pulls in the port's game headers. */
enum { IOS_CJK_JAPANESE = 1, IOS_CJK_CHINESE = 2 };

/* Hiragino Sans is the Japanese system face and PingFang the Chinese one. Each
 * list ends with the other script's face: both are full CJK fonts, so either
 * can draw the other's text if only one is installed. */
static NSArray<NSString*>* FaceNames(int script)
{
    if (script == IOS_CJK_CHINESE)
    {
        return @[ @"PingFangSC-Regular", @"PingFangTC-Regular", @"STHeitiSC-Light",
                  @"HiraginoSans-W3" ];
    }
    return @[ @"HiraginoSans-W3", @"HiraginoSans-W4", @"HiraMaruProN-W4",
              @"PingFangSC-Regular" ];
}

/* The file holding a named face, or NULL. CoreText substitutes a default when
 * it does not have the name, so the name that comes back has to be the one
 * that went in -- otherwise every lookup would "succeed" with the system UI
 * font, which has no CJK glyphs of its own (it cascades to one, and a cascade
 * is not a file stb_truetype can read). */
static NSString* PathOfFace(NSString* name)
{
    CTFontRef font = CTFontCreateWithName((CFStringRef)name, 12.0, NULL);
    NSString*  path = nil;

    if (font == NULL)
    {
        return nil;
    }

    CFStringRef got = CTFontCopyPostScriptName(font);
    if (got != NULL)
    {
        if ([(NSString*)got isEqualToString:name])
        {
            CFURLRef url = (CFURLRef)CTFontCopyAttribute(font, kCTFontURLAttribute);

            if (url != NULL)
            {
                /* The string belongs to the URL, and the URL may belong only
                 * to the font -- both are released below, so under this
                 * target's manual retain/release it has to be made to outlive
                 * them rather than handed back as a borrowed reference. */
                path = [[[(NSURL*)url path] retain] autorelease];
                CFRelease(url);
            }
        }
        CFRelease(got);
    }
    CFRelease(font);

    return path;
}

/* The faces iOS has, most preferred first, as NUL-terminated paths for C.
 * Resolved once per script and kept: the strings outlive the pool, and
 * lang_quick.c holds on to whichever one it settles on for the whole run. */
const char* const* Ios_CjkFontPaths(int script)
{
    /* Four names per script plus the terminator, for the two scripts. */
    static const char* s_paths[3][5];
    static int         s_resolved[3];

    if (script != IOS_CJK_JAPANESE && script != IOS_CJK_CHINESE)
    {
        return NULL;
    }
    if (s_resolved[script])
    {
        return (const char* const*)s_paths[script];
    }
    s_resolved[script] = 1;

    @autoreleasepool
    {
        int n = 0;

        for (NSString* name in FaceNames(script))
        {
            NSString* path = PathOfFace(name);
            const char* utf8 = (path != nil) ? [path fileSystemRepresentation] : NULL;

            if (utf8 == NULL || n >= 4)
            {
                continue;
            }
            s_paths[script][n] = strdup(utf8);
            if (s_paths[script][n] != NULL)
            {
                n++;
            }
        }
        NSLog(@"[SH] CJK faces for script %d: %d found", script, n);
    }

    return (const char* const*)s_paths[script];
}
