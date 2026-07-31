#include "Predef.h"
#include "AppleCPU.h"
#include "AppleMem.h"
#include "Apple2device.h"
#include "AppleFont.h"
#include "../VGA/VGA.h"     // _fb e _rawLUT
#include "PS2Keyboard.h"    // ponte para o teclado PS/2 (isolada do fabgl.h)
#include "Nibblize.h"       // conversao .dsk -> .nib
#include "AppleSpeaker.h"   // speaker $C030 (isolado do fabgl.h)

// Escreve um pixel do Apple II (280x192) centralizado no framebuffer 320x240.
// Formato: 1 byte RAW (RGB222 + sync), RAM interna, indice ^2 (byte swap do I2S).
// Elimina o backbuffer Color* de 161KB que ficava na PSRAM.
// Modo de video: VGA_320x200_70Hz. O pixel dele nao e quadrado (0,833), o que
// compensa em parte o esticao horizontal de monitores widescreen -- por isso
// fica melhor que o 320x240, cujo pixel e quadrado.
//
// Em 320x200 a tela do Apple II (280x192) deixa apenas 8 linhas sobrando.
// Centralizar daria 4 em cima e 4 embaixo: fino demais para o indicador e feio
// no topo. Entao COLAMOS NO TOPO e usamos as 8 linhas de baixo para o "DISK".
#define VGA_LINES  VGA_VRES

#define FB_OFFX  ((VGA_HRES - SCREENSIZE_X) / 2)   // 20
#define FB_OFFY  ((VGA_VRES - SCREENSIZE_Y) / 2)   // (240-192)/2 = 24

static inline void fbPoint(int x, int y, uint8_t raw)
{
	if ((unsigned)x >= SCREENSIZE_X || (unsigned)y >= SCREENSIZE_Y) return;
	_fb[(y + FB_OFFY) * VGA_HRES + ((x + FB_OFFX) ^ 2)] = raw;
}

// Escrita crua no framebuffer 320x240, SEM o clamp do Apple II.
// Usada para desenhar na borda (fora da area 280x192 do emulador).
static inline void fbRaw(int x, int y, uint8_t raw)
{
	if ((unsigned)x >= VGA_HRES || (unsigned)y >= VGA_LINES) return;
	_fb[y * VGA_HRES + (x ^ 2)] = raw;
}

// Mini-fonte 5x7 para o indicador "DISK". Bit 4 = coluna da esquerda.
static const uint8_t glyphDISK[4][7] = {
	{ 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E },   // D
	{ 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E },   // I
	{ 0x0E, 0x11, 0x10, 0x0E, 0x01, 0x11, 0x0E },   // S
	{ 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 },   // K
};

// Canto inferior direito, nas 8 linhas que sobram (a tela do Apple vai ate y=191).
// O glifo tem 7 pixels de altura: 192..198 cabe sem invadir a area do Apple.
#define DISK_X   292
#define DISK_Y   229

// ---- instrumentacao do disco (lida pelo main.cpp) ----
unsigned long g_diskReads = 0;   // nibbles entregues por segundo
unsigned long g_diskTrack = 0;   // trilha atual
unsigned long g_diskNib   = 0;   // posicao do nibble
unsigned long g_cpuTick   = 0;   // tick da CPU
unsigned long g_diskSkips = 0;   // vezes que o disco "girou" durante processamento

// ---------------------------------------------------------------------------
// DISK_TRACE
//   Imprime, a cada troca de trilha, quanto tempo ela levou e quantos nibbles
//   foram lidos. Referencia de um Apple II real com DOS 3.3 e interleave 2:1:
//   uma trilha de 16 setores sai em ~2 voltas (~13.300 nibbles) em ~400 ms.
//
//   Muitas voltas  -> o DOS esta dando volta atras de setor (interleave/timing)
//   Poucas voltas mas lento -> o gargalo nao e o disco, e a CPU emulada
// ---------------------------------------------------------------------------
#define DISK_TRACE  1

// ---------------------------------------------------------------------------
// DISK_TIME_BASED
//
//   0 = original: o nibble avanca A CADA LEITURA de $C0EC.
//       O disco "congela" enquanto o DOS decodifica o setor, o que quebra o
//       interleave 2:1 do DOS 3.3 -> ~11 voltas por trilha.
//
//   1 = puro por tempo: nibble = (tick / 32) % 0x1A00.
//       Fiel ao hardware, MAS exige CPU cycle-accurate. Este core nao e
//       (ha penalidades de page-crossing comentadas no AppleCpu.cpp), entao
//       nibbles se perdem, o checksum falha e o RWTS re-le pra sempre.
//
//   2 = HIBRIDO (recomendado): avanca 1 nibble por leitura -- nunca perde dado,
//       imune a imprecisao de ciclo -- mas quando houve um intervalo longo sem
//       leitura (o DOS decodificando), pula os nibbles que passaram nesse tempo.
//       E so isso que o interleave precisa: o disco continuar girando durante o
//       processamento.
// ---------------------------------------------------------------------------
#define DISK_TIME_BASED          2
#define DISK_CYCLES_PER_NIBBLE   32
#define DISK_SPIN_GAP            64   // ciclos: acima disso, o DOS saiu do loop de leitura

static WORD      lastNib  = 0xFFFF;
static long long lastTick = 0;

/////////////////////////////////////////////////////////////////////////// 

const int offsetGR[24] = {                                                    // helper for TEXT and GR video generation
  0x000, 0x080, 0x100, 0x180, 0x200, 0x280, 0x300, 0x380,                     // lines 0-7
  0x028, 0x0A8, 0x128, 0x1A8, 0x228, 0x2A8, 0x328, 0x3A8,                     // lines 8-15
  0x050, 0x0D0, 0x150, 0x1D0, 0x250, 0x2D0, 0x350, 0x3D0 };                    // lines 16-23


