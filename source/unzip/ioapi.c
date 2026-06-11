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

/* ioapi.h -- IO base function header for compress/uncompress .zip
   part of the MiniZip project - ( http://www.winimage.com/zLibDll/minizip.html )

         Copyright (C) 1998-2010 Gilles Vollant (minizip) ( http://www.winimage.com/zLibDll/minizip.html )

         Modifications for Zip64 support
         Copyright (C) 2009-2010 Mathias Svensson ( http://result42.com )

         For more info read MiniZip_info.txt

*/

#if defined(_WIN32) && (!(defined(_CRT_SECURE_NO_WARNINGS)))
        #define _CRT_SECURE_NO_WARNINGS
#endif

#if defined(__APPLE__) || defined(IOAPI_NO_64)
// In darwin and perhaps other BSD variants off_t is a 64 bit value, hence no need for specific 64 bit functions
#define FOPEN_FUNC(filename, mode) fopen(filename, mode)
#define FTELLO_FUNC(stream) ftello(stream)
#define FSEEKO_FUNC(stream, offset, origin) fseeko(stream, offset, origin)
#else
#define FOPEN_FUNC(filename, mode) fopen64(filename, mode)
#define FTELLO_FUNC(stream) ftello64(stream)
#define FSEEKO_FUNC(stream, offset, origin) fseeko64(stream, offset, origin)
#endif


#include "ioapi.h"

#if defined(_WIN32) || defined(WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

voidpf call_zopen64 (const zlib_filefunc64_32_def* pfilefunc,const void*filename,int mode)
{
    if (pfilefunc->zfile_func64.zopen64_file != NULL)
        return (*(pfilefunc->zfile_func64.zopen64_file)) (pfilefunc->zfile_func64.opaque,filename,mode);
    else
    {
        return (*(pfilefunc->zopen32_file))(pfilefunc->zfile_func64.opaque,(const char*)filename,mode);
    }
}

long call_zseek64 (const zlib_filefunc64_32_def* pfilefunc,voidpf filestream, ZPOS64_T offset, int origin)
{
    if (pfilefunc->zfile_func64.zseek64_file != NULL)
        return (*(pfilefunc->zfile_func64.zseek64_file)) (pfilefunc->zfile_func64.opaque,filestream,offset,origin);
    else
    {
        uLong offsetTruncated = (uLong)offset;
        if (offsetTruncated != offset)
            return -1;
        else
            return (*(pfilefunc->zseek32_file))(pfilefunc->zfile_func64.opaque,filestream,offsetTruncated,origin);
    }
}

ZPOS64_T call_ztell64 (const zlib_filefunc64_32_def* pfilefunc,voidpf filestream)
{
    if (pfilefunc->zfile_func64.zseek64_file != NULL)
        return (*(pfilefunc->zfile_func64.ztell64_file)) (pfilefunc->zfile_func64.opaque,filestream);
    else
    {
        uLong tell_uLong = (*(pfilefunc->ztell32_file))(pfilefunc->zfile_func64.opaque,filestream);
        if ((tell_uLong) == MAXU32)
            return (ZPOS64_T)-1;
        else
            return tell_uLong;
    }
}

void fill_zlib_filefunc64_32_def_from_filefunc32(zlib_filefunc64_32_def* p_filefunc64_32,const zlib_filefunc_def* p_filefunc32)
{
    p_filefunc64_32->zfile_func64.zopen64_file = NULL;
    p_filefunc64_32->zopen32_file = p_filefunc32->zopen_file;
    p_filefunc64_32->zfile_func64.zerror_file = p_filefunc32->zerror_file;
    p_filefunc64_32->zfile_func64.zread_file = p_filefunc32->zread_file;
    p_filefunc64_32->zfile_func64.zwrite_file = p_filefunc32->zwrite_file;
    p_filefunc64_32->zfile_func64.ztell64_file = NULL;
    p_filefunc64_32->zfile_func64.zseek64_file = NULL;
    p_filefunc64_32->zfile_func64.zclose_file = p_filefunc32->zclose_file;
    p_filefunc64_32->zfile_func64.zerror_file = p_filefunc32->zerror_file;
    p_filefunc64_32->zfile_func64.opaque = p_filefunc32->opaque;
    p_filefunc64_32->zseek32_file = p_filefunc32->zseek_file;
    p_filefunc64_32->ztell32_file = p_filefunc32->ztell_file;
}



