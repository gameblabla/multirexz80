/*
 * MultiRexZ80 optional debug console support.
 *
 * This implements the no$gmb/BGB/Emulicious LD D,D debug-message standard,
 * including the GBDK-style printf form used by ZEXALL-SMS 0.21:
 *
 *   ld d,d
 *   jr @end
 *   dw $6464
 *   dw $0200
 * @end
 *
 * For command $0200, the format string is read from HL and the argument block
 * is read from DE.  This file is intentionally dormant unless the build defines
 * MULTIREXZ80_DEBUG_CONSOLE to a non-zero value.
 */

#include "debug_console.h"

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef MULTIREXZ80_DEBUG_CONSOLE
#define MULTIREXZ80_DEBUG_CONSOLE 0
#endif

#if MULTIREXZ80_DEBUG_CONSOLE

#define DEBUG_CONSOLE_MAX_TEXT 4095u

static FILE *debug_console_fp;
static int debug_console_owns_fp;
static int debug_console_stop;
static char *debug_console_stop_text;
static size_t debug_console_stop_len;
static size_t debug_console_stop_pos;

static uint16_t rd16(multirexz80_debug_console_read8_t read8, uint16_t a)
{
    return (uint16_t)(read8(a) | ((uint16_t)read8((uint16_t)(a + 1u)) << 8));
}

static uint32_t rd32(multirexz80_debug_console_read8_t read8, uint16_t a)
{
    uint32_t lo = rd16(read8, a);
    uint32_t hi = rd16(read8, (uint16_t)(a + 2u));
    return lo | (hi << 16);
}

static uint64_t rd64(multirexz80_debug_console_read8_t read8, uint16_t a)
{
    uint64_t lo = rd32(read8, a);
    uint64_t hi = rd32(read8, (uint16_t)(a + 4u));
    return lo | (hi << 32);
}

static void stop_match_char(char c)
{
    if (!debug_console_stop_text || debug_console_stop_len == 0u || debug_console_stop)
        return;

    if (c == debug_console_stop_text[debug_console_stop_pos])
    {
        debug_console_stop_pos++;
        if (debug_console_stop_pos == debug_console_stop_len)
            debug_console_stop = 1;
        return;
    }

    debug_console_stop_pos = (c == debug_console_stop_text[0]) ? 1u : 0u;
}

static void emit_char(char c)
{
    if (!debug_console_fp)
        return;
    fputc((unsigned char)c, debug_console_fp);
    stop_match_char(c);
    if (c == '\n' || debug_console_stop)
        fflush(debug_console_fp);
}

static void emit_string(const char *s)
{
    while (*s)
    {
        emit_char(*s);
        s++;
    }
}

static void emit_printf(const char *fmt, ...)
{
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    emit_string(buf);
}

static void emit_string_from_mem(uint16_t addr, multirexz80_debug_console_read8_t read8)
{
    for (uint32_t i = 0; i < DEBUG_CONSOLE_MAX_TEXT; i++)
    {
        uint8_t c = read8((uint16_t)(addr + i));
        if (c == 0)
            break;
        emit_char((char)c);
    }
}

static int append_format_literal(char *fmt, size_t *used, size_t cap, char c)
{
    if (*used + 1u >= cap)
        return 0;
    fmt[*used] = c;
    (*used)++;
    fmt[*used] = '\0';
    return 1;
}

static uint16_t emit_printf_value(const char *fmt,
                                  char conv,
                                  unsigned length,
                                  uint16_t data,
                                  multirexz80_debug_console_read8_t read8)
{
    if (conv == 'c')
    {
        emit_printf(fmt, (int)read8(data));
        return (uint16_t)(data + 2u);
    }
    if (conv == 's')
    {
        uint16_t ptr = rd16(read8, data);
        if (strchr(fmt, 's'))
        {
            /* Width/padding on %s is rare for emulator debug messages.  Keep
             * this path simple and deterministic by emitting the string raw. */
            emit_string_from_mem(ptr, read8);
        }
        return (uint16_t)(data + 2u);
    }
    if (conv == 'd' || conv == 'i')
    {
        if (length == 2u)
        {
            int64_t v = (int64_t)rd64(read8, data);
            emit_printf(fmt, (long long)v);
            return (uint16_t)(data + 8u);
        }
        if (length == 1u)
        {
            int32_t v = (int32_t)rd32(read8, data);
            emit_printf(fmt, (long)v);
            return (uint16_t)(data + 4u);
        }
        if (length == 3u)
        {
            int v = (int8_t)read8(data);
            emit_printf(fmt, v);
            return (uint16_t)(data + 1u);
        }
        emit_printf(fmt, (int)(int16_t)rd16(read8, data));
        return (uint16_t)(data + 2u);
    }
    if (conv == 'u' || conv == 'x' || conv == 'X')
    {
        if (length == 2u)
        {
            unsigned long long v = (unsigned long long)rd64(read8, data);
            emit_printf(fmt, v);
            return (uint16_t)(data + 8u);
        }
        if (length == 1u)
        {
            unsigned long v = (unsigned long)rd32(read8, data);
            emit_printf(fmt, v);
            return (uint16_t)(data + 4u);
        }
        if (length == 3u)
        {
            unsigned v = read8(data);
            emit_printf(fmt, v);
            return (uint16_t)(data + 1u);
        }
        emit_printf(fmt, (unsigned)rd16(read8, data));
        return (uint16_t)(data + 2u);
    }
    if (conv == 'f')
    {
        union { uint32_t u; float f; } v;
        if (length == 1u)
        {
            union { uint64_t u; double d; } d;
            d.u = rd64(read8, data);
            emit_printf(fmt, d.d);
            return (uint16_t)(data + 8u);
        }
        v.u = rd32(read8, data);
        emit_printf(fmt, (double)v.f);
        return (uint16_t)(data + 4u);
    }

    emit_char('%');
    emit_char(conv);
    return data;
}

