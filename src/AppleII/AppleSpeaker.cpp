// Speaker de 1 bit do Apple II -> PCM -> DAC via FabGL SoundGenerator.
//
// Isolado do resto do emulador porque fabgl.h e Predef.h nao convivem.

#include <Arduino.h>
#include <fabgl.h>
#include "AppleSpeaker.h"

// ---------------------------------------------------------------------------
// Timing
//   O Apple II roda a 1.023.000 Hz. Escolhemos 64 ciclos por sample porque:
//     - 1023000/64 = 15984 Hz, perto dos 16 kHz padrao (erro 0,1%, inaudivel)
//     - 64 e potencia de 2 -> divisao vira shift
// ---------------------------------------------------------------------------
#define APPLE_HZ          1023000
#define CYCLES_PER_SAMP   64
#define SAMPLE_RATE       (APPLE_HZ / CYCLES_PER_SAMP)    // 15984

#define TOG_MAX           512      // toggles pendentes (1 kHz = 33/frame; sobra)
#define PCM_MAX           2048     // ~128ms de audio bufferizado
#define RESYNC_CYCLES     32768    // atraso maximo antes de ressincronizar

// ---- estado do renderizador (so a thread do emulador toca) ----
static long long s_tog[TOG_MAX];
static int       s_nTog = 0;
static long long s_renderTick = 0;
static int       s_level = 0;
static bool      s_started = false;
static float     s_dcx = 0, s_dcy = 0;      // DC blocker

// ---- ring PCM: produtor = emulador, consumidor = task de audio ----
static volatile int8_t s_pcm[PCM_MAX];
static volatile int    s_wr = 0;
static volatile int    s_rd = 0;

static inline void pcmPush(int8_t v)
{
	int nx = (s_wr + 1) % PCM_MAX;
	if (nx == s_rd) return;          // cheio (ex: turbo do disco) -> descarta
	s_pcm[s_wr] = v;
	s_wr = nx;
}

// ---------------------------------------------------------------------------
// Gerador da FabGL: apenas drena o ring PCM.
// ---------------------------------------------------------------------------
class AppleSpeakerGen : public fabgl::WaveformGenerator
{
public:
	void setFrequency(int value) { }        // nao usado: a onda vem do buffer

	int getSample()
	{
		if (s_rd == s_wr)
			return 0;                       // underrun -> silencio
		int8_t v = s_pcm[s_rd];
		s_rd = (s_rd + 1) % PCM_MAX;
		return (int)v * volume() / 127;
	}
};

static AppleSpeakerGen     s_gen;
static fabgl::SoundGenerator s_snd(SAMPLE_RATE);

// ---------------------------------------------------------------------------

void spk_begin()
{
	if (s_started) return;
	s_snd.setVolume(100);
	s_snd.attach(&s_gen);
	s_gen.enable(true);
	s_snd.play(true);
	s_started = true;
	Serial.printf("[SPK] audio iniciado: GPIO25 (DAC), %d Hz, %d ciclos/sample\n",
	              SAMPLE_RATE, CYCLES_PER_SAMP);
}

void spk_toggle(long long tick)
{
	if (s_nTog < TOG_MAX)
		s_tog[s_nTog++] = tick;
}

void spk_render(long long tick)
{
	if (!s_started) { s_nTog = 0; return; }

	if (s_renderTick == 0)
		s_renderTick = tick;

	// Muito atrasado (turbo do disco: 272050 ciclos num frame): ressincroniza
	// em vez de gerar milhares de samples que nao cabem no buffer.
	if (tick - s_renderTick > RESYNC_CYCLES)
		s_renderTick = tick - RESYNC_CYCLES;

	int t = 0;
	while (s_renderTick + CYCLES_PER_SAMP <= tick)
	{
		long long c0 = s_renderTick;
		long long c1 = c0 + CYCLES_PER_SAMP;

		// Quanto tempo, dentro desta janela, o cone ficou na posicao 1.
		// Isso e o que da o "anti-aliasing": um toggle no meio da janela
		// vira meio sample, nao um degrau inteiro.
		long long hi = 0, cur = c0;
		int lv = s_level;
		while (t < s_nTog && s_tog[t] < c1)
		{
			if (s_tog[t] >= c0)
			{
				if (lv) hi += s_tog[t] - cur;
				cur = s_tog[t];
				lv ^= 1;
			}
			t++;
		}
		if (lv) hi += c1 - cur;
		s_level = lv;

		float x = ((float)hi / CYCLES_PER_SAMP) * 2.0f - 1.0f;   // -1..+1
		float y = x - s_dcx + 0.995f * s_dcy;                     // DC blocker:
		s_dcx = x; s_dcy = y;                                     // silencio -> 0

		int s = (int)(y * 100.0f);
		if (s >  127) s =  127;
		if (s < -128) s = -128;
		pcmPush((int8_t)s);

		s_renderTick = c1;
	}

	// descarta os toggles ja consumidos
	int keep = 0;
	for (int i = t; i < s_nTog; i++) s_tog[keep++] = s_tog[i];
	s_nTog = keep;
}
