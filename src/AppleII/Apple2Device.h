#ifndef APPLE2_DEVICE_H
#define APPLE2_DEVICE_H

#include <stdio.h>
#include <string>
#include "AppleFont.h"
#include "./Tools/Log.h"
#include "./Tools/FileSystem.h"

class CPU;	// 6502 cpu
class Memory;

struct _RECT
{
	int x, y, width, height;
};

// two disk ][ drive units
struct FloppyDrive
{
	char filename[400];
	bool readOnly;
	// nibblelized disk image
	BYTE *data;
	bool motorOn;
	bool writeMode;
	BYTE track;
	WORD nibble;

	FloppyDrive()
	{
		DEBUG_PRINTLN("Construct FloppyDrive");
		data = (BYTE*)ps_malloc(DISKSIZE);
	}

	void Reset()
	{
		memset(data, 0, DISKSIZE);
		memset(filename,0, 400);
		readOnly = false;
		motorOn = false;
		writeMode = false;
		track = 0;
	 	nibble = 0;
	}
};


// apple2의 cpu / memory제외한 device
class Apple2Device
{
public:
	bool loaddumpmachine;
	bool dumpMachine;
	bool loadromfile;

	bool resetMachine;
	bool diskMenuRequested;       // F2: abrir o menu de disco
	bool helpRequested;           // F1: tela de ajuda
	bool exitRequested;           // F7: sair para o bootloader
	bool menuCancelled;           // ESC no menu: nao troca o disco
	bool coldBootRequested;       // Ctrl+F12: liga/desliga
	bool colorMonitor;
	BYTE zoomscale;

	//////////////////////////////////////////////////////////////////////////

	// 현재 플로피 디스크 (1,2)
	int	currentDrive;

	//////////////////////////////////////////////////////////////////////////

	bool textMode;
	bool mixedMode;
	bool hires_Mode;
	BYTE videoPage;
	WORD videoAddress;

	_RECT pixelGR;

	int LoResCache[24][40];
	int HiResCache[192][40];
	int TextCache[SCREENTEXT_Y][SCREENTEXT_X];   // cache do modo texto (glyph | flag inverse)
	BYTE previousBit[192][40];
	BYTE flashCycle;


private:
	CPU* cpu;
	Color* backbuffer;	// Render Backbuffer
	//Texture2D renderTexture;
	//Image renderImage;

	AppleFont font;

	// 키보드입력 값
	BYTE keyboard;

	////////////////////////////////////////////////

	FloppyDrive disk[2];
	BYTE updatedrive;

	bool phases[2][4];
	// phases states Before
	bool phasesB[2][4];
	// phases states Before Before
	bool phasesBB[2][4];
	// phase index (for both drives)
	int pIdx[2];
	// phase index Before
	int pIdxB[2];
	int halfTrackPos[2];
	BYTE dLatch;

	////////////////////////////////////////////////

	FileSystem filesystem;
	char selectedDisk[64];        // escolhido no menu

	////////////////////////////////////////////////

	// DISK2
	bool InsertFloppy(const char* filename, int drv);
	void stepMotor(WORD address);
	void setDrv(int drv);

	// Keyboard
	void UpdateKeyBoard();
	// GamePad
	void UpdateGamepad();

	void ClearScreen();
	void DrawDiskIndicator();
	void DrawText(int col, int line, const char *s, bool inv);
	void DrawPoint(int x, int y, int r, int g, int b);
	void DrawRect(_RECT rect, int r, int g, int b);
	int GetScreenMode();

public:
	Apple2Device();
	~Apple2Device();

	void Create(CPU* cpu);
	void Reset();
	void Dump(FILE* fp);
	void LoadDump(FILE* fp);

	BYTE SoftSwitch(Memory* mem, WORD address, BYTE value, bool WRT);
	void PlaySound();
	void Render( Memory& mem, int frame);

	void UpdateInput();

	bool UpdateFloppyDisk();
	void SelectFloppy();          // menu de escolha do disco
	void InsetFloppy();           // liga com o drive vazio
	void MotorOff();              // desliga o motor (usado no Ctrl+Reset)
	void SwapFloppy();            // troca com a maquina ligada (NAO mexe no drive)
	void ShowHelp();              // F1
	void ForceRedraw();           // invalida os caches de video

	bool GetDiskMotorState();
	std::string GetDiskName(int i);

	Color *getBackBuffer();
};



#endif