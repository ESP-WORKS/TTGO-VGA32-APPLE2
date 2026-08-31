#include "Predef.h"
#include "rombios.h"
#include "Apple2Machine.h"
#include "./Tools/Log.h"
#include "./VGA/ESP32S3VGA.h"
#include "AppleSpeaker.h"

// ---- contadores de profiling (lidos pelo main.cpp) ----
unsigned long g_cycles      = 0;
unsigned long g_floppyIters = 0;
unsigned long g_devRender   = 0;
unsigned long g_blit        = 0;

Apple2Machine::Apple2Machine()
{
	DEBUG_PRINTLN("Construct Apple2Machine");
}

Apple2Machine::~Apple2Machine()
{

}

void Apple2Machine::InitMachine()
{
	mem.Create();

	// Create() foi movido para CA. Ele e quem inicializa a fonte, o teclado PS/2
	// e o audio -- e o menu de selecao precisa dos tres. Nao depende de nada que
	// o InsetFloppy() faca, entao a troca de ordem e segura.
	device.Create(&cpu);

	// Sem menu no boot: a maquina liga com o drive vazio, igual a um Apple II
	// de verdade. O disco entra depois, com F2.
	device.InsetFloppy();

	// unset the Power-UP byte
	mem.WriteByte(0x3F4, 0);
	cpu.Reset(mem);
	mem.WriteByte(0x4D, 0xAA);   // Just crashes if this memory location equals zero
	mem.WriteByte(0xD0, 0xAA);   // won't work if this memory location equals zero

	mem.device = &device;

	Booting();
	//UploadRom();	
}

// 롬을 내장
bool Apple2Machine::Booting()
{
	DEBUG_PRINTLN("====> BOOTING ...");
	DEBUG_PRINTLN("Load Apple II Rom");
	memcpy(mem.rom, appleIIrom, ROMSIZE);

	DEBUG_PRINTLN("Load Disk II");
	memcpy(mem.sl6, diskII, SL6SIZE);

	cpu.Reset(mem);
	return true;
}

// 롬을 파일에서 로딩
bool Apple2Machine::UploadRom()
{
	bool ret = false;

	// load the Apple II+ ROM
	FILE* fp = fopen("/apple2.rom", "rb"); 
	if (fp)
	{
#if 0
		fread(rom, ROMSIZE, 1, fp);
		mem.UpLoadProgram(ROMSTART, rom, ROMSIZE);
#else
		fread(mem.rom, ROMSIZE, 1, fp);
#endif
		fclose(fp);
		ret = true;
	}

	// load Apple II+ / Disk II
	FILE* disk2fp = fopen("rom/diskII.rom", "rb");
	if (disk2fp)
	{
#if 0
		fread(disk2, SL6SIZE, 1, disk2fp);
		mem.UpLoadProgram(SL6START, disk2, SL6SIZE);
#else
		fread(mem.sl6, SL6SIZE, 1, disk2fp);
#endif
		fclose(disk2fp);
		ret = true;
	}
	cpu.Reset(mem);
	return ret;
}

void Apple2Machine::Reset()
{
	mem.Reset();
	cpu.Reboot(mem);
	device.Reset();

	// unset the Power-UP byte
	mem.WriteByte(0x3F4, 0);
	// dirty hack, fix soon... if I understand why
	mem.WriteByte(0x4D, 0xAA);   // Joust crashes if this memory location equals zero
	mem.WriteByte(0xD0, 0xAA);   // Planetoids won't work if this memory location equals zero

	Booting();
}

void Apple2Machine::Run(long long cycle)
{
	// F7 -> reinicia o ESP32
	if (device.exitRequested)
	{
		device.exitRequested = false;
		ESP.restart();
	}

	// F1 -> ajuda
	if (device.helpRequested)
	{
		device.helpRequested = false;
		device.ShowHelp();
		return;
	}

	// F2 -> trocar de disco. NAO reseta: e o equivalente a abrir a portinha do
	// drive e trocar o disquete com a maquina ligada, que e o que os jogos de
	// varios discos esperam ("INSERT DISK 2 AND PRESS RETURN").
	if (device.diskMenuRequested)
	{
		device.diskMenuRequested = false;
		device.SelectFloppy();
		if (!device.menuCancelled)     // ESC no menu = nao troca o disco
		{
			device.SwapFloppy();
			device.ForceRedraw();      // apaga a tela do menu
		}
		return;
	}

	// Ctrl+F12 -> liga/desliga: limpa a RAM, recarrega a ROM e boota o disco.
	if (device.coldBootRequested)
	{
		device.coldBootRequested = false;
		Reset();
		return;
	}

	// F12 -> Ctrl+Reset. Num Apple II real isso so puxa a linha de RESET: a ROM
	// autostart confere o power-up byte e, se ele estiver valido, faz warm start
	// e cai no "]" do Applesoft. Nao limpamos a RAM nem recarregamos a ROM --
	// isso seria desligar e ligar (que e o Ctrl+F12 acima).
	if (device.resetMachine)
	{
		device.resetMachine = false;
		device.MotorOff();
		cpu.Reset(mem);
		device.ForceRedraw();
		return;
	}

	device.UpdateInput();
	cpu.Run(mem, cycle);
	g_cycles += cycle;
	while (1)
	{
		if( device.UpdateFloppyDisk() == false ) 
			break;
		cpu.Run(mem, 1000);
		g_cycles += 1000;
		g_floppyIters++;
	}

	// Converte os toggles do speaker desta rajada em PCM, no tempo certo.
	spk_render(cpu.tick);
}

void Apple2Machine::Render(VGA *vga, int frame)
{
	// O device.Render() ja escreve direto no framebuffer da VGA.
	// Nao existe mais backbuffer nem blit.
	unsigned long a = micros();
	device.Render(mem, frame);
	unsigned long b = micros();

	g_devRender += (b - a);
	g_blit      += 0;
}


// DUMP파일을 로드하여 재개
void Apple2Machine::LoadMachine(std::string path)
{
}

// 현재의 모든 상태를 저장
void Apple2Machine::DumpMachine(std::string path)
{
}