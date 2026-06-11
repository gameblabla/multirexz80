/*
 * MultiRexZ80
 *
 * Multi-system Z80 emulator based on SMS Plus GX by Eke-Eke, itself based on
 * SMS Plus by Charles MacDonald.
 *
 * Default project license: GPL-2.0-or-later.  File-specific notices below
 * are retained and take precedence for imported or derived components,
 * including MAME-derived code and other third-party modules.
 */

#ifndef FILEIO_H_
#define FILEIO_H_

#include <stdint.h>

/* Function prototypes */
uint8_t *loadFromZipByName(char *archive, char *filename, uint32_t *filesize);
int32_t loadZipMemberExact(const char *archive, const char *name, uint8_t *dst, uint32_t expected_size, uint32_t expected_crc);
int32_t zip_member_exists_in_archive(const char *archive, const char *name);
int32_t check_zip(const char *filename);
//int gzsize(gzFile *gd);

#endif /* FILEIO_H_ */
