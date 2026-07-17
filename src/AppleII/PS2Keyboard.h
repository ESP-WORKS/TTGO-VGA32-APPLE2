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

// Inicializa o controlador PS/2. Chamar depois do VGA estar rodando.
void ps2_begin();

// Retorna o codigo de tecla do Apple II (0x00-0x7F, SEM o strobe do bit 7),
// ou -1 se nao houver tecla nova.
int ps2_poll();

// Teclas de funcao. Cada uma retorna true UMA vez e se limpa.
bool ps2_helpRequested();    // F1  - tela de ajuda
bool ps2_menuRequested();    // F2  - inserir / trocar disco
bool ps2_colorRequested();   // F11 - monitor color / verde
bool ps2_resetRequested();   // F12 - Reset (Ctrl+Reset do Apple II)

#endif