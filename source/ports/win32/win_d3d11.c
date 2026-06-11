#include "win_d3d11.h"

#ifdef _WIN64

#ifndef COBJMACROS
#define COBJMACROS 1
#endif
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <stdlib.h>
#include <string.h>

typedef HRESULT (WINAPI *d3dcompile_fn)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO *,
                                        ID3DInclude *, LPCSTR, LPCSTR, UINT, UINT,
                                        ID3DBlob **, ID3DBlob **);

typedef struct d3d_vertex { float x, y, u, v; } d3d_vertex_t;

struct win_d3d11 {
    HWND hwnd;
    int tex_w, tex_h;            /* current dynamic texture dimensions */
    char backend_name[16];
    HMODULE compiler_dll;
    d3dcompile_fn compile;
    ID3D11Device *device;
    ID3D11DeviceContext *ctx;
    IDXGISwapChain *swap;
    ID3D11RenderTargetView *rtv;
    ID3D11VertexShader *vs;
    ID3D11PixelShader *ps;
    ID3D11InputLayout *layout;
    ID3D11Buffer *vbuf;
    ID3D11Texture2D *tex;
    ID3D11ShaderResourceView *srv;
    ID3D11SamplerState *sampler;
};

static const char *k_shader =
    "Texture2D tex : register(t0);\n"
    "SamplerState smp : register(s0);\n"
    "struct VSIn { float2 pos : POSITION; float2 tex : TEXCOORD0; };\n"
    "struct VSOut { float4 pos : SV_POSITION; float2 tex : TEXCOORD0; };\n"
    "VSOut vs_main(VSIn i){ VSOut o; o.pos=float4(i.pos,0,1); o.tex=i.tex; return o; }\n"
    "float4 ps_main(VSOut i):SV_Target { return tex.Sample(smp,i.tex); }\n";

static int load_compiler(win_d3d11_t *d) {
    static const char *dlls[] = { "d3dcompiler_47.dll", "d3dcompiler_46.dll", "d3dcompiler_43.dll" };
    for (size_t i = 0; i < sizeof(dlls)/sizeof(dlls[0]); ++i) {
        HMODULE m = LoadLibraryA(dlls[i]);
        if (!m) continue;
        d->compile = (d3dcompile_fn)(void(*)(void))GetProcAddress(m, "D3DCompile");
        if (d->compile) { d->compiler_dll = m; return 1; }
        FreeLibrary(m);
    }
    return 0;
}

static int compile_shader(win_d3d11_t *d, const char *entry, const char *target, ID3DBlob **out) {
    ID3DBlob *blob = NULL, *err = NULL;
    HRESULT hr;
    if (!d->compile || !out) return 0;
    *out = NULL;
    hr = d->compile(k_shader, strlen(k_shader), "win_d3d11.hlsl", NULL, NULL, entry, target, 0, 0, &blob, &err);
    if (err) { const char *m = (const char *)ID3D10Blob_GetBufferPointer(err); if (m && *m) OutputDebugStringA(m); ID3D10Blob_Release(err); }
    if (FAILED(hr) || !blob) { if (blob) ID3D10Blob_Release(blob); return 0; }
    *out = blob;
    return 1;
}

static int create_rtv(win_d3d11_t *d) {
    ID3D11Texture2D *back = NULL;
    HRESULT hr = IDXGISwapChain_GetBuffer(d->swap, 0, &IID_ID3D11Texture2D, (void **)&back);
    if (FAILED(hr) || !back) return 0;
    hr = ID3D11Device_CreateRenderTargetView(d->device, (ID3D11Resource *)back, NULL, &d->rtv);
    ID3D11Texture2D_Release(back);
    return SUCCEEDED(hr) && d->rtv;
}

static void release_texture(win_d3d11_t *d) {
    if (d->srv) { ID3D11ShaderResourceView_Release(d->srv); d->srv = NULL; }
    if (d->tex) { ID3D11Texture2D_Release(d->tex); d->tex = NULL; }
    d->tex_w = d->tex_h = 0;
}

/* (Re)create the dynamic source texture when the framebuffer size changes. */
static int ensure_texture(win_d3d11_t *d, int w, int h) {
    D3D11_TEXTURE2D_DESC td; D3D11_SHADER_RESOURCE_VIEW_DESC sd; HRESULT hr;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (d->tex && d->srv && d->tex_w == w && d->tex_h == h) return 1;
    release_texture(d);
    memset(&td, 0, sizeof(td));
    td.Width = (UINT)w; td.Height = (UINT)h; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DYNAMIC; td.BindFlags = D3D11_BIND_SHADER_RESOURCE; td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = ID3D11Device_CreateTexture2D(d->device, &td, NULL, &d->tex);
    if (FAILED(hr)) return 0;
    memset(&sd, 0, sizeof(sd));
    sd.Format = td.Format; sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; sd.Texture2D.MipLevels = 1;
    hr = ID3D11Device_CreateShaderResourceView(d->device, (ID3D11Resource *)d->tex, &sd, &d->srv);
    if (FAILED(hr)) { release_texture(d); return 0; }
    d->tex_w = w; d->tex_h = h;
    return 1;
}

