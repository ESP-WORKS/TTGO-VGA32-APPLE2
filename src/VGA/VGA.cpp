// VGA.cpp - TTGO VGA32 usando FabGL VGADirectController
//
// Diferenca para a versao com VGAController + Canvas:
//   - Nao existe canvas, nem fila de primitivas, nem setPixel().
//   - Registramos um callback de scanline; a FabGL o chama em ISR para cada
//     linha e nos so fazemos memcpy da linha correspondente do framebuffer.
//   - show() vira no-op: a tela le o framebuffer continuamente.
//
// Formato do framebuffer: 1 byte por pixel, ja no formato RAW da FabGL
// (RGB222 + bits de sync embutidos). Guardado ja "swizzled" (indice ^ 2),
// que e a ordem que o I2S espera, entao o memcpy da linha sai direto.
//
// ATENCAO: este .cpp NAO pode incluir Predef.h — fabgl.h faz
// "using fabgl::Color;" e colide com o "struct Color" de la.
// Por isso blit() recebe void* e usamos AppleColor com o mesmo layout.

#include "VGA.h"
#include <Arduino.h>
#include <fabgl.h>

// Mesmo layout do "struct Color" do Predef.h (3 bytes, sem alpha)
struct AppleColor { unsigned char r, g, b; };

static fabgl::VGADirectController vgaCtrl;

uint8_t *_fb = nullptr;
uint8_t  _rawLUT[64];

// ---------------------------------------------------------------------------
// Callback de scanline: roda em ISR, precisa ser curta e em IRAM.
// ---------------------------------------------------------------------------
static void IRAM_ATTR drawScanline(void *arg, uint8_t *dest, int scanLine)
{
    memcpy(dest, _fb + scanLine * VGA_HRES, VGA_HRES);
}

// ---------------------------------------------------------------------------

VGA::VGA()
{
    bufferCount = 1;
    dmaBuffer   = nullptr;
    usePsram    = false;
    dmaChannel  = 0;
    bits        = 8;
    backBuffer  = 0;
}

VGA::~VGA()
{
    if (_fb) { heap_caps_free(_fb); _fb = nullptr; }
}

void VGA::attachPinToSignal(int, int) {}

bool VGA::init(const PinConfig /*pins*/, const Mode m, int b, int buffercount)
{
    mode        = m;
    bits        = b;
    bufferCount = buffercount;
    backBuffer  = 0;
    dmaBuffer   = nullptr;

    // Framebuffer em RAM INTERNA: o callback roda em ISR e ler PSRAM la
    // e lento e arriscado (o cache pode cair durante acesso ao SPIFFS).
    _fb = (uint8_t *)heap_caps_malloc(VGA_HRES * VGA_VRES,
                                      MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!_fb)
    {
        Serial.println("[VGA] sem RAM interna p/ framebuffer, caindo p/ PSRAM (pode piscar)");
        _fb = (uint8_t *)heap_caps_malloc(VGA_HRES * VGA_VRES,
                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!_fb)
    {
        Serial.println("[VGA] ERRO: falha ao alocar framebuffer");
        return false;
    }

    vgaCtrl.begin();
    vgaCtrl.setDrawScanlineCallback(drawScanline);
    // 320x200: o pixel deste modo nao e quadrado (0,833), o que compensa em
    // parte o esticao horizontal de monitores widescreen. O 320x240 tem pixel
    // quadrado e, num monitor que estica 4:3 para 16:9, fica largo demais.
    //
    // IMPORTANTE: VGA_VRES no VGA.h precisa ser 200. Ele define o tamanho do
    // framebuffer -- com 240 sobram 12,8 KB de RAM INTERNA alocados a toa
    // (e a RAM interna e o recurso mais escasso: o callback roda em ISR e nao
    // pode ler PSRAM).
    vgaCtrl.setResolution(VGA_320x200_70Hz);
    //vgaCtrl.setResolution(QVGA_320x240_60Hz);

    // LUT: 64 cores RGB222 -> pixel RAW (ja com os sync bits)
    for (int i = 0; i < 64; i++)
        _rawLUT[i] = vgaCtrl.createRawPixel(RGB222((i >> 4) & 3, (i >> 2) & 3, i & 3));

    memset(_fb, _rawLUT[0], VGA_HRES * VGA_VRES);   // preto
    return true;
}

bool VGA::start()
{
    // Ja esta rodando desde setResolution()
    return true;
}

bool VGA::show()
{
    // O callback de scanline le _fb direto: nada a copiar.
    return true;
}

// MORTO: desde que DrawPoint/RenderFont passaram a escrever direto no _fb,
// nao existe mais backbuffer (getBackBuffer() devolve NULL) e ninguem chama
// isto. Mantido so para nao mexer no VGA.h.
//
// Converte o backbuffer (Color*, 3 bytes/pixel) direto para o framebuffer.
void VGA::blit(const void *src, int srcW, int srcH)
{
    const AppleColor *base = (const AppleColor *)src;

    int offX = (VGA_HRES - srcW) / 2;   // 20
    int offY = (VGA_VRES - srcH) / 2;   // 24
    if (offX < 0) offX = 0;
    if (offY < 0) offY = 0;
    offX &= ~3;                         // mantem o swizzle ^2 alinhado

    for (int y = 0; y < srcH; y++)
    {
        const AppleColor *row   = base + y * srcW;
        uint8_t          *fbRow = _fb + (y + offY) * VGA_HRES;
        for (int x = 0; x < srcW; x++)
        {
            const AppleColor &c = row[x];
            fbRow[(x + offX) ^ 2] =
                _rawLUT[((c.r >> 6) << 4) | ((c.g >> 6) << 2) | (c.b >> 6)];
        }
    }
}

void VGA::dotdit(int x, int y, uint8_t r, uint8_t g, uint8_t b)
{
    r = (uint8_t)min((int)((rand() & 31) | (r & 0xE0)), 255);
    g = (uint8_t)min((int)((rand() & 31) | (g & 0xE0)), 255);
    b = (uint8_t)min((int)((rand() & 63) | (b & 0xC0)), 255);
    dot(x, y, r, g, b);
}

void VGA::clear(int c)
{
    memset(_fb, (uint8_t)(c & 0xFF), VGA_HRES * VGA_VRES);
}