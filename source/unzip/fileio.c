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
#include "miniz.h"

#if defined(_WIN32) || defined(WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#ifndef NOMINMAX
#define NOMINMAX 1
#endif
#include <windows.h>
#endif

static const char *zip_path_ext(const char *path)
{
    const char *dot = path ? strrchr(path, '.') : NULL;
    return dot ? dot : "";
}


static const char *zip_member_basename(const char *name)
{
    const char *slash = name ? strrchr(name, '/') : NULL;
    const char *backslash = name ? strrchr(name, '\\') : NULL;
    const char *base = name ? name : "";
    if (slash && slash + 1 > base) base = slash + 1;
    if (backslash && backslash + 1 > base) base = backslash + 1;
    return base;
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


static uint16_t zip_rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t zip_rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

typedef struct zip_entry_ref_s
{
    const uint8_t *archive;
    size_t archive_size;
    const uint8_t *name;
    uint16_t name_len;
    uint16_t flags;
    uint16_t method;
    uint32_t crc;
    uint32_t compressed_size;
    uint32_t uncompressed_size;
    uint32_t local_header_offset;
} zip_entry_ref_t;

#if defined(_WIN32) || defined(WIN32)
static int zip_read_whole_file_win32(const char *path, uint8_t **data, size_t *size)
{
    HANDLE h;
    LARGE_INTEGER file_size;
    uint8_t *buf;
    size_t total;
    size_t done = 0;

    if (data) *data = NULL;
    if (size) *size = 0;
    if (!path || !data || !size)
        return 0;

    h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    if (!GetFileSizeEx(h, &file_size) || file_size.QuadPart <= 0 ||
        (uint64_t)file_size.QuadPart > (uint64_t)SIZE_MAX)
    {
        CloseHandle(h);
        return 0;
    }

    total = (size_t)file_size.QuadPart;
    buf = (uint8_t *)malloc(total);
    if (!buf)
    {
        CloseHandle(h);
        return 0;
    }

    while (done < total)
    {
        DWORD want = (DWORD)((total - done) > 0x100000u ? 0x100000u : (total - done));
        DWORD got = 0;
        if (!ReadFile(h, buf + done, want, &got, NULL) || got == 0)
        {
            free(buf);
            CloseHandle(h);
            return 0;
        }
        done += (size_t)got;
    }

    CloseHandle(h);
    *data = buf;
    *size = total;
    return 1;
}
#endif

static int zip_read_whole_file(const char *path, uint8_t **data, size_t *size)
{
#if defined(_WIN32) || defined(WIN32)
    return zip_read_whole_file_win32(path, data, size);
#else
    FILE *fp;
    long sz;
    uint8_t *buf;

    if (data) *data = NULL;
    if (size) *size = 0;
    if (!path || !data || !size)
        return 0;

    fp = fopen(path, "rb");
    if (!fp)
        return 0;
    if (fseek(fp, 0, SEEK_END) != 0)
    {
        fclose(fp);
        return 0;
    }
    sz = ftell(fp);
    if (sz <= 0)
    {
        fclose(fp);
        return 0;
    }
    if (fseek(fp, 0, SEEK_SET) != 0)
    {
        fclose(fp);
        return 0;
    }

    buf = (uint8_t *)malloc((size_t)sz);
    if (!buf)
    {
        fclose(fp);
        return 0;
    }
    if (fread(buf, 1, (size_t)sz, fp) != (size_t)sz)
    {
        free(buf);
        fclose(fp);
        return 0;
    }
    fclose(fp);
    *data = buf;
    *size = (size_t)sz;
    return 1;
#endif
}

static int zip_find_eocd(const uint8_t *data, size_t size, size_t *eocd_offset)
{
    size_t start;
    size_t pos;

    if (eocd_offset) *eocd_offset = 0;
    if (!data || size < 22)
        return 0;

    start = (size > (size_t)22 + 65535u) ? size - ((size_t)22 + 65535u) : 0;
    pos = size - 22;
    for (;;)
    {
        if (zip_rd32(data + pos) == 0x06054b50u)
        {
            uint16_t comment_len = zip_rd16(data + pos + 20);
            if (pos + 22u + comment_len == size)
            {
                if (eocd_offset) *eocd_offset = pos;
                return 1;
            }
        }
        if (pos == start)
            break;
        pos--;
    }
    return 0;
}

static int zip_copy_entry_name(const zip_entry_ref_t *entry, char *out, size_t out_size)
{
    size_t n;
    if (!entry || !out || !out_size)
        return 0;
    n = entry->name_len;
    if (n >= out_size)
        n = out_size - 1;
    memcpy(out, entry->name, n);
    out[n] = '\0';
    return 1;
}

static int zip_ascii_tolower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

static int zip_name_ieq(const char *a, const uint8_t *b, uint16_t b_len)
{
    size_t i;
    if (!a || !b)
        return 0;
    for (i = 0; a[i] && i < b_len; i++)
    {
        if (zip_ascii_tolower((unsigned char)a[i]) != zip_ascii_tolower((unsigned char)b[i]))
            return 0;
    }
    return a[i] == '\0' && i == b_len;
}

static const uint8_t *zip_entry_basename_ptr(const uint8_t *name, uint16_t name_len, uint16_t *base_len)
{
    uint16_t i;
    uint16_t base = 0;
    if (!name)
    {
        if (base_len) *base_len = 0;
        return name;
    }
    for (i = 0; i < name_len; i++)
    {
        if (name[i] == '/' || name[i] == '\\')
            base = (uint16_t)(i + 1);
    }
    if (base_len) *base_len = (uint16_t)(name_len - base);
    return name + base;
}

static int zip_entry_basename_ieq(const char *wanted, const zip_entry_ref_t *entry)
{
    const char *want_base;
    const uint8_t *actual_base;
    uint16_t actual_len;

    if (!wanted || !entry)
        return 0;
    want_base = zip_member_basename(wanted);
    actual_base = zip_entry_basename_ptr(entry->name, entry->name_len, &actual_len);
    return zip_name_ieq(want_base, actual_base, actual_len);
}

static int zip_entry_supported_rom(const zip_entry_ref_t *entry)
{
    char name[PATH_MAX];
    if (!entry || entry->uncompressed_size == 0)
        return 0;
    if (!zip_copy_entry_name(entry, name, sizeof(name)))
        return 0;
    return zip_member_supported_rom_ext(name);
}

static int zip_parse_central_entry(const uint8_t *archive, size_t archive_size,
                                   size_t pos, zip_entry_ref_t *entry, size_t *next_pos)
{
    uint16_t name_len;
    uint16_t extra_len;
    uint16_t comment_len;
    uint32_t local_offset;

    if (!archive || !entry || !next_pos || pos + 46u > archive_size)
        return 0;
    if (zip_rd32(archive + pos) != 0x02014b50u)
        return 0;

    name_len = zip_rd16(archive + pos + 28);
    extra_len = zip_rd16(archive + pos + 30);
    comment_len = zip_rd16(archive + pos + 32);
    if (pos + 46u + name_len + extra_len + comment_len > archive_size)
        return 0;

    local_offset = zip_rd32(archive + pos + 42);
    if ((size_t)local_offset + 30u > archive_size)
        return 0;

    memset(entry, 0, sizeof(*entry));
    entry->archive = archive;
    entry->archive_size = archive_size;
    entry->name = archive + pos + 46;
    entry->name_len = name_len;
    entry->flags = zip_rd16(archive + pos + 8);
    entry->method = zip_rd16(archive + pos + 10);
    entry->crc = zip_rd32(archive + pos + 16);
    entry->compressed_size = zip_rd32(archive + pos + 20);
    entry->uncompressed_size = zip_rd32(archive + pos + 24);
    entry->local_header_offset = local_offset;
    *next_pos = pos + 46u + name_len + extra_len + comment_len;
    return 1;
}

static int zip_find_entry_in_memory(const uint8_t *archive, size_t archive_size,
                                    const char *wanted_name, int first_supported_rom,
                                    zip_entry_ref_t *out_entry)
{
    size_t eocd;
    size_t cd_offset_sz;
    size_t cd_size_sz;
    uint16_t disk_no;
    uint16_t cd_disk;
    uint16_t entries_this_disk;
    uint16_t entries_total;
    uint32_t cd_size;
    uint32_t cd_offset;
    int pass;

    if (!archive || !out_entry)
        return 0;
    if (!zip_find_eocd(archive, archive_size, &eocd))
        return 0;

    disk_no = zip_rd16(archive + eocd + 4);
    cd_disk = zip_rd16(archive + eocd + 6);
    entries_this_disk = zip_rd16(archive + eocd + 8);
    entries_total = zip_rd16(archive + eocd + 10);
    cd_size = zip_rd32(archive + eocd + 12);
    cd_offset = zip_rd32(archive + eocd + 16);

    if (disk_no || cd_disk || entries_this_disk != entries_total)
        return 0;
    cd_offset_sz = (size_t)cd_offset;
    cd_size_sz = (size_t)cd_size;
    if (cd_offset_sz > archive_size || cd_size_sz > archive_size - cd_offset_sz)
        return 0;

    /* Match MiniZip's old behavior: try the exact archive path first, then
     * fall back to basename matching.  Merged sets such as athena.zip contain
     * both p1.4p and athenab/p1.4p; a one-pass basename match would silently
     * pick the clone member first and reject it on CRC. */
    for (pass = 0; pass < (first_supported_rom ? 1 : 2); pass++)
    {
        size_t cd_pos = cd_offset_sz;
        size_t cd_end = cd_offset_sz + cd_size_sz;
        uint16_t i;
        for (i = 0; i < entries_total && cd_pos < cd_end; i++)
        {
            zip_entry_ref_t entry;
            size_t next_pos;
            if (!zip_parse_central_entry(archive, archive_size, cd_pos, &entry, &next_pos))
                return 0;

            if (first_supported_rom)
            {
                if (zip_entry_supported_rom(&entry))
                {
                    *out_entry = entry;
                    return 1;
                }
            }
            else if (wanted_name)
            {
                if ((pass == 0 && zip_name_ieq(wanted_name, entry.name, entry.name_len)) ||
                    (pass == 1 && zip_entry_basename_ieq(wanted_name, &entry)))
                {
                    *out_entry = entry;
                    return 1;
                }
            }
            cd_pos = next_pos;
        }
    }
    return 0;
}


static int zip_find_entry_by_size_crc_in_memory(const uint8_t *archive, size_t archive_size,
                                                uint32_t expected_size, uint32_t expected_crc,
                                                zip_entry_ref_t *out_entry)
{
    size_t eocd;
    size_t cd_offset_sz;
    size_t cd_size_sz;
    uint16_t disk_no;
    uint16_t cd_disk;
    uint16_t entries_this_disk;
    uint16_t entries_total;
    uint32_t cd_size;
    uint32_t cd_offset;
    size_t cd_pos;
    size_t cd_end;
    uint16_t i;

    if (!archive || !out_entry || !expected_size || !expected_crc)
        return 0;
    if (!zip_find_eocd(archive, archive_size, &eocd))
        return 0;

    disk_no = zip_rd16(archive + eocd + 4);
    cd_disk = zip_rd16(archive + eocd + 6);
    entries_this_disk = zip_rd16(archive + eocd + 8);
    entries_total = zip_rd16(archive + eocd + 10);
    cd_size = zip_rd32(archive + eocd + 12);
    cd_offset = zip_rd32(archive + eocd + 16);

    if (disk_no || cd_disk || entries_this_disk != entries_total)
        return 0;
    cd_offset_sz = (size_t)cd_offset;
    cd_size_sz = (size_t)cd_size;
    if (cd_offset_sz > archive_size || cd_size_sz > archive_size - cd_offset_sz)
        return 0;

    cd_pos = cd_offset_sz;
    cd_end = cd_offset_sz + cd_size_sz;
    for (i = 0; i < entries_total && cd_pos < cd_end; i++)
    {
        zip_entry_ref_t entry;
        size_t next_pos;
        if (!zip_parse_central_entry(archive, archive_size, cd_pos, &entry, &next_pos))
            return 0;
        if (entry.uncompressed_size == expected_size && entry.crc == expected_crc)
        {
            *out_entry = entry;
            return 1;
        }
        cd_pos = next_pos;
    }
    return 0;
}

static int zip_extract_entry(const zip_entry_ref_t *entry, uint8_t *dst,
                             uint32_t expected_size, uint32_t expected_crc)
{
    const uint8_t *archive;
    size_t archive_size;
    size_t local;
    uint16_t local_name_len;
    uint16_t local_extra_len;
    size_t data_offset;
    size_t out_size;

    if (!entry || !dst)
        return 0;
    archive = entry->archive;
    archive_size = entry->archive_size;
    if (!archive)
        return 0;

    if (entry->flags & 1u)          /* encrypted */
        return 0;
    if (entry->uncompressed_size != expected_size)
        return 0;
    if (expected_crc && entry->crc != expected_crc)
        return 0;

    local = (size_t)entry->local_header_offset;
    if (local + 30u > archive_size || zip_rd32(archive + local) != 0x04034b50u)
        return 0;
    local_name_len = zip_rd16(archive + local + 26);
    local_extra_len = zip_rd16(archive + local + 28);
    data_offset = local + 30u + local_name_len + local_extra_len;
    if (data_offset > archive_size || (size_t)entry->compressed_size > archive_size - data_offset)
        return 0;

    if (entry->method == 0)
    {
        if (entry->compressed_size != entry->uncompressed_size)
            return 0;
        memcpy(dst, archive + data_offset, expected_size);
        out_size = expected_size;
    }
    else if (entry->method == 8)
    {
        out_size = tinfl_decompress_mem_to_mem(dst, expected_size,
                                               archive + data_offset,
                                               entry->compressed_size,
                                               TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
        if (out_size == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED)
            return 0;
    }
    else
    {
        return 0;
    }

    if (out_size != expected_size)
        return 0;
    if ((uint32_t)crc32(0, dst, expected_size) != entry->crc)
        return 0;
    return 1;
}

int32_t zip_member_exists_in_archive(const char *archive_path, const char *name)
{
    uint8_t *archive = NULL;
    size_t archive_size = 0;
    zip_entry_ref_t entry;
    int ok;

    if (!zip_read_whole_file(archive_path, &archive, &archive_size))
        return 0;
    ok = zip_find_entry_in_memory(archive, archive_size, name, 0, &entry);
    free(archive);
    return ok;
}

int32_t loadZipMemberExact(const char *archive_path, const char *name,
                           uint8_t *dst, uint32_t expected_size, uint32_t expected_crc)
{
    uint8_t *archive = NULL;
    size_t archive_size = 0;
    zip_entry_ref_t entry;
    int ok = 0;

    if (!archive_path || !name || !dst || !expected_size)
        return 0;
    if (!zip_read_whole_file(archive_path, &archive, &archive_size))
        return 0;
    if (zip_find_entry_in_memory(archive, archive_size, name, 0, &entry))
        ok = zip_extract_entry(&entry, dst, expected_size, expected_crc);
    if (!ok && expected_crc && zip_find_entry_by_size_crc_in_memory(archive, archive_size, expected_size, expected_crc, &entry))
        ok = zip_extract_entry(&entry, dst, expected_size, expected_crc);
    free(archive);
    return ok;
}

uint8_t *loadFromZipByName(char *archive_path, char *filename, uint32_t *filesize)
{
    uint8_t *archive = NULL;
    size_t archive_size = 0;
    zip_entry_ref_t entry;
    uint8_t *buffer = NULL;
    char member_name[PATH_MAX];

    if (filename) filename[0] = '\0';
    if (filesize) *filesize = 0;
    if (!archive_path || !filename || !filesize)
        return NULL;

    if (!zip_read_whole_file(archive_path, &archive, &archive_size))
        return NULL;
    if (!zip_find_entry_in_memory(archive, archive_size, NULL, 1, &entry))
    {
        free(archive);
        return NULL;
    }
    if (!zip_copy_entry_name(&entry, member_name, sizeof(member_name)))
    {
        free(archive);
        return NULL;
    }

    buffer = (uint8_t *)malloc(entry.uncompressed_size);
    if (!buffer)
    {
        free(archive);
        return NULL;
    }
    if (!zip_extract_entry(&entry, buffer, entry.uncompressed_size, entry.crc))
    {
        free(buffer);
        free(archive);
        return NULL;
    }

    zip_apply_member_console_hint(member_name);
    *filesize = entry.uncompressed_size;
    snprintf(filename, PATH_MAX, "%s", member_name);
    free(archive);
    return buffer;
}

/*
    Verifies if a file is a ZIP archive or not.
    Returns: 1= ZIP archive, 0= not a ZIP archive
*/
int32_t check_zip(const char *filename)
{
    uint8_t buf[2] = {0, 0};
#if defined(_WIN32) || defined(WIN32)
    HANDLE h;
    DWORD got = 0;
    if (!filename || !filename[0]) return 0;
    h = CreateFileA(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    if (!ReadFile(h, buf, sizeof(buf), &got, NULL)) got = 0;
    CloseHandle(h);
    if (got != sizeof(buf)) return 0;
#else
    FILE* fd = NULL;
    if (!filename || !filename[0]) return 0;
    fd = fopen(filename, "rb");
    if(!fd) return (0);
    if (fread(buf, 1, sizeof(buf), fd) != sizeof(buf))
    {
        fclose(fd);
        return 0;
    }
    fclose(fd);
#endif
    if(memcmp(buf, "PK", 2) == 0) return (1);
    return (0);
}