static void release_pipeline(win_d3d11_t *d) {
    if (d->ctx) { ID3D11DeviceContext_ClearState(d->ctx); }
    if (d->sampler) { ID3D11SamplerState_Release(d->sampler); d->sampler = NULL; }
    release_texture(d);
    if (d->vbuf) { ID3D11Buffer_Release(d->vbuf); d->vbuf = NULL; }
    if (d->layout) { ID3D11InputLayout_Release(d->layout); d->layout = NULL; }
    if (d->ps) { ID3D11PixelShader_Release(d->ps); d->ps = NULL; }
    if (d->vs) { ID3D11VertexShader_Release(d->vs); d->vs = NULL; }
}

static int create_pipeline(win_d3d11_t *d) {
    ID3DBlob *vsb = NULL, *psb = NULL; HRESULT hr;
    D3D11_INPUT_ELEMENT_DESC il[2];
    D3D11_BUFFER_DESC vbd;
    D3D11_SAMPLER_DESC sm;
    if (!load_compiler(d)) return 0;
    if (!compile_shader(d, "vs_main", "vs_4_0", &vsb)) return 0;
    if (!compile_shader(d, "ps_main", "ps_4_0", &psb)) { ID3D10Blob_Release(vsb); return 0; }
    hr = ID3D11Device_CreateVertexShader(d->device, ID3D10Blob_GetBufferPointer(vsb), ID3D10Blob_GetBufferSize(vsb), NULL, &d->vs);
    if (FAILED(hr)) goto fail;
    hr = ID3D11Device_CreatePixelShader(d->device, ID3D10Blob_GetBufferPointer(psb), ID3D10Blob_GetBufferSize(psb), NULL, &d->ps);
    if (FAILED(hr)) goto fail;
    memset(il, 0, sizeof(il));
    il[0].SemanticName = "POSITION"; il[0].Format = DXGI_FORMAT_R32G32_FLOAT; il[0].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
    il[1].SemanticName = "TEXCOORD"; il[1].Format = DXGI_FORMAT_R32G32_FLOAT; il[1].AlignedByteOffset = 8; il[1].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
    hr = ID3D11Device_CreateInputLayout(d->device, il, 2, ID3D10Blob_GetBufferPointer(vsb), ID3D10Blob_GetBufferSize(vsb), &d->layout);
    if (FAILED(hr)) goto fail;
    memset(&vbd, 0, sizeof(vbd));
    vbd.ByteWidth = (UINT)(sizeof(d3d_vertex_t) * 4u); vbd.Usage = D3D11_USAGE_DYNAMIC; vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER; vbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = ID3D11Device_CreateBuffer(d->device, &vbd, NULL, &d->vbuf);
    if (FAILED(hr)) goto fail;
    memset(&sm, 0, sizeof(sm));
    sm.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT; sm.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP; sm.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP; sm.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; sm.MaxLOD = D3D11_FLOAT32_MAX;
    hr = ID3D11Device_CreateSamplerState(d->device, &sm, &d->sampler);
    if (FAILED(hr)) goto fail;
    ID3D10Blob_Release(vsb); ID3D10Blob_Release(psb);
    return 1;
fail:
    if (vsb) ID3D10Blob_Release(vsb);
    if (psb) ID3D10Blob_Release(psb);
    release_pipeline(d);
    return 0;
}

win_d3d11_t *win_d3d11_create(HWND hwnd) {
    win_d3d11_t *d;
    RECT rc;
    DXGI_SWAP_CHAIN_DESC sd;
    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 }, got;
    D3D_DRIVER_TYPE drivers[] = { D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP };
    HRESULT hr = E_FAIL;
    size_t i;
    if (!hwnd) return NULL;
    d = (win_d3d11_t *)calloc(1, sizeof(*d));
    if (!d) return NULL;
    d->hwnd = hwnd;
    strcpy(d->backend_name, "d3d11");
    GetClientRect(hwnd, &rc);
    memset(&sd, 0, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = (UINT)((rc.right > rc.left) ? rc.right - rc.left : 1);
    sd.BufferDesc.Height = (UINT)((rc.bottom > rc.top) ? rc.bottom - rc.top : 1);
    sd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd; sd.SampleDesc.Count = 1; sd.Windowed = TRUE; sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    for (i = 0; i < sizeof(drivers)/sizeof(drivers[0]); ++i) {
        hr = D3D11CreateDeviceAndSwapChain(NULL, drivers[i], NULL, 0, levels,
                                           (UINT)(sizeof(levels)/sizeof(levels[0])), D3D11_SDK_VERSION,
                                           &sd, &d->swap, &d->device, &got, &d->ctx);
        if (SUCCEEDED(hr)) break;
    }
    if (FAILED(hr) || !create_rtv(d) || !create_pipeline(d)) { win_d3d11_destroy(d); return NULL; }
    return d;
}