static voidpf  ZCALLBACK fopen_file_func OF((voidpf opaque, const char* filename, int mode));
static uLong   ZCALLBACK fread_file_func OF((voidpf opaque, voidpf stream, void* buf, uLong size));
static uLong   ZCALLBACK fwrite_file_func OF((voidpf opaque, voidpf stream, const void* buf,uLong size));
static ZPOS64_T ZCALLBACK ftell64_file_func OF((voidpf opaque, voidpf stream));
static long    ZCALLBACK fseek64_file_func OF((voidpf opaque, voidpf stream, ZPOS64_T offset, int origin));
static int     ZCALLBACK fclose_file_func OF((voidpf opaque, voidpf stream));
static int     ZCALLBACK ferror_file_func OF((voidpf opaque, voidpf stream));

static voidpf ZCALLBACK fopen_file_func (voidpf opaque, const char* filename, int mode)
{
    FILE* file = NULL;
    const char* mode_fopen = NULL;
    if ((mode & ZLIB_FILEFUNC_MODE_READWRITEFILTER)==ZLIB_FILEFUNC_MODE_READ)
        mode_fopen = "rb";
    else
    if (mode & ZLIB_FILEFUNC_MODE_EXISTING)
        mode_fopen = "r+b";
    else
    if (mode & ZLIB_FILEFUNC_MODE_CREATE)
        mode_fopen = "wb";

    if ((filename!=NULL) && (mode_fopen != NULL))
        file = fopen(filename, mode_fopen);
    return file;
}

static voidpf ZCALLBACK fopen64_file_func (voidpf opaque, const void* filename, int mode)
{
    FILE* file = NULL;
    const char* mode_fopen = NULL;
    if ((mode & ZLIB_FILEFUNC_MODE_READWRITEFILTER)==ZLIB_FILEFUNC_MODE_READ)
        mode_fopen = "rb";
    else
    if (mode & ZLIB_FILEFUNC_MODE_EXISTING)
        mode_fopen = "r+b";
    else
    if (mode & ZLIB_FILEFUNC_MODE_CREATE)
        mode_fopen = "wb";

    if ((filename!=NULL) && (mode_fopen != NULL))
        file = FOPEN_FUNC((const char*)filename, mode_fopen);
    return file;
}


static uLong ZCALLBACK fread_file_func (voidpf opaque, voidpf stream, void* buf, uLong size)
{
    uLong ret;
    ret = (uLong)fread(buf, 1, (size_t)size, (FILE *)stream);
    return ret;
}

static uLong ZCALLBACK fwrite_file_func (voidpf opaque, voidpf stream, const void* buf, uLong size)
{
    uLong ret;
    ret = (uLong)fwrite(buf, 1, (size_t)size, (FILE *)stream);
    return ret;
}

static long ZCALLBACK ftell_file_func (voidpf opaque, voidpf stream)
{
    long ret;
    ret = ftell((FILE *)stream);
    return ret;
}


static ZPOS64_T ZCALLBACK ftell64_file_func (voidpf opaque, voidpf stream)
{
    ZPOS64_T ret;
    ret = FTELLO_FUNC((FILE *)stream);
    return ret;
}

static long ZCALLBACK fseek_file_func (voidpf  opaque, voidpf stream, uLong offset, int origin)
{
    int fseek_origin=0;
    long ret;
    switch (origin)
    {
    case ZLIB_FILEFUNC_SEEK_CUR :
        fseek_origin = SEEK_CUR;
        break;
    case ZLIB_FILEFUNC_SEEK_END :
        fseek_origin = SEEK_END;
        break;
    case ZLIB_FILEFUNC_SEEK_SET :
        fseek_origin = SEEK_SET;
        break;
    default: return -1;
    }
    ret = 0;
    if (fseek((FILE *)stream, offset, fseek_origin) != 0)
        ret = -1;
    return ret;
}

static long ZCALLBACK fseek64_file_func (voidpf  opaque, voidpf stream, ZPOS64_T offset, int origin)
{
    int fseek_origin=0;
    long ret;
    switch (origin)
    {
    case ZLIB_FILEFUNC_SEEK_CUR :
        fseek_origin = SEEK_CUR;
        break;
    case ZLIB_FILEFUNC_SEEK_END :
        fseek_origin = SEEK_END;
        break;
    case ZLIB_FILEFUNC_SEEK_SET :
        fseek_origin = SEEK_SET;
        break;
    default: return -1;
    }
    ret = 0;

    if(FSEEKO_FUNC((FILE *)stream, offset, fseek_origin) != 0)
                        ret = -1;

    return ret;
}


static int ZCALLBACK fclose_file_func (voidpf opaque, voidpf stream)
{
    int ret;
    ret = fclose((FILE *)stream);
    return ret;
}

