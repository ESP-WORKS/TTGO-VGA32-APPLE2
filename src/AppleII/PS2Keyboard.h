#ifndef PS2KEYBOARD_H
#define PS2KEYBOARD_H

// Ponte para o teclado PS/2 da FabGL.
//
// Este header NAO expoe nenhum tipo da FabGL de proposito: o fabgl.h faz
// "using fabgl::Color;" e colide com o "struct Color" do Predef.h, entao
// Apple2Device.cpp nao pode incluir fabgl.h. Toda a conversa com a FabGL fica
// isolada no PS2Keyboard.cpp.
//
// TTGO VGA32 v1.4: conector PS/2 do teclado = GPIO33 (CLK) / GPIO32 (DATA),
// que sao exatamente os defaults do PS2Preset::KeyboardPort0 da FabGL.

// Inicializa o controlador PS/2 com os pinos informados. Chamar depois do VGA
// estar rodando. Passe clk=33 dat=32 para o comportamento padrao (a FabGL
// configura os pinos sozinha via preset); qualquer outro par usa configuracao
// manual. Os pinos vem do /bootl.rc do SD (ver FileSystem), com fallback 33/32.
void ps2_begin(int clk_pin = 33, int dat_pin = 32);

// Retorna o codigo de tecla do Apple II (0x00-0x7F, SEM o strobe do bit 7),
// ou -1 se nao houver tecla nova.
int ps2_poll();

// Teclas de funcao. Cada uma retorna true UMA vez e se limpa.
bool ps2_helpRequested();    // F1  - tela de ajuda
bool ps2_menuRequested();    // F2  - inserir / trocar disco
bool ps2_exitRequested();    // F7  - sair para o ESP32 Bootloader
bool ps2_colorRequested();   // F11 - monitor color / verde
bool ps2_resetRequested();   // F12      - Ctrl+Reset (warm: cai no BASIC)
bool ps2_coldRequested();    // Ctrl+F12 - liga/desliga (cold: boota o disco)

#endif