void win_d3d11_destroy(win_d3d11_t *d) {
    if (!d) return;
    release_pipeline(d);
    if (d->rtv) ID3D11RenderTargetView_Release(d->rtv);
    if (d->swap) IDXGISwapChain_Release(d->swap);
    if (d->ctx) ID3D11DeviceContext_Release(d->ctx);
    if (d->device) ID3D11Device_Release(d->device);
    if (d->compiler_dll) FreeLibrary(d->compiler_dll);
    free(d);
}

void win_d3d11_resize(win_d3d11_t *d, int width, int height) {
    if (!d || !d->swap) return;
    if (d->rtv) { ID3D11RenderTargetView_Release(d->rtv); d->rtv = NULL; }
    if (SUCCEEDED(IDXGISwapChain_ResizeBuffers(d->swap, 0, (UINT)(width > 0 ? width : 1), (UINT)(height > 0 ? height : 1), DXGI_FORMAT_UNKNOWN, 0)))
        (void)create_rtv(d);
}

static int upload(win_d3d11_t *d, const uint32_t *pixels, int w, int h) {
    D3D11_MAPPED_SUBRESOURCE map;
    HRESULT hr = ID3D11DeviceContext_Map(d->ctx, (ID3D11Resource *)d->tex, 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
    int y;
    if (FAILED(hr)) return 0;
    for (y = 0; y < h; ++y)
        memcpy((uint8_t *)map.pData + (size_t)y * map.RowPitch, pixels + (size_t)y * w, (size_t)w * sizeof(uint32_t));
    ID3D11DeviceContext_Unmap(d->ctx, (ID3D11Resource *)d->tex, 0);
    return 1;
}

static int upload_vertices(win_d3d11_t *d, const RECT *dst, int win_w, int win_h) {
    float ww = (float)(win_w > 0 ? win_w : 1);
    float wh = (float)(win_h > 0 ? win_h : 1);
    float l = ((float)dst->left / ww) * 2.0f - 1.0f;
    float r = ((float)dst->right / ww) * 2.0f - 1.0f;
    float t = 1.0f - ((float)dst->top / wh) * 2.0f;
    float b = 1.0f - ((float)dst->bottom / wh) * 2.0f;
    d3d_vertex_t v[4] = { {l,t,0,0}, {r,t,1,0}, {l,b,0,1}, {r,b,1,1} };
    D3D11_MAPPED_SUBRESOURCE map;
    HRESULT hr = ID3D11DeviceContext_Map(d->ctx, (ID3D11Resource *)d->vbuf, 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
    if (FAILED(hr)) return 0;
    memcpy(map.pData, v, sizeof(v));
    ID3D11DeviceContext_Unmap(d->ctx, (ID3D11Resource *)d->vbuf, 0);
    return 1;
}

int win_d3d11_present(win_d3d11_t *d, const uint32_t *pixels, int src_w, int src_h,
                      const RECT *dst, int win_w, int win_h) {
    FLOAT clear[4] = {0,0,0,1};
    D3D11_VIEWPORT vp;
    UINT stride = sizeof(d3d_vertex_t), offset = 0;
    ID3D11ShaderResourceView *null_srv = NULL;
    if (!d || !d->rtv || !pixels || !dst || src_w < 1 || src_h < 1) return 0;
    if (!ensure_texture(d, src_w, src_h)) return 0;
    if (!upload(d, pixels, src_w, src_h) || !upload_vertices(d, dst, win_w, win_h)) return 0;
    memset(&vp, 0, sizeof(vp));
    vp.Width = (float)(win_w > 0 ? win_w : 1); vp.Height = (float)(win_h > 0 ? win_h : 1); vp.MaxDepth = 1.0f;
    ID3D11DeviceContext_OMSetRenderTargets(d->ctx, 1, &d->rtv, NULL);
    ID3D11DeviceContext_RSSetViewports(d->ctx, 1, &vp);
    ID3D11DeviceContext_ClearRenderTargetView(d->ctx, d->rtv, clear);
    ID3D11DeviceContext_IASetInputLayout(d->ctx, d->layout);
    ID3D11DeviceContext_IASetVertexBuffers(d->ctx, 0, 1, &d->vbuf, &stride, &offset);
    ID3D11DeviceContext_IASetPrimitiveTopology(d->ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ID3D11DeviceContext_VSSetShader(d->ctx, d->vs, NULL, 0);
    ID3D11DeviceContext_PSSetShader(d->ctx, d->ps, NULL, 0);
    ID3D11DeviceContext_PSSetShaderResources(d->ctx, 0, 1, &d->srv);
    ID3D11DeviceContext_PSSetSamplers(d->ctx, 0, 1, &d->sampler);
    ID3D11DeviceContext_Draw(d->ctx, 4, 0);
    ID3D11DeviceContext_PSSetShaderResources(d->ctx, 0, 1, &null_srv);
    return SUCCEEDED(IDXGISwapChain_Present(d->swap, 0, 0));
}

const char *win_d3d11_backend_name(const win_d3d11_t *d) { return d ? d->backend_name : "none"; }

#else /* !_WIN64 : Win32 (Win95-class) target stays GDI-only */

typedef int win_d3d11_translation_unit_not_empty;

#endif /* _WIN64 */
