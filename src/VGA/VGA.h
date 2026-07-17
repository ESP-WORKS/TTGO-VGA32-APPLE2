#ifndef VGA_H
#define VGA_H

// VGA.h - TTGO VGA32 usando FabGL VGADirectController
// Interface publica compativel com o original ESP32-S3.
// dot() e rgb() sao inline: escrevem direto no framebuffer, sem overhead.
//
// IMPORTANTE: este header NAO inclui fabgl.h nem Predef.h.
// fabgl.h faz "using fabgl::Color;" e colide com o "struct Color" do Predef.h,
// entao os dois nunca podem aparecer na mesma unidade de compilacao.

#include <stdint.h>
#include "PinConfig.h"
#include "Mode.h"

class DMAVideoBuffer;   // stub, nunca instanciado

#define VGA_HRES 320
#define VGA_VRES 240

// Framebuffer raw (1 byte/pixel, ja com sync bits) e LUT de cor.
// Definidos em VGA.cpp.
extern uint8_t *_fb;
extern uint8_t  _rawLUT[64];

class VGA
{
public:
    Mode        mode;
    int         bufferCount;
    int         bits;
    PinConfig   pins;
    int         backBuffer;
    DMAVideoBuffer *dmaBuffer;   // sempre nullptr
    bool        usePsram;
    int         dmaChannel;

public:
    VGA();
    ~VGA();

    bool init(const PinConfig pins, const Mode mode, int bits, int buffercount);
    bool start();
    bool show();
    void clear(int rgb = 0);
    void dotdit(int x, int y, uint8_t r, uint8_t g, uint8_t b);

    // Caminho rapido: converte o backbuffer inteiro de uma vez.
    // src aponta para um array de Color {uint8_t r, g, b} (3 bytes por pixel).
    // Recebe void* para nao precisar da definicao de Color aqui.
    void blit(const void *src, int srcW, int srcH);

    // --- inline: zero overhead por pixel ---

    // Retorna o pixel RAW da FabGL (RGB222 + sync bits), nao RGB565.
    inline int rgb(uint8_t r, uint8_t g, uint8_t b)
    {
        return _rawLUT[((r >> 6) << 4) | ((g >> 6) << 2) | (b >> 6)];
    }

    inline void dot(int x, int y, int c)
    {
        if ((unsigned)x >= VGA_HRES || (unsigned)y >= VGA_VRES) return;
        _fb[y * VGA_HRES + (x ^ 2)] = (uint8_t)c;
    }

    inline void dot(int x, int y, uint8_t r, uint8_t g, uint8_t b)
    {
        dot(x, y, rgb(r, g, b));
    }

protected:
    void attachPinToSignal(int pin, int signal);   // nao usada
};

#endif // VGA_H