static int ZCALLBACK ferror_file_func (voidpf opaque, voidpf stream)
{
    int ret;
    ret = ferror((FILE *)stream);
    return ret;
}


#if defined(_WIN32) || defined(WIN32)
/*
 * The generic MiniZip stdio64 callbacks are fragile on Win32/Win64 MinGW:
 * they route unzOpen() through fopen64/fseeko64/ftello64 aliases whose exact
 * ABI varies between CRTs.  When the seek/tell callback misreports position,
 * the ZIP central-directory search or deflate read can spin inside a UI thread
 * load, making the Win32 frontend look like it entered an endless menu/repaint
 * loop.  The emulator only needs normal local ROM archives, so use native
 * Win32 file handles for ZIP I/O and keep all offsets explicit 64-bit values.
 */
typedef struct win32_zip_file_s
{
    HANDLE handle;
    DWORD last_error;
} win32_zip_file_t;

static voidpf ZCALLBACK win32_open_file_func(voidpf opaque, const char* filename, int mode)
{
    win32_zip_file_t *wf;
    DWORD access = 0;
    DWORD creation = OPEN_EXISTING;
    (void)opaque;

    if (!filename)
        return NULL;

    if ((mode & ZLIB_FILEFUNC_MODE_READWRITEFILTER) == ZLIB_FILEFUNC_MODE_READ)
        access |= GENERIC_READ;
    if (mode & ZLIB_FILEFUNC_MODE_WRITE)
        access |= GENERIC_WRITE;
    if (mode & ZLIB_FILEFUNC_MODE_EXISTING)
        creation = OPEN_EXISTING;
    if (mode & ZLIB_FILEFUNC_MODE_CREATE)
        creation = CREATE_ALWAYS;

    if (!access)
        return NULL;

    wf = (win32_zip_file_t *)malloc(sizeof(*wf));
    if (!wf)
        return NULL;
    wf->last_error = ERROR_SUCCESS;
    wf->handle = CreateFileA(filename, access, FILE_SHARE_READ, NULL, creation,
                             FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS, NULL);
    if (wf->handle == INVALID_HANDLE_VALUE)
    {
        wf->last_error = GetLastError();
        free(wf);
        return NULL;
    }
    return wf;
}

static voidpf ZCALLBACK win32_open64_file_func(voidpf opaque, const void* filename, int mode)
{
    return win32_open_file_func(opaque, (const char *)filename, mode);
}

static uLong ZCALLBACK win32_read_file_func(voidpf opaque, voidpf stream, void* buf, uLong size)
{
    win32_zip_file_t *wf = (win32_zip_file_t *)stream;
    DWORD want;
    DWORD got = 0;
    (void)opaque;
    if (!wf || wf->handle == INVALID_HANDLE_VALUE || !buf)
        return 0;
    while (size > 0)
    {
        want = size > 0x40000000UL ? 0x40000000UL : (DWORD)size;
        if (!ReadFile(wf->handle, buf, want, &got, NULL))
        {
            wf->last_error = GetLastError();
            return 0;
        }
        return (uLong)got;
    }
    return 0;
}

static uLong ZCALLBACK win32_write_file_func(voidpf opaque, voidpf stream, const void* buf, uLong size)
{
    win32_zip_file_t *wf = (win32_zip_file_t *)stream;
    DWORD want;
    DWORD done = 0;
    (void)opaque;
    if (!wf || wf->handle == INVALID_HANDLE_VALUE || !buf)
        return 0;
    want = size > 0x40000000UL ? 0x40000000UL : (DWORD)size;
    if (!WriteFile(wf->handle, buf, want, &done, NULL))
    {
        wf->last_error = GetLastError();
        return 0;
    }
    return (uLong)done;
}

static ZPOS64_T ZCALLBACK win32_tell64_file_func(voidpf opaque, voidpf stream)
{
    win32_zip_file_t *wf = (win32_zip_file_t *)stream;
    LARGE_INTEGER zero;
    LARGE_INTEGER pos;
    (void)opaque;
    if (!wf || wf->handle == INVALID_HANDLE_VALUE)
        return (ZPOS64_T)-1;
    zero.QuadPart = 0;
    if (!SetFilePointerEx(wf->handle, zero, &pos, FILE_CURRENT))
    {
        wf->last_error = GetLastError();
        return (ZPOS64_T)-1;
    }
    return (ZPOS64_T)pos.QuadPart;
}

static long ZCALLBACK win32_tell_file_func(voidpf opaque, voidpf stream)
{
    ZPOS64_T pos = win32_tell64_file_func(opaque, stream);
    if (pos > 0x7fffffffULL)
        return -1;
    return (long)pos;
}

