/*
 * Compatibility wrapper.
 *
 * The original project carried an empty source/basetsd.h, which is harmless on
 * non-Windows targets but shadows MinGW-w64's system <basetsd.h> when building
 * the native Win32 frontend with -Isource.  Windows headers require the real
 * MinGW basetsd definitions for LONG_PTR, DWORD_PTR, SIZE_T, etc.
 */
#ifndef MULTIREXZ80_COMPAT_BASETSD_H
#define MULTIREXZ80_COMPAT_BASETSD_H

#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
# if defined(__GNUC__)
#  include_next <basetsd.h>
# else
#  include <../include/basetsd.h>
# endif
#endif

#endif /* MULTIREXZ80_COMPAT_BASETSD_H */