const int offsetHGR[192] = {                                                  // helper for HGR video generation
	0x0000, 0x0400, 0x0800, 0x0C00, 0x1000, 0x1400, 0x1800, 0x1C00,             // lines 0-7
	0x0080, 0x0480, 0x0880, 0x0C80, 0x1080, 0x1480, 0x1880, 0x1C80,             // lines 8-15
	0x0100, 0x0500, 0x0900, 0x0D00, 0x1100, 0x1500, 0x1900, 0x1D00,             // lines 16-23
	0x0180, 0x0580, 0x0980, 0x0D80, 0x1180, 0x1580, 0x1980, 0x1D80,
	0x0200, 0x0600, 0x0A00, 0x0E00, 0x1200, 0x1600, 0x1A00, 0x1E00,
	0x0280, 0x0680, 0x0A80, 0x0E80, 0x1280, 0x1680, 0x1A80, 0x1E80,
	0x0300, 0x0700, 0x0B00, 0x0F00, 0x1300, 0x1700, 0x1B00, 0x1F00,
	0x0380, 0x0780, 0x0B80, 0x0F80, 0x1380, 0x1780, 0x1B80, 0x1F80,
	0x0028, 0x0428, 0x0828, 0x0C28, 0x1028, 0x1428, 0x1828, 0x1C28,
	0x00A8, 0x04A8, 0x08A8, 0x0CA8, 0x10A8, 0x14A8, 0x18A8, 0x1CA8,
	0x0128, 0x0528, 0x0928, 0x0D28, 0x1128, 0x1528, 0x1928, 0x1D28,
	0x01A8, 0x05A8, 0x09A8, 0x0DA8, 0x11A8, 0x15A8, 0x19A8, 0x1DA8,
	0x0228, 0x0628, 0x0A28, 0x0E28, 0x1228, 0x1628, 0x1A28, 0x1E28,
	0x02A8, 0x06A8, 0x0AA8, 0x0EA8, 0x12A8, 0x16A8, 0x1AA8, 0x1EA8,
	0x0328, 0x0728, 0x0B28, 0x0F28, 0x1328, 0x1728, 0x1B28, 0x1F28,
	0x03A8, 0x07A8, 0x0BA8, 0x0FA8, 0x13A8, 0x17A8, 0x1BA8, 0x1FA8,
	0x0050, 0x0450, 0x0850, 0x0C50, 0x1050, 0x1450, 0x1850, 0x1C50,
	0x00D0, 0x04D0, 0x08D0, 0x0CD0, 0x10D0, 0x14D0, 0x18D0, 0x1CD0,
	0x0150, 0x0550, 0x0950, 0x0D50, 0x1150, 0x1550, 0x1950, 0x1D50,
	0x01D0, 0x05D0, 0x09D0, 0x0DD0, 0x11D0, 0x15D0, 0x19D0, 0x1DD0,
	0x0250, 0x0650, 0x0A50, 0x0E50, 0x1250, 0x1650, 0x1A50, 0x1E50,
	0x02D0, 0x06D0, 0x0AD0, 0x0ED0, 0x12D0, 0x16D0, 0x1AD0, 0x1ED0,             // lines 168-183
	0x0350, 0x0750, 0x0B50, 0x0F50, 0x1350, 0x1750, 0x1B50, 0x1F50,             // lines 176-183
	0x03D0, 0x07D0, 0x0BD0, 0x0FD0, 0x13D0, 0x17D0, 0x1BD0, 0x1FD0 };            // lines 184-191


const int color[16][3] = {                                                    // the 16 low res colors
	{ 0,   0,   0	  }, { 226, 57,  86  }, { 28,  116, 205 }, { 126, 110, 173 },
	{ 31,  129, 128 }, { 137, 130, 122 }, { 86,  168, 228 }, { 144, 178, 223 },
	{ 151, 88,  34	}, { 234, 108, 21  }, { 158, 151, 143 }, { 255, 206, 240 },
	{ 144, 192, 49	}, { 255, 253, 166 }, { 159, 210, 213 }, { 255, 255, 255 }
};

// Cores do HiRes.
//
// Original do autor: os indices 8-15 (pixels impares) usavam as cores pela
// METADE do brilho ("one pixel every two is darker"), para imitar a luminancia
// do NTSC. Em RGB888 isso e uma sombra sutil; na TTGO VGA32 temos RGB222, e o
// >>6 transforma essa sombra em liga/desliga -> listras verticais.
//
// Aqui mantemos a TROCA DE MATIZ entre par/impar (que e o que o HGR realmente
// faz: bit ligado = violeta na coluna par, verde na impar) mas com brilho cheio
// nos dois. Resultado: areas de cor solida ficam solidas.
//
// Para voltar ao comportamento original, use os valores em comentario.
const int hcolor[16][3] = {
	{ 0,   0,   0   }, { 144, 192, 49  }, { 126, 110, 173 }, { 255, 255, 255 },   // par,   colorSet 0
	{ 0,   0,   0   }, { 234, 108, 21  }, { 86,  168, 228 }, { 255, 255, 255 },   // par,   colorSet 1
	{ 0,   0,   0   }, { 126, 110, 173 }, { 144, 192, 49  }, { 255, 255, 255 },   // impar, colorSet 0  (era { 63,55,86 } e { 72,96,25 })
	{ 0,   0,   0   }, { 86,  168, 228 }, { 234, 108, 21  }, { 255, 255, 255 }    // impar, colorSet 1  (era { 43,84,114 } e { 117,54,10 })
};


Apple2Device::Apple2Device()
{
	DEBUG_PRINTLN("Construct Apple2Device");
	backbuffer = NULL;
	Reset();
}

Apple2Device::~Apple2Device()
{
	if (backbuffer != NULL)
		free(backbuffer);
}

void Apple2Device::Create(CPU* cpu)
{
	this->cpu = cpu;
	font.Create();
	zoomscale = 3;

	// Teclado PS/2 (GPIO33=CLK, GPIO32=DATA na TTGO VGA32).
	// Chamado aqui porque Create() roda depois do VGA ja estar de pe.
	ps2_begin();

	// Audio (GPIO25, DAC interno)
	spk_begin();

	// backbuffer removido: DrawPoint/RenderFont escrevem direto no _fb da VGA.
	// Economiza 161KB de PSRAM e elimina o blit por completo.
	backbuffer = NULL;
	ClearScreen();
/*
	renderImage.data = backbuffer;
	renderImage.width = SCREENSIZE_X;
	renderImage.height = SCREENSIZE_Y;
	renderImage.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
	renderImage.mipmaps = 1;
	renderTexture = LoadTextureFromImage(renderImage);
*/

	//////////////////////////////////////////////////////////////////////////
}

