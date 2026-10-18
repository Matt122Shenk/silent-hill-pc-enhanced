/*
 * psycross_cxx_stubs.cpp - PsyCross globals that PsyX_GTE.cpp declares WITHOUT
 * extern "C", so they carry C++ linkage and cannot be defined from a C file.
 * On PC they live in PsyX_GPU.cpp, which the Xbox does not build.
 */

/* PGXPDBG console bitmask (PC diagnostic: isolate the precise-vertex source).
 * Always 0 on Xbox. */
int g_PsxPgxpDbg = 0;
