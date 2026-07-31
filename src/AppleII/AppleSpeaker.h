#ifndef APPLESPEAKER_H
#define APPLESPEAKER_H

// Ponte para o speaker do Apple II -> saida de audio da FabGL.
//
// Como o PS2Keyboard.h, este header nao expoe nenhum tipo da FabGL: fabgl.h faz
// "using fabgl::Color;" e colide com o "struct Color" do Predef.h.
//
// O speaker do Apple II e de 1 bit: ler/escrever $C030 inverte a posicao do cone.
// Todo o som vem do INTERVALO entre esses toggles. O problema e que o emulador
// roda 17050 ciclos numa rajada de ~7ms e depois dorme -- se chavearmos um pino
// direto no $C030 o tom sai ~2,4x agudo e picotado.
//
// Por isso gravamos o TICK de cada toggle e, uma vez por frame, convertemos
// isso em PCM espalhado no tempo correto (64 ciclos por sample = 15984 Hz).
//
// TTGO VGA32: saida de audio no GPIO25 (DAC interno), que e o default da FabGL.

// Inicializa a saida de audio. Chamar depois do VGA estar rodando.
void spk_begin();

// Chamado do $C030: registra um toggle do cone no instante 'tick'.
void spk_toggle(long long tick);

// Chamado 1x por frame: gera os samples de tudo que aconteceu ate 'tick'.
void spk_render(long long tick);

#endif