void Apple2Device::Reset()
{
	loaddumpmachine = false;
	dumpMachine = false;
	loadromfile = false;

	resetMachine = false;
	diskMenuRequested = false;
	helpRequested = false;
	coldBootRequested = false;
	exitRequested = false;
	colorMonitor = true;
	keyboard = 0;

	pixelGR = { 0, 0, 7, 4 };

	// DISK ][
	updatedrive = 0;
	currentDrive = 0;
	// I/O register
	dLatch = 0;

	memset(phases, 0, sizeof(phases));
	memset(phasesB, 0, sizeof(phasesB));
	memset(phasesBB, 0, sizeof(phasesBB));
	memset(pIdx, 0, sizeof(pIdx));
	memset(pIdxB, 0, sizeof(pIdxB));
	memset(halfTrackPos, 0, sizeof(halfTrackPos));

	////////////////////////////////////////////////////////////////////////// VIDEO

	textMode = true;
	mixedMode = false;
	videoPage = 1;
	hires_Mode = false;

	//videoAddress = videoPage * 0x0400;
	memset(LoResCache, 0, sizeof(LoResCache));
	memset(HiResCache, 0, sizeof(HiResCache));
	memset(TextCache, -1, sizeof(TextCache));      // -1 = nunca desenhado
	memset(previousBit, 0, sizeof(previousBit));
	flashCycle = 0;
}


