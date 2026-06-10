/*
 * MultiRexZ80 optional debug console support.
 *
 * Disabled in release builds unless MULTIREXZ80_DEBUG_CONSOLE is non-zero.
 */

#ifndef MULTIREXZ80_DEBUG_CONSOLE_H_
#define MULTIREXZ80_DEBUG_CONSOLE_H_

#include <stdint.h>

#ifndef MULTIREXZ80_DEBUG_CONSOLE
#define MULTIREXZ80_DEBUG_CONSOLE 0
#endif

typedef uint8_t (*multirexz80_debug_console_read8_t)(uint16_t address);

int multirexz80_debug_console_open(const char *path, const char *stop_text);
void multirexz80_debug_console_close(void);
int multirexz80_debug_console_is_active(void);
int multirexz80_debug_console_stop_requested(void);
void multirexz80_debug_console_probe_lddd(uint16_t pc_after_opcode,
                                          uint16_t hl,
                                          uint16_t de,
                                          multirexz80_debug_console_read8_t read8);

#endif /* MULTIREXZ80_DEBUG_CONSOLE_H_ */
