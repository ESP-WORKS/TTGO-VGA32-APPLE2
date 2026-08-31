
/*
	MOS 6502 CPU Emulator
*/

#include "AppleMem.h"
#include "Tools/Log.h"
#include <esp_heap_caps.h>

Memory::Memory()
{
	DEBUG_PRINTLN("Construct Memory");
	device = NULL;
	ram = nullptr;
	rom = nullptr;
	lgc = nullptr;
	bk2 = nullptr;
	sl6 = nullptr;
}

Memory::~Memory()
{
	Destroy();
}

void Memory::Create()
{
	// O caminho critico do 6502 nao pode depender da PSRAM.
	// MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT garante DRAM interna,
	// deixando a PSRAM exclusivamente para dados grandes como os discos.
	ram = (BYTE*)heap_caps_malloc(RAMSIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
	rom = (BYTE*)heap_caps_malloc(ROMSIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
	lgc = (BYTE*)heap_caps_malloc(LGCSIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
	bk2 = (BYTE*)heap_caps_malloc(BK2SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
	sl6 = (BYTE*)heap_caps_malloc(SL6SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

	if (!ram || !rom || !lgc || !bk2 || !sl6)
	{
		DEBUG_PRINTLN("ERRO: RAM interna insuficiente para memoria Apple II");
		Destroy();
		return;
	}

	DEBUG_PRINTLN("Apple II memory: INTERNAL DRAM");
	Reset();
}

void Memory::Destroy()
{
	heap_caps_free(ram);
	heap_caps_free(rom);
	heap_caps_free(lgc);
	heap_caps_free(bk2);
	heap_caps_free(sl6);
	ram = nullptr;
	rom = nullptr;
	lgc = nullptr;
	bk2 = nullptr;
	sl6 = nullptr;
}

void Memory::Reset()
{
	LCWritable = true;
	LCReadable = false;
	LCBank2Enable = true;
	LCPreWriteFlipflop = false;

	memset(ram, 0, RAMSIZE);
	memset(rom, 0, ROMSIZE);
	memset(lgc, 0, LGCSIZE);
	memset(bk2, 0, BK2SIZE);
	memset(sl6, 0, SL6SIZE);
}


IRAM_ATTR BYTE Memory::ReadByte(int address)
{
#if 0
	BYTE v = memory[address];
	if (address == 0xCFFF || ((address & 0xFF00) == 0xC000))
		v = device->SoftSwitch(address, 0, false);
	return v;
#else
	if (address < RAMSIZE)
		return ram[address];                                                        // RAM

	if (address >= ROMSTART) {
		if (!LCReadable)
			return rom[address - ROMSTART];                                           // ROM

		if (LCBank2Enable && (address < 0xE000))
			return bk2[address - BK2START];                                           // BK2

		return lgc[address - LGCSTART];                                             // LC
	}

	if ((address & 0xFF00) == SL6START)
		return sl6[address - SL6START];  // disk][

	if ((address & 0xF000) == 0xC000)
		return (device->SoftSwitch(this, address, 0, false));

	return 0;
#endif
}

IRAM_ATTR void Memory::WriteByte(int address, BYTE value)
{
#if 0

	memory[address] = value;
	if (address == 0xCFFF || ((address & 0xFF00) == 0xC000))
		device->SoftSwitch(address, value, true);
#else
	if (address < RAMSIZE) {
		ram[address] = value;                                                       // RAM
		return;
	}

	if (LCWritable && (address >= ROMSTART)) {
		if (LCBank2Enable && (address < 0xE000)) {
			bk2[address - BK2START] = value;                                          // BK2
			return;
		}
		lgc[address - LGCSTART] = value;                                            // LC
		return;
	}

	if ((address & 0xF000) == 0xC000)
	{
		device->SoftSwitch(this, address, value, true);
		return;
	}
#endif
}

IRAM_ATTR WORD Memory::ReadWord(int addr)
{
	BYTE m0 = ReadByte(addr);
	BYTE m1 = ReadByte(addr + 1);
	WORD w = (m1 << 8) | m0;
	return w;
}

IRAM_ATTR void Memory::WriteWord(WORD value, int addr)
{
	WriteByte(addr, value >> 8);
	WriteByte(addr + 1, value & 0xFF);
}


void Memory::UpLoadToRom(BYTE* code)
{
	memcpy(rom, code, ROMSIZE);
}

void Memory::ResetRam()
{
	memset(ram, 0, RAMSIZE);
}

void Memory::Dump(FILE* fp)
{

}

void Memory::LoadDump(FILE* fp)
{

}