BYTE Apple2Device::SoftSwitch(Memory *mem, WORD address, BYTE value, bool WRT)
{
	switch (address) 
	{
		// KEYBOARD
		case 0xC000: 
			return(keyboard);
		// KBDSTROBE
		case 0xC010: 
			keyboard &= 0x7F; 
			return(keyboard);

		// TAPEOUT??
		case 0xC020:
			break;

		///////////////////////////////////////////////////////////////////////////////// Botoes / Paddles
		//
		// IMPORTANTE: estes casos precisam existir mesmo sem joystick implementado.
		// Sem eles a leitura cai no floating bus do fim da funcao (cpu->tick % 256),
		// e bit 7 setado em $C061/$C062 significa BOTAO PRESSIONADO.
		// Enquanto o tick era 0 isso passava despercebido (retornava sempre 0);
		// com o tick funcionando, os botoes passam a "apertar" sozinhos.

		// Open Apple / Closed Apple / Shift-key mod (PB0, PB1, PB2)
		case 0xC061:
		case 0xC062:
		case 0xC063:
			return 0;              // nenhum botao pressionado

		// Paddles 0-3: bit 7 setado enquanto o timer nao estourou.
		// Sem joystick, timer sempre estourado -> 0.
		case 0xC064:
		case 0xC065:
		case 0xC066:
		case 0xC067:
			return 0;

		// PDLTRIG - dispara a contagem dos paddles
		case 0xC070:
			return 0;

		///////////////////////////////////////////////////////////////////////////////// Speaker

		case 0xC030: // SPEAKER
		//case 0xC033: 
			PlaySound(); 
			break;

		///////////////////////////////////////////////////////////////////////////////// Graphics

		case 0xC050: 
			textMode = false; 
			//printf("Text Mode Off\n");
			break;
		// Text
		case 0xC051: 
			textMode = true;  
			//printf("Text Mode On\n");
			break;

		// Mixed off
		case 0xC052: 
			mixedMode = false; 
			//printf("Mixed Mode Off\n");
			break;

		// Mixed on
		case 0xC053: 
			mixedMode = true;  
			//printf("Mixed Mode On\n");
			break;

		// Page 1
		case 0xC054: 
			videoPage = 1;
			//printf("Video Page 1\n");
			break;
		// Page 2
		case 0xC055: 
			videoPage = 2;
			//printf("Video Page 2\n");
			break;

		// HiRes off
		case 0xC056: 
			hires_Mode = false; 
			//printf("HIRES Mode Off\n");
			break;
		// HiRes on
		case 0xC057: 
			hires_Mode = true;  
			//printf("HIRES Mode On\n");
			break;

		/////////////////////////////////////////////////////////////////////////////////	Joy Paddle ?

/*
	https://apple2.org.za/gswv/a2zine/faqs/csa2pfaq.html

	These are actually the first two game Pushbutton inputs (PB0
	and PB1) which are borrowed by the Open Apple and Closed Apple
	keys. Bit 7 is set (=1) in these locations if the game switch or
	corresponding key is pressed.

	PB2 =      $C063 ;game Pushbutton 2 (read)
	This input has an option to be connected to the shift key on
	the keyboard. (See info on the 'shift key mod'.)

	PADDLE0 =  $C064 ;bit 7 = status of pdl-0 timer (read)
	PADDLE1 =  $C065 ;bit 7 = status of pdl-1 timer (read)
	PADDLE2 =  $C066 ;bit 7 = status of pdl-2 timer (read)
	PADDLE3 =  $C067 ;bit 7 = status of pdl-3 timer (read)
	PDLTRIG =  $C070 ;trigger paddles
	Read this to start paddle countdown, then time the period until
	$C064-$C067 bit 7 becomes set to determine the paddle position.
	This takes up to three milliseconds if the paddle is at its maximum
	extreme (reading of 255 via the standard firmware routine).

	SETIOUDIS= $C07E ;enable DHIRES & disable $C058-5F (W)
	CLRIOUDIS= $C07E ;disable DHIRES & enable $C058-5F (W)

*/
/*
		// Push Button 0
		case 0xC061: 
		{
			if (gamepad.pressbtn2)
				return 0x80;
			else
				return 0;
		}
		// Push Button 1
		case 0xC062: 
		{
			if (gamepad.pressbtn1)
				return 0x80;
			else
				return 0;
		}

// 		// Push Button 2
// 		case 0xC063: 
// 			return 0;

		// Paddle 0
		case 0xC064: 
		{
			BYTE v = readPaddle(0);
			return(v);
		}

		// Paddle 1
		case 0xC065: 
		{
			BYTE v = readPaddle(1);
			return(v);
		}

		// paddle timer RST
		case 0xC070: 
			resetPaddles(); 
			break;
*/
		/////////////////////////////////////////////////////////////////////////////////	DISK 2

		case 0xC0E0:
		case 0xC0E1:
		case 0xC0E2:
		case 0xC0E3:
		case 0xC0E4:
		case 0xC0E5:
		case 0xC0E6:
		case 0xC0E7: 
			stepMotor(address); 
			break; // MOVE DRIVE HEAD

		// MOTOR OFF
		case 0xCFFF:
		case 0xC0E8: 
			disk[currentDrive].motorOn = false; 
			//printf("--> DISK MOTOR OFF\n");
			break;

		// MOTOR ON
		case 0xC0E9: 
			disk[currentDrive].motorOn = true;  
			//printf("--> DISK MOTOR ON\n");
			break;

		// DRIVE 0
		case 0xC0EA: 
			setDrv(0); 
			break;
		// DRIVE 1
		case 0xC0EB: 
			setDrv(1); 
			break;

		// Shift Data Latch
		case 0xC0EC:                                                                
		{
#if DISK_TIME_BASED == 1
			// --- puro por tempo (exige cycle accuracy; provavelmente quebra) ---
			WORD nib = (WORD)((cpu->tick / DISK_CYCLES_PER_NIBBLE) % 0x1A00);
			if (nib == lastNib)
				return 0;          // nibble ainda montando: bit 7 limpo -> BPL espera
			lastNib = nib;
			disk[currentDrive].nibble = nib;

			if (disk[currentDrive].writeMode)
				disk[currentDrive].data[disk[currentDrive].track * 0x1A00 + nib] = dLatch;
			else
				dLatch = disk[currentDrive].data[disk[currentDrive].track * 0x1A00 + nib];

#elif DISK_TIME_BASED == 2
			// --- hibrido ---
			// 1) le o nibble atual (sempre valido: nunca perdemos dado)
			if (disk[currentDrive].writeMode)
				disk[currentDrive].data[disk[currentDrive].track * 0x1A00 + disk[currentDrive].nibble] = dLatch;
			else
				dLatch = disk[currentDrive].data[disk[currentDrive].track * 0x1A00 + disk[currentDrive].nibble];

			// 2) avanca. Em leitura sequencial (gap curto) anda 1, igual ao original.
			//    Se houve gap longo, o disco girou nesse tempo: pula os nibbles.
			//    O cpu.Reset() zera o tick, entao ele pode andar para tras.
			if (cpu->tick < lastTick)
				lastTick = cpu->tick;
			long long elapsed = cpu->tick - lastTick;
			lastTick = cpu->tick;

			WORD step = 1;
			if (elapsed >= DISK_SPIN_GAP)
			{
				long long s = elapsed / DISK_CYCLES_PER_NIBBLE;
				if (s < 1)      s = 1;
				step = (WORD)(s % 0x1A00);
				if (step == 0)  step = 1;
				g_diskSkips++;
			}
			disk[currentDrive].nibble = (disk[currentDrive].nibble + step) % 0x1A00;

#else
			// --- original: 1 nibble por leitura, disco congela no processamento ---
			if (disk[currentDrive].writeMode)
				disk[currentDrive].data[disk[currentDrive].track * 0x1A00 + disk[currentDrive].nibble] = dLatch;
			else
				dLatch = disk[currentDrive].data[disk[currentDrive].track * 0x1A00 + disk[currentDrive].nibble];

			disk[currentDrive].nibble = (disk[currentDrive].nibble + 1) % 0x1A00;
#endif
			g_diskReads++;
			g_diskTrack = disk[currentDrive].track;
			g_diskNib   = disk[currentDrive].nibble;

#if DISK_TRACE
			{
				static int           trkAtual = -1;
				static unsigned long trkT0    = 0;
				static unsigned long trkNibs  = 0;

				int t = disk[currentDrive].track;
				if (t != trkAtual)
				{
					if (trkAtual >= 0)
					{
						unsigned long dt = millis() - trkT0;
						Serial.printf("[DISK] trilha %2d : %5lu ms  %6lu nibbles  %5.1f voltas\n",
						              trkAtual, dt, trkNibs, trkNibs / 6656.0f);
					}
					trkAtual = t;
					trkT0    = millis();
					trkNibs  = 0;
				}
				trkNibs++;
			}
#endif
		}
		return(dLatch);

		// Load Data Latch
		case 0xC0ED: 
			dLatch = value; 
			break;

		// latch for READ
		case 0xC0EE:
			disk[currentDrive].writeMode = false;
			return(disk[currentDrive].readOnly ? 0x80 : 0);                                 // check protection

		// latch for WRITE
		case 0xC0EF: 
			disk[currentDrive].writeMode = true; 
			break;

		///////////////////////////////////////////////////////////////////////////////// LANGUAGE CARD

		case 0xC080: 
		case 0xC084: 
			mem->LCBank2Enable = 1; 
			mem->LCReadable = 1; 
			mem->LCWritable = 0;
			mem->LCPreWriteFlipflop = 0;    
			break;       // LC2RD

		case 0xC081:
		case 0xC085: 
			mem->LCBank2Enable = 1;
			mem->LCReadable = 0; 
			mem->LCWritable |= mem->LCPreWriteFlipflop; 
			mem->LCPreWriteFlipflop = !WRT; 
			break;       // LC2WR

		case 0xC082:
		case 0xC086: 
			mem->LCBank2Enable = 1;
			mem->LCReadable = 0;
			mem->LCWritable = 0;
			mem->LCPreWriteFlipflop = 0;    
			break;       // ROMONLY2

		case 0xC083:
		case 0xC087: 
			mem->LCBank2Enable = 1;
			mem->LCReadable = 1;
			mem->LCWritable |= mem->LCPreWriteFlipflop; 
			mem->LCPreWriteFlipflop = !WRT; 
			break;       // LC2RW

		case 0xC088:
		case 0xC08C: 
			mem->LCBank2Enable = 0;
			mem->LCReadable = 1; 
			mem->LCWritable = 0;
			mem->LCPreWriteFlipflop = 0;    
			break;       // LC1RD

		case 0xC089:
		case 0xC08D: 
			mem->LCBank2Enable = 0; 
			mem->LCReadable = 0; 
			mem->LCWritable |= mem->LCPreWriteFlipflop; 
			mem->LCPreWriteFlipflop = !WRT; 
			break;       // LC1WR

		case 0xC08A:
		case 0xC08E: 
			mem->LCBank2Enable = 0; 
			mem->LCReadable = 0; 
			mem->LCWritable = 0; 
			mem->LCPreWriteFlipflop = 0;    
			break;       // ROMONLY1

		case 0xC08B:
		case 0xC08F: 
			mem->LCBank2Enable = 0; mem->LCReadable = 1; 
			mem->LCWritable |= mem->LCPreWriteFlipflop; 
			mem->LCPreWriteFlipflop = !WRT; 
			break;       // LC1RW
	}

	return (cpu->tick % 256);
}

