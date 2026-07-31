#include <string.h>
#include "Nibblize.h"

// Tabela de escrita 6&2: 64 valores de 6 bits -> bytes validos de disco.
// Nenhum deles e 0xD5 ou 0xAA, que e o que garante que os marcadores
// (D5 AA 96 / D5 AA AD) nunca aparecam por acidente dentro do dado.
static const uint8_t wtab[64] = {
	0x96,0x97,0x9A,0x9B,0x9D,0x9E,0x9F,0xA6, 0xA7,0xAB,0xAC,0xAD,0xAE,0xAF,0xB2,0xB3,
	0xB4,0xB5,0xB6,0xB7,0xB9,0xBA,0xBB,0xBC, 0xBD,0xBE,0xBF,0xCB,0xCD,0xCE,0xCF,0xD3,
	0xD6,0xD7,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE, 0xDF,0xE5,0xE6,0xE7,0xE9,0xEA,0xEB,0xEC,
	0xED,0xEE,0xEF,0xF2,0xF3,0xF4,0xF5,0xF6, 0xF7,0xF9,0xFA,0xFB,0xFC,0xFD,0xFE,0xFF
};

// inversao dos 2 bits baixos (o RWTS le nessa ordem)
static const uint8_t bitrev2[4] = { 0, 2, 1, 3 };

// Interleave do DOS 3.3: setor fisico -> setor logico dentro do arquivo .dsk.
// (Para imagens .po, ordem ProDOS, a tabela seria outra.)
static const uint8_t phys2log[16] = { 0,7,14,6,13,5,12,4,11,3,10,2,9,1,8,15 };

// 16 + 14 + 8 + 349 + 29 = 416 ; 416 * 16 = 6656 = NIB_TRACK_SIZE
#define GAP1  16
#define GAP2   8
#define GAP3  29

void nib_fromDsk(const uint8_t *dsk, uint8_t *nib, uint8_t volume)
{
	for (int track = 0; track < 35; track++)
	{
		uint8_t *p = nib + track * NIB_TRACK_SIZE;
		const uint8_t *dskTrack = dsk + track * 16 * 256;

		for (int sec = 0; sec < 16; sec++)
		{
			// ---- GAP1 ----
			memset(p, 0xFF, GAP1); p += GAP1;

			// ---- campo de endereco (4&4) ----
			*p++ = 0xD5; *p++ = 0xAA; *p++ = 0x96;
			uint8_t chk = volume ^ (uint8_t)track ^ (uint8_t)sec;
			uint8_t f[4] = { volume, (uint8_t)track, (uint8_t)sec, chk };
			for (int i = 0; i < 4; i++) {
				*p++ = (uint8_t)((f[i] >> 1) | 0xAA);   // bits impares
				*p++ = (uint8_t)( f[i]       | 0xAA);   // bits pares
			}
			*p++ = 0xDE; *p++ = 0xAA; *p++ = 0xEB;

			// ---- GAP2 ----
			memset(p, 0xFF, GAP2); p += GAP2;

			// ---- campo de dados (6&2) ----
			*p++ = 0xD5; *p++ = 0xAA; *p++ = 0xAD;

			const uint8_t *page = dskTrack + phys2log[sec] * 256;

			uint8_t nbuf[342];
			memset(nbuf, 0, 86);
			for (int i = 0; i < 256; i++) {
				nbuf[86 + i] = page[i] >> 2;                              // 6 bits altos
				nbuf[i % 86] |= bitrev2[page[i] & 3] << ((i / 86) * 2);   // 2 bits baixos
			}

			uint8_t last = 0;
			for (int i = 0; i < 342; i++) {
				*p++ = wtab[nbuf[i] ^ last];    // XOR encadeado
				last = nbuf[i];
			}
			*p++ = wtab[last];                  // checksum

			*p++ = 0xDE; *p++ = 0xAA; *p++ = 0xEB;

			// ---- GAP3 ----
			memset(p, 0xFF, GAP3); p += GAP3;
		}
	}
}
