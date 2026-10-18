/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * psx_memory.c - PSX memory layout emulation for PC port
 *
 * The PSX has 2MB of main RAM at 0x80000000-0x801FFFFF.
 * Game code uses hardcoded addresses throughout (buffer pointers, etc).
 *
 * We allocate a full 2MB block. PSX_ADDR() in psx_memory.h maps
 * an address offset (0x00000000-0x001FFFFF) into this buffer.
 */
#include <stdint.h>
#include <string.h>
#include <stdio.h>

/* Full PSX RAM emulation - 2MB + 1MB overflow guard.
 * Some file reads target buffers near the end of PSX RAM (e.g. FS_BUFFER_16
 * at 0x1EBE00) and the loaded file may exceed the remaining space.
 * On PSX this wraps around; on PC it would corrupt adjacent memory.
 * Extra 1MB prevents stack/heap corruption from these overflows. */
#ifdef SH_XBOX_PORT
/* The guard is protecting against a real overflow, not padding, but 1 MB of it
 * is a lot of a 64 MB console's free RAM. The known overrun is a file read into
 * FS_BUFFER_16 (0x1EBE00, 82 KB short of the 2 MB boundary); 256 KB covers
 * three times that gap (the N64 port runs on 128 KB). */
#define PSX_RAM_SIZE (2 * 1024 * 1024 + 256 * 1024)
#else
#define PSX_RAM_SIZE (3 * 1024 * 1024)
#endif
uint8_t g_PsxRam[PSX_RAM_SIZE];

/* PSX Scratchpad RAM emulation.
 * Real PSX has 1KB; the PC port widens s_GteScratchData/s_GteScratchData2 to
 * ~2964 bytes (256-slot pools for oversized models) -- 4KB holds it, and the
 * bodyprog.h _Static_asserts pin sizeof <= 4096. */
uint8_t g_PsxScratchpad[4096];

void PsxMemory_Init(void)
{
    memset(g_PsxRam, 0, sizeof(g_PsxRam));
    memset(g_PsxScratchpad, 0, sizeof(g_PsxScratchpad));
    printf("[PSX_MEM] Initialized %d KB RAM emulation at %p\n",
           PSX_RAM_SIZE / 1024, (void*)g_PsxRam);
}