void Apple2Device::ClearScreen()
{
	const uint8_t raw = _rawLUT[0];   // preto
	for (int y = 0; y < SCREENSIZE_Y; y++)
		for (int x = 0; x < SCREENSIZE_X; x++)
			fbPoint(x, y, raw);
}

// "DISK" na borda inferior direita enquanto o motor do drive estiver ligado.
// So redesenha quando o estado muda, entao custa ~zero por frame.
void Apple2Device::DrawDiskIndicator()
{
	static int lastState = -1;
	int on = disk[currentDrive].motorOn ? 1 : 0;
	if (on == lastState)
		return;
	lastState = on;

	// RGB222(3,0,0) = vermelho -> indice 48 ; apagado = preto
	const uint8_t raw = on ? _rawLUT[3 << 4] : _rawLUT[0];

	for (int ch = 0; ch < 4; ch++)
		for (int y = 0; y < 7; y++)
		{
			uint8_t bits = glyphDISK[ch][y];
			for (int x = 0; x < 5; x++)
				if (bits & (0x10 >> x))
					fbRaw(DISK_X + ch * 6 + x, DISK_Y + y, raw);
		}
}

// Escreve texto usando a fonte do Apple II, em coordenadas de caractere
// (40x24). Usa a instancia 'font' que ja existe -- nao aloca outra.
void Apple2Device::DrawText(int col, int line, const char *s, bool inv)
{
	for (int i = 0; s[i] && (col + i) < SCREENTEXT_X; i++)
	{
		uint8_t g = (uint8_t)s[i];
		if (g >= 'a' && g <= 'z') g -= 32;          // Apple II+ so tem maiuscula
		if (g < 0x20 || g > 0x5F) g = 0x20;
		font.RenderFont(NULL, g, (col + i) * FONT_X, line * FONT_Y, inv);
	}
}

void Apple2Device::DrawPoint(int x, int y, int r, int g, int b)
{
	uint8_t idx;
	if (colorMonitor)
	{
		idx = (uint8_t)(((r >> 6) << 4) | ((g >> 6) << 2) | (b >> 6));
	}
	else
	{
		// monitor monocromatico verde
		int gray = ((77 * r) + (150 * g) + (29 * b)) >> 8;   // sem float
		idx = (uint8_t)((gray >> 6) << 2);
	}

	fbPoint(x, y, _rawLUT[idx]);
}

void Apple2Device::DrawRect(_RECT rect, int r, int g, int b)
{
	for (int y = 0; y < (int)rect.height; y++)
		for (int x = 0; x < (int)rect.width; x++)
		{
			DrawPoint((int)rect.x + x, (int)rect.y + y, r, g, b);
		}
}

int Apple2Device::GetScreenMode()
{
	if (mixedMode == false)
	{
		if (textMode == false && hires_Mode)
			return HIRES_MODE;

		if (textMode == false && hires_Mode == false)
			return LORES_MODE;

		if (textMode == true && hires_Mode == false)
			return TEXT_MODE;
	}
	else
	{
		if (hires_Mode)
			return HIRES_MIX_MODE;
		else
			return LORES_MIX_MODE;
	}

	return TEXT_MODE;
}

