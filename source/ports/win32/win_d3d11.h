/*
 * MultiRexZ80 native Windows frontend - Direct3D 11 presentation backend.
 *
 * Win64-only (guarded by _WIN64).  Presents the emulator framebuffer as a
 * point-sampled textured quad so window scaling happens on the GPU instead of
 * GDI's software StretchDIBits, whose cost grows with the window resolution.
 *
 * The whole module compiles to nothing on the Win32 (Win95-class) target, which
 * stays GDI-only by design.
 */
#ifndef MULTIREXZ80_WIN_D3D11_H
#define MULTIREXZ80_WIN_D3D11_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#include <windows.h>
#include <stdint.h>

typedef struct win_d3d11 win_d3d11_t;

/* Create a D3D11 presenter for hwnd.  Returns NULL if D3D11 (or its shader
 * compiler) is unavailable; the caller should fall back to the GDI path. */
win_d3d11_t *win_d3d11_create(HWND hwnd);
void win_d3d11_destroy(win_d3d11_t *d);

/* Notify the swap chain of a new client size. */
void win_d3d11_resize(win_d3d11_t *d, int width, int height);

/*
 * Present one frame.  pixels is src_w*src_h XRGB8888 (top-down).  dst is the
 * destination rectangle inside the client (already computed by the frontend's
 * aspect/integer/stretch logic); win_w/win_h are the client size.  Returns 1 on
 * success, 0 on failure (caller should fall back to GDI for this frame).
 */
int win_d3d11_present(win_d3d11_t *d, const uint32_t *pixels, int src_w, int src_h,
                      const RECT *dst, int win_w, int win_h);

const char *win_d3d11_backend_name(const win_d3d11_t *d);

#endif /* MULTIREXZ80_WIN_D3D11_H */
