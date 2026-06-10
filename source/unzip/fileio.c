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

/*
    fileio.c --
    File management.
*/
#include "shared.h"
#include "unzip.h"

static const char *zip_path_ext(const char *path)
{
    const char *dot = path ? strrchr(path, '.') : NULL;
    return dot ? dot : "";
}

static int zip_ext_equal(const char *ext, const char *wanted)
{
    if (!ext || !wanted) return 0;
    while (*ext && *wanted)
    {
        int a = *ext++;
        int b = *wanted++;
        if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
        if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
        if (a != b) return 0;
    }
    return *ext == '\0' && *wanted == '\0';
}

static int zip_member_supported_rom_ext(const char *name)
{
    const char *ext = zip_path_ext(name);
    return zip_ext_equal(ext, ".sms") || zip_ext_equal(ext, ".gg") ||
           zip_ext_equal(ext, ".sg")  || zip_ext_equal(ext, ".sc") ||
           zip_ext_equal(ext, ".sf")  || zip_ext_equal(ext, ".col") ||
           zip_ext_equal(ext, ".cv")  || zip_ext_equal(ext, ".m5") ||
           zip_ext_equal(ext, ".rom") || zip_ext_equal(ext, ".bin");
}

static void zip_apply_member_console_hint(const char *name)
{
    const char *ext = zip_path_ext(name);

    if (option.console != 0)
        return;

    if (zip_ext_equal(ext, ".col") || zip_ext_equal(ext, ".cv"))
        option.console = 6;
    else if (zip_ext_equal(ext, ".gg"))
        option.console = 3;
    else if (zip_ext_equal(ext, ".sg"))
        option.console = 5;
    else if (zip_ext_equal(ext, ".m5"))
        option.console = 7;
}

uint8_t *loadFromZipByName(char *archive, char *filename, uint32_t *filesize)
{
    char name[PATH_MAX];
    uint8_t *buffer;

    int32_t zerror = UNZ_OK;
    unzFile zhandle;
    unz_file_info zinfo;
    int found_supported_rom = 0;
    
    zinfo.uncompressed_size = 0;

    zhandle = unzOpen(archive);
    if(!zhandle) return (NULL);

    /* Prefer the first supported ROM member instead of blindly loading the
     * first archive entry.  This makes plain .zip cartridge loading usable
     * when the archive starts with a README, directory entry, or artwork. */
    zerror = unzGoToFirstFile(zhandle);
    while(zerror == UNZ_OK)
    {
        memset(name, 0, sizeof(name));
        unzGetCurrentFileInfo(zhandle, &zinfo, &name[0], sizeof(name) - 1, NULL, 0, NULL, 0);
        if (zinfo.uncompressed_size != 0 && zip_member_supported_rom_ext(name))
        {
            found_supported_rom = 1;
            break;
        }
        zerror = unzGoToNextFile(zhandle);
    }

    if (!found_supported_rom)
    {
        unzClose(zhandle);
        return (NULL);
    }

    zip_apply_member_console_hint(name);
    
    *filesize = zinfo.uncompressed_size;

    /* Error: file size is zero */
    if(*filesize == 0)
    {
        unzClose(zhandle);
        return (NULL);
    }

    /* Open current file */
    zerror = unzOpenCurrentFile(zhandle);
    if(zerror != UNZ_OK)
    {
        unzClose(zhandle);
        return (NULL);
    }

    /* Allocate buffer and read in file */
    buffer = malloc(*filesize);
    if(!buffer)
    {
        unzCloseCurrentFile(zhandle);
        unzClose(zhandle);
        return (NULL);
    }
    zerror = unzReadCurrentFile(zhandle, buffer, *filesize);

    /* Internal error: free buffer and close file */
    if(zerror < 0 || zerror != (int32_t)*filesize)
    {
        free(buffer);
        buffer = NULL;
        unzCloseCurrentFile(zhandle);
        unzClose(zhandle);
        return (NULL);
    }

    /* Close current file and archive file */
    unzCloseCurrentFile(zhandle);
    unzClose(zhandle);

    memcpy(filename, name, PATH_MAX);
    return (buffer);
}

/*
    Verifies if a file is a ZIP archive or not.
    Returns: 1= ZIP archive, 0= not a ZIP archive
*/
int32_t check_zip(const char *filename)
{
    uint8_t buf[2];
    FILE* fd = NULL;
    fd = fopen(filename, "rb");
    if(!fd) return (0);
    
    fread(buf, 2, 1, fd);
    fclose(fd);
    if(memcmp(buf, "PK", 2) == 0) return (1);
    return (0);
}