/*
	TEXT 40x24 ( 7x8 Font )
	LORES : 40x24 (MIX 40x20)
	HIRES : 280×192 (MIX 280×160)
	MIX일경우에 하단은 TEXT( 4Line : 32 pixel )
*/
void Apple2Device::Render(Memory &mem, int frame)
{
	int screenmode = GetScreenMode();

	// video Page에 따라 Address가 달라짐
	// $400, $800, $2000, $4000
	if (screenmode == LORES_MODE || screenmode == HIRES_MODE || 
		screenmode == LORES_MIX_MODE || screenmode == HIRES_MIX_MODE)
	{
		// LoRes 저해상도
		if (hires_Mode == false)
		{
			videoAddress = videoPage * 0x0400;
			BYTE glyph;                                                            // 2 blocks in GR
			BYTE colorIdx = 0;                                                     // to index the color arrays

			// for each column
			for (int col = 0; col < 40; col++) 
			{
				pixelGR.x = col * 7;
				// Mixmode이면 하단 4라인은 Text용
				for (int line = 0; line < (mixedMode ? 20 : 24); line++) 
				{
					pixelGR.y = line * 8;                                                 // first block

					glyph = mem.ReadByte(videoAddress + offsetGR[line] + col);                         // read video memory

					if (LoResCache[line][col] != glyph || !flashCycle) 
					{
						LoResCache[line][col] = glyph;

						// first nibble(4bit) 1/2 Byte
						colorIdx = glyph & 0x0F;
						DrawRect(pixelGR, color[colorIdx][0], color[colorIdx][1], color[colorIdx][2]);

						pixelGR.y += 4;                                                       // second block
						colorIdx = (glyph & 0xF0) >> 4;                                       // second nibble
						DrawRect(pixelGR, color[colorIdx][0], color[colorIdx][1], color[colorIdx][2]);
					}
				}
			}
		}
		else
		{
			// highRes 고해상도
			WORD word;
			BYTE bits[16], bit, pbit, colorSet, even;
			// PAGE is 1 or 2
			videoAddress = videoPage * 0x2000;
			BYTE colorIdx = 0;

			// Mixmode이면 하단 4라인은 Text용
			for (int line = 0; line < (mixedMode ? 160 : 192); line++)
			{
				// for every 7 horizontal dots
				for (int col = 0; col < 40; col += 2) 
				{
					int x = col * 7;
					even = 0;

					word = (WORD)(mem.ReadByte((videoAddress + offsetHGR[line] + col + 1))) << 8;    // store the two next bytes into 'word'
					word += mem.ReadByte(videoAddress + offsetHGR[line] + col);              // in reverse order

					// check if this group of 7 dots need a redraw
					if (HiResCache[line][col] != word || !flashCycle) 
					{

						for (bit = 0; bit < 16; bit++)                                        // store all bits 'word' into 'bits'
							bits[bit] = (word >> bit) & 1;

						colorSet = bits[7] * 4;                                             // select the right color set
						pbit = previousBit[line][col];                                      // the bit value of the left dot
						bit = 0;                                                            // starting at 1st bit of 1st byte

						while (bit < 15) 
						{                                                  // until we reach bit7 of 2nd byte
							if (bit == 7) 
							{                                                   // moving into the second byte
								colorSet = bits[15] * 4;                                        // update the color set
								bit++;                                                          // skip bit 7
							}
							colorIdx = even + colorSet + (bits[bit] << 1) + (pbit);

							DrawPoint(x++, line, hcolor[colorIdx][0], hcolor[colorIdx][1], hcolor[colorIdx][2]);
							pbit = bits[bit++];                                               // proceed to the next pixel
							even = even ? 0 : 8;                                              // one pixel every two is darker
						}

						HiResCache[line][col] = word;                                       // update the video cache
						if ((col < 37) && (previousBit[line][col + 2] != pbit)) {           // check color franging effect on the dot after
							previousBit[line][col + 2] = pbit;                                // set pbit and clear the
							HiResCache[line][col + 2] = -1;                                   // video cache for next dot
						}
					}                                                                     // if (HiResCache[line][col] ...
				}
			}

		}
	}

	// TEXT는 TEXT Only 그리고 Mixed에 모두 출력되어야 함
	if (screenmode == TEXT_MODE || screenmode == LORES_MIX_MODE || screenmode == HIRES_MIX_MODE)
	{
// 		if(screenmode == TEXT_MODE)
// 			ClearScreen();

		videoAddress = videoPage * 0x0400;

		// Text or Mixed
		// Font 크기 7X8 / 40x20 글자
		int linelimit = textMode ? 0 : 20;

		for (int col = 0; col < SCREENTEXT_X; col++)
		{
			for (int line = linelimit; line < SCREENTEXT_Y; line++)
			{
				// read video memory
				BYTE glyph = mem.ReadByte(videoAddress + offsetGR[line] + col);

				int fontattr = 0;
				if (glyph > 0x7F)
					fontattr = FONT_NORMAL;
				else if (glyph < 0x40)
					fontattr = FONT_INVERSE;
				else
					fontattr = FONT_FLASH;

				glyph &= 0x7F; // unset bit 7
				if (glyph > 0x5F) glyph &= 0x3F; // shifts to match
				if (glyph < 0x20) glyph |= 0x40; // the ASCII codes

				bool inverse = !(fontattr == FONT_NORMAL || (fontattr == FONT_FLASH && frame < 15));

				// ---- CACHE DE TEXTO ----
				// chave = glyph + estado de inversao. Se nada mudou, pula o RenderFont.
				// O "&& flashCycle" mantem o refresh completo 1x a cada 30 frames,
				// igual ao que LoResCache/HiResCache ja fazem.
				int key = (int)glyph | (inverse ? 0x100 : 0);
				if (TextCache[line][col] == key && flashCycle)
					continue;
				TextCache[line][col] = key;
				// ------------------------

				font.RenderFont(backbuffer, glyph, col * FONT_X, line * FONT_Y, inverse);
			}
		}
	}


	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/*
	// Render Backbuffer
	UnloadTexture(renderTexture);
	renderTexture = LoadTextureFromImage(renderImage);

	Vector2 pos;
 	pos.x = 300;
 	pos.y = 10;
	DrawTextureEx(renderTexture, pos, 0, zoomscale, WHITE);

	const int gap = 8;
	Rectangle rec;
	rec.x = pos.x - gap;
	rec.y = pos.y - gap;
	rec.width = (float)(SCREENSIZE_X * zoomscale + (gap * 2));
	rec.height = (float)(SCREENSIZE_Y * zoomscale + (gap * 2));
	DrawRectangleLinesEx(rec, 2, GRAY);
*/
	DrawDiskIndicator();
	g_cpuTick = (unsigned long)cpu->tick;

	if (++flashCycle == 30)
		flashCycle = 0;

}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Detecta .dsk / .do pelo final do nome (case-insensitive)
static bool isDskImage(const char *filename)
{
	int len = (int)strlen(filename);
	if (len >= 4 && strcasecmp(filename + len - 4, ".dsk") == 0) return true;
	if (len >= 3 && strcasecmp(filename + len - 3, ".do")  == 0) return true;
	return false;
}

// Apple Disk II
//
// .nib (232960 bytes) = fluxo de nibbles cru, vai direto para o buffer.
// .dsk (143360 bytes) = setores logicos, precisa ser nibblizado antes: o
//                       Disk II nao le setores, le nibbles.
bool Apple2Device::InsertFloppy(const char* filename, int drv)
{
	if (isDskImage(filename))
	{
		BYTE *raw = (BYTE*)ps_malloc(DSK_SIZE);
		if (!raw)
		{
			Serial.println("Insert Floppy: sem PSRAM para o buffer do .dsk");
			return false;
		}

		int readlen = filesystem.ReadFile(filename, raw, DSK_SIZE);
		if (readlen != DSK_SIZE)
		{
			Serial.printf("Read Floppy Fail (.dsk tem %d bytes, esperado %d) : %s\n",
			              readlen, DSK_SIZE, filename);
			free(raw);
			return false;
		}

		nib_fromDsk(raw, disk[drv].data);
		free(raw);
		Serial.printf("Read Floppy OK (.dsk nibblizado) : %s\n", filename);
	}
	else
	{
		int readlen = filesystem.ReadFile(filename, disk[drv].data, DISKSIZE);
		if (readlen != DISKSIZE)
		{
			Serial.printf("Read Floppy Fail : %s\n", filename);
			return false;
		}
		Serial.printf("Read Floppy OK : %s\n", filename);
	}

	sprintf(disk[drv].filename, "%s", filename);

	// 일단 쓰기 불가 모드로 진행
	disk[drv].readOnly = false;	// 읽기만 가능
	return true;
}

void Apple2Device::stepMotor(WORD address)
{
	address &= 7;
	int phase = address >> 1;

	phasesBB[currentDrive][pIdxB[currentDrive]] = phasesB[currentDrive][pIdxB[currentDrive]];
	phasesB[currentDrive][pIdx[currentDrive]] = phases[currentDrive][pIdx[currentDrive]];
	pIdxB[currentDrive] = pIdx[currentDrive];
	pIdx[currentDrive] = phase;

	if (!(address & 1)) 
	{                                                         // head not moving (PHASE x OFF)
		phases[currentDrive][phase] = false;
		return;
	}

	if ((phasesBB[currentDrive][(phase + 1) & 3]) && (--halfTrackPos[currentDrive] < 0))      // head is moving in
		halfTrackPos[currentDrive] = 0;

	if ((phasesBB[currentDrive][(phase - 1) & 3]) && (++halfTrackPos[currentDrive] > 140))    // head is moving out
		halfTrackPos[currentDrive] = 140;

	phases[currentDrive][phase] = true;                                                 // update track#
	disk[currentDrive].track = (halfTrackPos[currentDrive] + 1) / 2;
}

