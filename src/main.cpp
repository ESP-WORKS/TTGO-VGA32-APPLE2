#include <Arduino.h>
#include "./Tools/Log.h"
#include "./VGA/ESP32S3VGA.h"
#include "./AppleII/Apple2Machine.h"


// Ported by FG1998 using Claude.ai

// ---------------------------------------------------------------------------
// PROFILING / TESTE
//   NO_RENDER = 1  -> pula machine->Render() por completo.
//                     Se o FPS NAO subir, o gargalo e o machine->Run() (6502).
//                     Se o FPS disparar, o gargalo e o Render().
// ---------------------------------------------------------------------------
#define NO_RENDER   0

// ---------------------------------------------------------------------------
// LIMITADOR DE VELOCIDADE
//   O Apple II roda a 1,023 MHz. Como emulamos 17050 ciclos por frame:
//       17050 x 60 fps = 1.023.000 Hz  -> exatamente 60 FPS
//   Sem isso o emulador roda a ~77 FPS = 1,31 MHz (28% acelerado).
//   Durante leitura de disco o frame ja estoura os 16ms, entao o limitador
//   nao interfere e o "turbo" do floppy continua valendo.
// ---------------------------------------------------------------------------
#define FRAME_LIMIT   1
#define FRAME_US      16667

//#define COLORBIT_8    true

//pin configuration
#define PIN_NULL	0

#define PIN_R0	4
#define PIN_R1	5
#define PIN_R2	6
#define PIN_R3	7
#define PIN_R4	15

#define PIN_G0	16
#define PIN_G1	17
#define PIN_G2	18
#define PIN_G3	8
#define PIN_G4	3

#define PIN_B0	10
#define PIN_B1	11
#define PIN_B2	12
#define PIN_B3	13
#define PIN_B4	14

#define HSYNC	21//48
#define VSYNC	47


const PinConfig pins(
PIN_R0, PIN_R1, PIN_R2, PIN_R3, PIN_R4,
PIN_G0, PIN_G1, PIN_NULL, PIN_G2, PIN_G3, PIN_G4,
PIN_B0, PIN_B1, PIN_B2, PIN_B3, PIN_B4,
HSYNC, VSYNC);

const PinConfig pins_8(
PIN_NULL, PIN_NULL, PIN_R0, PIN_R2, PIN_R4, // 3
PIN_NULL, PIN_NULL, PIN_NULL, PIN_G0, PIN_G2, PIN_G4, // 3
PIN_NULL, PIN_NULL, PIN_NULL, PIN_B0, PIN_B3,
HSYNC, VSYNC);


VGA *vga;
Apple2Machine *machine;

//initial setup
void setup()
{
	Serial.begin(115200);

	// Ensures the bootloader runs on the next power-up
    const esp_partition_t* otadata = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, NULL);
    if (otadata) {
        esp_partition_erase_range(otadata, 0, otadata->size);
    }

	
	if(psramInit())
		Serial.println("\nPSRAM is correctly initialized");
	else
		Serial.println("PSRAM not available");

	// O cartao SD e montado pelo menu (Apple2Device::SelectFloppy).

	machine = new Apple2Machine();
	vga = new VGA();

	DEBUG_PRINTLN("===> MODE320x200");
    Mode mode = Mode::MODE_320x200x70;
#ifdef COLORBIT_8
    if(!vga->init(pins_8, mode, 8, 2)) while(1) delay(1);
#else    
	if(!vga->init(pins, mode, 16, 1)) while(1) delay(1);
#endif

    vga->start();
	DEBUG_PRINTLN("===> INIT Machine");
	machine->InitMachine();
}

unsigned long heapCheckMillis = 0;

unsigned long memlast = 0;
int frame = 0;
int fpscount = 0;
unsigned long fpsMillis = 0;

// acumuladores de profiling
static unsigned long accRun = 0, accRender = 0, accShow = 0, accN = 0;

static unsigned long nextFrameUs = 0;

// contadores vindos do Apple2Machine.cpp
extern unsigned long g_cycles;
extern unsigned long g_floppyIters;
extern unsigned long g_devRender;
extern unsigned long g_blit;

// contadores vindos do Apple2Device.cpp
extern unsigned long g_diskReads;
extern unsigned long g_diskTrack;
extern unsigned long g_diskNib;
extern unsigned long g_cpuTick;
extern unsigned long g_diskSkips;

void loop()
{
	long long p = 17050;

	unsigned long t0 = micros();
	machine->Run(p);

	unsigned long t1 = micros();
#if !NO_RENDER
	machine->Render(vga, frame);
#endif

	unsigned long t2 = micros();
	vga->show();

	unsigned long t3 = micros();

	accRun    += (t1 - t0);
	accRender += (t2 - t1);
	accShow   += (t3 - t2);
	accN++;

    if (frame++ > TARGET_FRAME) 
        frame = 0;

	if(millis() - heapCheckMillis > 150000)
	{
		unsigned int memcurr = ESP.getFreeHeap();
		memlast = memcurr;
		heapCheckMillis = millis();

		Serial.printf("Heap : %d / %d\n", ESP.getFreeHeap(), ESP.getHeapSize());
		Serial.printf("PSRam : %d / %d\n", ESP.getFreePsram(), ESP.getPsramSize());
	}

#if FRAME_LIMIT
	if (nextFrameUs == 0)
		nextFrameUs = micros();
	nextFrameUs += FRAME_US;
	long wait = (long)(nextFrameUs - micros());
	if (wait > 0)
		delayMicroseconds(wait);
	else
		nextFrameUs = micros();   // atrasado (disco girando): nao acumula divida
#endif

    fpscount++;
    if( millis() - fpsMillis > 1000)
    {
        fpsMillis = millis();
        if (accN == 0) accN = 1;
        Serial.printf("FPS:%d  Run:%luus  devRender:%luus  cyc:%lu  floppy:%lu  | nibs/s:%lu  track:%lu  nib:%lu  skips:%lu\n",
                      fpscount,
                      accRun / accN,
                      g_devRender / accN,
                      g_cycles / accN,
                      g_floppyIters / accN,
                      g_diskReads,
                      g_diskTrack,
                      g_diskNib,
                      g_diskSkips);
        g_diskReads = 0;
        g_diskSkips = 0;
        accRun = accRender = accShow = 0;
        accN = 0;
        g_cycles = g_floppyIters = g_devRender = g_blit = 0;
        fpscount = 0;
    }
}