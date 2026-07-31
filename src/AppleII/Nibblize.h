#ifndef NIBBLIZE_H
#define NIBBLIZE_H

#include <stdint.h>

// Converte imagem .dsk (setores crus) para .nib (fluxo de nibbles).
//
// O .dsk guarda 35 trilhas x 16 setores x 256 bytes de dado puro -- nao tem
// address field, nem sync, nem encoding. O Disk II nao le isso: ele le o fluxo
// de nibbles gravado no disco. Entao precisamos "nibblizar":
//
//   por setor fisico:
//     GAP1 (sync FF)
//     campo de endereco : D5 AA 96 + volume/trilha/setor/checksum em 4&4 + DE AA EB
//     GAP2 (sync FF)
//     campo de dados    : D5 AA AD + 342 nibbles em 6&2 + checksum + DE AA EB
//     GAP3 (sync FF)
//
// e aplicar o interleave 2:1 do DOS 3.3 (o setor logico N do arquivo nao fica
// na posicao fisica N do disco).

#define DSK_SIZE        143360   // 35 * 16 * 256
#define NIB_SIZE        232960   // 35 * 6656
#define NIB_TRACK_SIZE  0x1A00   // 6656

// dsk: DSK_SIZE bytes de entrada.  nib: NIB_SIZE bytes de saida.
void nib_fromDsk(const uint8_t *dsk, uint8_t *nib, uint8_t volume = 254);

#endif