void Apple2Device::setDrv(int drv)
{
	disk[drv].motorOn = disk[!drv].motorOn || disk[drv].motorOn;                  // if any of the motors were ON
	disk[!drv].motorOn = false;                                                   // motor of the other drive is set to OFF
	currentDrive = drv;                                                                 // set the current drive
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////// Input

void Apple2Device::UpdateInput()
{
	UpdateKeyBoard();
	UpdateGamepad();
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////// 

// 플로피 디스크 업데이트
bool Apple2Device::UpdateFloppyDisk()
{
	// Floppy motor가 off이거나 updatedrive이 0이되면 끝
	if (disk[currentDrive].motorOn && ++updatedrive)
		return true;
	else
		return false;
}

// Navegacao preguicosa: le SO o diretorio atual. Entrar numa pasta le aquela
// pasta. Nunca varremos o cartao inteiro -- 30 GB podem ter milhares de arquivos.
//
// Roda antes do emulador subir, entao pode usar a tela toda. Depende de Create()
// ja ter rodado (fonte + teclado prontos).

static const char *baseName(const char *p)
{
	const char *s = strrchr(p, '/');
	return s ? s + 1 : p;
}

// "/a/b" -> "/a" ; "/a" -> "/" ; "/" fica "/"
static void parentDir(char *p)
{
	if (strcmp(p, "/") == 0) return;          // ja na raiz do cartao
	char *s = strrchr(p, '/');
	if (!s)      { strcpy(p, "/"); return; }
	if (s == p)  { p[1] = 0;      return; }   // "/algo" -> "/"
	*s = 0;
}

void Apple2Device::SelectFloppy()
{
	if (!filesystem.BeginSD())
	{
		ClearScreen();
		DrawText(2, 10, "SD CARD NOT FOUND", false);
		DrawText(2, 12, "INSERT SD CARD AND RESET", false);
		while (1) delay(100);
	}

	// entradas do diretorio atual, na PSRAM (256 x ~68 bytes)
	DirEntry *ents = (DirEntry *)ps_malloc(sizeof(DirEntry) * MAXENTRIES);
	if (!ents)
	{
		ClearScreen();
		DrawText(2, 10, "NO PSRAM FOR MENU", false);
		while (1) delay(100);
	}

	char cur[PATHLEN];
	strcpy(cur, "/");                            // raiz do cartao
	int n = filesystem.ListDir(cur, ents, MAXENTRIES);
	int sel = 0, top = 0;
	bool redraw = true;

	const int VISIBLE = 17;
	selectedDisk[0] = 0;
	menuCancelled = false;

	while (1)
	{
		// ".." ocupa a primeira linha quando nao estamos na raiz do cartao
		bool temDots = (strcmp(cur, "/") != 0);
		int total = n + (temDots ? 1 : 0);

		if (redraw)
		{
			ClearScreen();
			DrawText(3, 0, "APPLE ][  -  SELECT BOOT DISK", true);

			char cab[SCREENTEXT_X + 1];
			int lc = strlen(cur);
			if (lc > 37) snprintf(cab, sizeof(cab), "..%s", cur + lc - 35);
			else         snprintf(cab, sizeof(cab), "%s", cur);
			DrawText(1, 2, cab, false);

			for (int i = 0; i < VISIBLE && (top + i) < total; i++)
			{
				int idx = top + i;
				char linha[SCREENTEXT_X + 1];

				if (temDots && idx == 0)
					snprintf(linha, sizeof(linha), " %-36s", "..");
				else
				{
					DirEntry *e = &ents[idx - (temDots ? 1 : 0)];
					char nome[SCREENTEXT_X + 1];
					if (e->isDir)
						snprintf(nome, sizeof(nome), "[%s]", baseName(e->path));
					else
						snprintf(nome, sizeof(nome), "%s", baseName(e->path));
					snprintf(linha, sizeof(linha), " %-36s", nome);
				}
				DrawText(1, 4 + i, linha, idx == sel);
			}

			char rodape[SCREENTEXT_X + 1];
			snprintf(rodape, sizeof(rodape), "%d/%d  ARROWS  RETURN=OK  ESC=CANCEL",
			         total ? sel + 1 : 0, total);
			DrawText(1, 22, rodape, false);
			redraw = false;
		}

		int k = ps2_poll();
		if (k < 0) { delay(10); continue; }

		if (k == 0x0B)                                   // seta cima
		{
			if (sel > 0) sel--;
			if (sel < top) top = sel;
			redraw = true;
		}
		else if (k == 0x0A)                              // seta baixo
		{
			if (sel < total - 1) sel++;
			if (sel >= top + VISIBLE) top = sel - VISIBLE + 1;
			redraw = true;
		}
		else if (k == 0x1B)                              // ESC
		{
			if (strcmp(cur, "/") != 0)
			{
				parentDir(cur);                          // dentro de pasta: sobe um nivel
				n = filesystem.ListDir(cur, ents, MAXENTRIES);
				sel = 0; top = 0; redraw = true;
			}
			else
			{
				selectedDisk[0] = 0;                     // na raiz: cancela, volta pro Apple
				menuCancelled = true;
				break;
			}
		}
		else if (k == 0x0D)                              // RETURN
		{
			if (total == 0) continue;

			if (temDots && sel == 0)                     // ".."
			{
				parentDir(cur);
				n = filesystem.ListDir(cur, ents, MAXENTRIES);
				sel = 0; top = 0; redraw = true;
				continue;
			}

			DirEntry *e = &ents[sel - (temDots ? 1 : 0)];
			if (e->isDir)                                // entra na pasta
			{
				strncpy(cur, e->path, PATHLEN - 1);
				cur[PATHLEN - 1] = 0;
				n = filesystem.ListDir(cur, ents, MAXENTRIES);
				sel = 0; top = 0; redraw = true;
				continue;
			}

			strncpy(selectedDisk, e->path, sizeof(selectedDisk) - 1);
			selectedDisk[sizeof(selectedDisk) - 1] = 0;
			break;
		}
	}

	free(ents);
	ps2_menuRequested();      // descarta um F2 apertado dentro do proprio menu
	Serial.printf("[MENU] escolhido: %s\n", selectedDisk);

	ClearScreen();
	DrawText(6, 11, "LOADING...", false);
}

// Invalida os caches de video: no proximo Render() a tela inteira e redesenhada.
//
// Nao ha memset aqui de proposito. O proprio codigo ja usa flashCycle == 0 como
// "forca redesenho" -- os tres caches testam "|| !flashCycle" ou "&& flashCycle".
// E o que apaga a tela do menu depois que ele fecha.
void Apple2Device::ForceRedraw()
{
	flashCycle = 0;
}

// Troca a imagem com a maquina LIGADA.
//
// Diferente do InsetFloppy(), nao chama disk[].Reset(): trocar o disquete nao
// move a cabeca do drive nem para o motor. Zerar 'track' aqui faria o DOS ler
// a trilha errada, porque ele acha que a cabeca continua onde deixou.
void Apple2Device::SwapFloppy()
{
	Serial.printf("Swap Floppy: %s\n", selectedDisk);
	InsertFloppy(selectedDisk, 0);
}

void Apple2Device::ShowHelp()
{
	ClearScreen();
	DrawText( 8,  1, "EMULATOR KEYS", true);

	DrawText( 2,  3, "F1    THIS HELP", false);
	DrawText( 2,  4, "F2    INSERT/SWAP DISKS", false);
	DrawText( 2,  5, "F11   COLOR/GREEN DISPLAY", false);
	DrawText( 2,  6, "F12   CTRL-RESET (TO BASIC)", false);
	DrawText( 2,  7, "CTRL+F12  POWER CYCLE (REBOOT)", false);
	DrawText( 2,  8, "F7    EXIT TO ESP32 BOOTLOADER", false);

	DrawText( 2,  9, "ARROWS     APPLE ][ ARROW KEYS", false);
	DrawText( 2, 10, "ESC        ESC", false);
	DrawText( 2, 11, "CTRL+C     BREAK (BASIC)", false);
	DrawText( 2, 12, "CTRL+KEY   CONTROL CODES", false);

	DrawText( 2, 14, "DISK MENU:", true);
	DrawText( 2, 15, "  ARROWS   NAVIGATE", false);
	DrawText( 2, 16, "  RETURN   OPEN FOLDER OR BOOT", false);
	DrawText( 2, 17, "  ESC      GO BACK ONE LEVEL", false);

	DrawText( 2, 19, "BOOTS WITH NO DISK: PRESS F2 TO", false);
	DrawText( 2, 20, "INSERT ONE, THEN PR#6 TO BOOT IT.", false);

	DrawText( 2, 22, "PORTED BY FG1998 - GITHUB.COM/FG1998", false);

	DrawText( 9, 23, "PRESS ANY KEY", false);

	while (ps2_poll() < 0)
		delay(10);

	// descarta teclas de funcao apertadas aqui dentro
	ps2_helpRequested();
	ps2_menuRequested();
	ps2_colorRequested();
	ps2_resetRequested();
	ps2_coldRequested();

	ForceRedraw();
}

// Liga a maquina com o drive VAZIO -- e o que um Apple II real faz quando voce
// liga sem disquete: a ROM do Disk II gira o motor procurando o setor de boot,
// nao acha nada, e fica moendo com "APPLE ][" no topo da tela. Ctrl+Reset (F12)
// sai disso e cai no "]". Dai da pra inserir um disco com F2 e dar CATALOG ou PR#6.
void Apple2Device::InsetFloppy()
{
	disk[0].Reset();
	disk[1].Reset();
	selectedDisk[0] = 0;
	Serial.println("Insert Floppy: drive vazio (Ctrl+Reset cai no BASIC)");
}

// Desliga o motor do drive.
//
// Necessario no Ctrl+Reset: sem disco, a ROM do Disk II deixa o motor ligado, e
// o motor ligado dispara o turbo do floppy (255.000 ciclos extras por frame) --
// o BASIC ficaria a 6 FPS. No hardware o motor tem um timer de ~1s que o desliga
// sozinho; aqui nao temos esse timer, entao desligamos no reset.
void Apple2Device::MotorOff()
{
	disk[0].motorOn = false;
	disk[1].motorOn = false;
}

bool Apple2Device::GetDiskMotorState()
{
	return disk[currentDrive].motorOn;
}

int tock = 0;

// $C030: cada acesso inverte a posicao do cone do speaker.
// Nao tocamos som aqui: so gravamos QUANDO isso aconteceu. O emulador roda em
// rajada (17050 ciclos em ~7ms), entao o audio e montado depois, em spk_render(),
// espalhado no tempo correto.
void Apple2Device::PlaySound()
{
	spk_toggle(cpu->tick);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////// 키보드

void Apple2Device::UpdateKeyBoard()
{
	// F12 = Reset, F2 = trocar disco, F1 = ajuda.
	// O Apple2Machine::Run() olha essas flags no inicio do frame.
	if (ps2_resetRequested())
		resetMachine = true;
	if (ps2_menuRequested())
		diskMenuRequested = true;
	if (ps2_helpRequested())
		helpRequested = true;
	if (ps2_coldRequested())
		coldBootRequested = true;
	if (ps2_exitRequested())
		exitRequested = true;

	// F11 = alterna monitor colorido / verde. Age na hora: so muda o DrawPoint.
	if (ps2_colorRequested())
	{
		colorMonitor = !colorMonitor;
		ForceRedraw();
		Serial.printf("[VIDEO] monitor: %s\n", colorMonitor ? "COLOR" : "GREEN");
	}

	// Latch do teclado do Apple II ($C000):
	//   bit 7 = strobe (tem tecla nova), bits 0-6 = ASCII.
	//   $C010 limpa o bit 7 mas o valor persiste -- o latch e de UMA tecla so,
	//   igual ao hardware.
	int k = ps2_poll();
	if (k >= 0)
		keyboard = (BYTE)(k | 0x80);
}


// game pad update
void Apple2Device::UpdateGamepad()
{
}

std::string Apple2Device::GetDiskName(int i)
{
	return disk[i].filename;
}


///////////////////////////////////////////////////////////////////////////////////////

void Apple2Device::Dump(FILE* fp)
{

}

void Apple2Device::LoadDump(FILE* fp)
{

}

// Surface backbuffer
Color * Apple2Device::getBackBuffer()
{
	return backbuffer;
}