static long ZCALLBACK win32_seek64_file_func(voidpf opaque, voidpf stream, ZPOS64_T offset, int origin)
{
    win32_zip_file_t *wf = (win32_zip_file_t *)stream;
    LARGE_INTEGER dist;
    DWORD move_method;
    (void)opaque;
    if (!wf || wf->handle == INVALID_HANDLE_VALUE)
        return -1;
    switch (origin)
    {
    case ZLIB_FILEFUNC_SEEK_CUR: move_method = FILE_CURRENT; break;
    case ZLIB_FILEFUNC_SEEK_END: move_method = FILE_END; break;
    case ZLIB_FILEFUNC_SEEK_SET: move_method = FILE_BEGIN; break;
    default: return -1;
    }
    dist.QuadPart = (LONGLONG)offset;
    if (!SetFilePointerEx(wf->handle, dist, NULL, move_method))
    {
        wf->last_error = GetLastError();
        return -1;
    }
    return 0;
}

static long ZCALLBACK win32_seek_file_func(voidpf opaque, voidpf stream, uLong offset, int origin)
{
    return win32_seek64_file_func(opaque, stream, (ZPOS64_T)offset, origin);
}

static int ZCALLBACK win32_close_file_func(voidpf opaque, voidpf stream)
{
    win32_zip_file_t *wf = (win32_zip_file_t *)stream;
    int ok;
    (void)opaque;
    if (!wf)
        return EOF;
    ok = (wf->handle != INVALID_HANDLE_VALUE) ? CloseHandle(wf->handle) : 1;
    free(wf);
    return ok ? 0 : EOF;
}

static int ZCALLBACK win32_error_file_func(voidpf opaque, voidpf stream)
{
    win32_zip_file_t *wf = (win32_zip_file_t *)stream;
    (void)opaque;
    return (wf && wf->last_error != ERROR_SUCCESS) ? 1 : 0;
}
#endif

void fill_fopen_filefunc (pzlib_filefunc_def)
  zlib_filefunc_def* pzlib_filefunc_def;
{
#if defined(_WIN32) || defined(WIN32)
    pzlib_filefunc_def->zopen_file = win32_open_file_func;
    pzlib_filefunc_def->zread_file = win32_read_file_func;
    pzlib_filefunc_def->zwrite_file = win32_write_file_func;
    pzlib_filefunc_def->ztell_file = win32_tell_file_func;
    pzlib_filefunc_def->zseek_file = win32_seek_file_func;
    pzlib_filefunc_def->zclose_file = win32_close_file_func;
    pzlib_filefunc_def->zerror_file = win32_error_file_func;
#else
    pzlib_filefunc_def->zopen_file = fopen_file_func;
    pzlib_filefunc_def->zread_file = fread_file_func;
    pzlib_filefunc_def->zwrite_file = fwrite_file_func;
    pzlib_filefunc_def->ztell_file = ftell_file_func;
    pzlib_filefunc_def->zseek_file = fseek_file_func;
    pzlib_filefunc_def->zclose_file = fclose_file_func;
    pzlib_filefunc_def->zerror_file = ferror_file_func;
#endif
    pzlib_filefunc_def->opaque = NULL;
}

void fill_fopen64_filefunc (zlib_filefunc64_def*  pzlib_filefunc_def)
{
#if defined(_WIN32) || defined(WIN32)
    pzlib_filefunc_def->zopen64_file = win32_open64_file_func;
    pzlib_filefunc_def->zread_file = win32_read_file_func;
    pzlib_filefunc_def->zwrite_file = win32_write_file_func;
    pzlib_filefunc_def->ztell64_file = win32_tell64_file_func;
    pzlib_filefunc_def->zseek64_file = win32_seek64_file_func;
    pzlib_filefunc_def->zclose_file = win32_close_file_func;
    pzlib_filefunc_def->zerror_file = win32_error_file_func;
#else
    pzlib_filefunc_def->zopen64_file = fopen64_file_func;
    pzlib_filefunc_def->zread_file = fread_file_func;
    pzlib_filefunc_def->zwrite_file = fwrite_file_func;
    pzlib_filefunc_def->ztell64_file = ftell64_file_func;
    pzlib_filefunc_def->zseek64_file = fseek64_file_func;
    pzlib_filefunc_def->zclose_file = fclose_file_func;
    pzlib_filefunc_def->zerror_file = ferror_file_func;
#endif
    pzlib_filefunc_def->opaque = NULL;
}