static void emit_printf_from_mem(uint16_t fmt_addr, uint16_t data_addr,
                                 multirexz80_debug_console_read8_t read8)
{
    uint16_t fmt_ptr = fmt_addr;
    uint16_t data_ptr = data_addr;

    for (uint32_t i = 0; i < DEBUG_CONSOLE_MAX_TEXT; i++)
    {
        char c = (char)read8(fmt_ptr++);
        if (c == '\0')
            break;
        if (c != '%')
        {
            emit_char(c);
            continue;
        }

        c = (char)read8(fmt_ptr++);
        if (c == '\0')
        {
            emit_char('%');
            break;
        }
        if (c == '%')
        {
            emit_char('%');
            continue;
        }

        char fmt[32];
        size_t used = 0;
        unsigned length = 0; /* 0=word/default, 1=long, 2=long long, 3=short/byte */
        append_format_literal(fmt, &used, sizeof(fmt), '%');

        while ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == ' ' || c == '#')
        {
            append_format_literal(fmt, &used, sizeof(fmt), c);
            c = (char)read8(fmt_ptr++);
        }
        if (c == 'h')
        {
            length = 3u;
            append_format_literal(fmt, &used, sizeof(fmt), c);
            c = (char)read8(fmt_ptr++);
        }
        else if (c == 'l')
        {
            length = 1u;
            append_format_literal(fmt, &used, sizeof(fmt), c);
            c = (char)read8(fmt_ptr++);
            if (c == 'l')
            {
                length = 2u;
                append_format_literal(fmt, &used, sizeof(fmt), c);
                c = (char)read8(fmt_ptr++);
            }
        }
        append_format_literal(fmt, &used, sizeof(fmt), c);
        data_ptr = emit_printf_value(fmt, c, length, data_ptr, read8);
    }
}

int multirexz80_debug_console_open(const char *path, const char *stop_text)
{
    if (!path || !*path)
        return 1;
    if (!strcmp(path, "-"))
    {
        debug_console_fp = stdout;
        debug_console_owns_fp = 0;
    }
    else
    {
        debug_console_fp = fopen(path, "wb");
        if (!debug_console_fp)
            return 0;
        debug_console_owns_fp = 1;
    }

    debug_console_stop = 0;
    debug_console_stop_pos = 0;
    free(debug_console_stop_text);
    debug_console_stop_text = NULL;
    debug_console_stop_len = 0;
    if (stop_text && *stop_text)
    {
        debug_console_stop_text = strdup(stop_text);
        if (!debug_console_stop_text)
        {
            multirexz80_debug_console_close();
            return 0;
        }
        debug_console_stop_len = strlen(debug_console_stop_text);
    }
    return 1;
}

void multirexz80_debug_console_close(void)
{
    if (debug_console_fp)
    {
        fflush(debug_console_fp);
        if (debug_console_owns_fp)
            fclose(debug_console_fp);
    }
    debug_console_fp = NULL;
    debug_console_owns_fp = 0;
    free(debug_console_stop_text);
    debug_console_stop_text = NULL;
    debug_console_stop_len = 0;
    debug_console_stop_pos = 0;
    debug_console_stop = 0;
}

int multirexz80_debug_console_is_active(void)
{
    return debug_console_fp != NULL;
}

int multirexz80_debug_console_stop_requested(void)
{
    return debug_console_stop;
}

void multirexz80_debug_console_probe_lddd(uint16_t pc_after_opcode,
                                          uint16_t hl,
                                          uint16_t de,
                                          multirexz80_debug_console_read8_t read8)
{
    if (!debug_console_fp || !read8)
        return;

    uint16_t pc = pc_after_opcode;
    if (read8(pc) != 0x18u) /* JR +payload */
        return;
    if (read8((uint16_t)(pc + 2u)) != 0x64u || read8((uint16_t)(pc + 3u)) != 0x64u)
        return;

    uint16_t command = rd16(read8, (uint16_t)(pc + 4u));
    if (command == 0x0000u)
    {
        emit_string_from_mem((uint16_t)(pc + 6u), read8);
    }
    else if (command == 0x0001u)
    {
        uint16_t addr = rd16(read8, (uint16_t)(pc + 6u));
        emit_string_from_mem(addr, read8);
    }
    else if (command == 0x0200u)
    {
        emit_printf_from_mem(hl, de, read8);
    }
}

#else /* MULTIREXZ80_DEBUG_CONSOLE */

int multirexz80_debug_console_open(const char *path, const char *stop_text)
{
    (void)stop_text;
    return (!path || !*path);
}

void multirexz80_debug_console_close(void) {}
int multirexz80_debug_console_is_active(void) { return 0; }
int multirexz80_debug_console_stop_requested(void) { return 0; }
void multirexz80_debug_console_probe_lddd(uint16_t pc_after_opcode,
                                          uint16_t hl,
                                          uint16_t de,
                                          multirexz80_debug_console_read8_t read8)
{
    (void)pc_after_opcode;
    (void)hl;
    (void)de;
    (void)read8;
}

#endif /* MULTIREXZ80_DEBUG_CONSOLE */
