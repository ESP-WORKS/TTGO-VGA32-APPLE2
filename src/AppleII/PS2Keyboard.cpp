// Ponte para o teclado PS/2 da FabGL -> codigos do Apple II.
//
// Isolado do resto do emulador porque fabgl.h e Predef.h nao convivem
// (ambos definem "Color" no escopo global).

#include <Arduino.h>
#include <fabgl.h>
#include "PS2Keyboard.h"

// 1 = imprime cada tecla recebida no serial (para diagnostico)
#define PS2_DEBUG   1

static fabgl::PS2Controller ps2;
static bool s_started  = false;
static bool s_resetReq = false;
static bool s_menuReq  = false;
static bool s_helpReq  = false;
static bool s_colorReq = false;

void ps2_begin()
{
	if (s_started) return;

	// GPIO33 = CLK, GPIO32 = DATA (defaults da FabGL = pinos da TTGO VGA32)
	//
	// ATENCAO ao modo: virtualKeyAvailable()/getNextVirtualKey() leem de uma
	// FILA que so existe com CreateVirtualKeysQueue. Com GenerateVirtualKeys
	// as teclas sao geradas mas nao enfileiradas, e virtualKeyAvailable()
	// retorna false para sempre.
	ps2.begin(PS2Preset::KeyboardPort0, KbdMode::CreateVirtualKeysQueue);
	s_started = true;

	auto kbd = ps2.keyboard();
	bool present = (kbd && kbd->isKeyboardAvailable());
	Serial.printf("[PS2] init: GPIO33=CLK GPIO32=DATA | teclado detectado: %s\n",
	              present ? "SIM" : "NAO");
	if (!present)
		Serial.println("[PS2] teclado nao respondeu ao handshake -- checar cabo/conector/alimentacao");
}

int ps2_poll()
{
	if (!s_started) return -1;

	auto kbd = ps2.keyboard();
	if (!kbd) return -1;
	if (!kbd->virtualKeyAvailable()) return -1;

	fabgl::VirtualKeyItem item;
	if (!kbd->getNextVirtualKey(&item, 0)) return -1;
	if (!item.down) return -1;                       // so key-down

	// ---- teclas de funcao do emulador ----
	// Nao existem no Apple II: sao do emulador. Devolvem -1 para nao virarem
	// tecla do Apple; quem age nelas e o Apple2Machine::Run().
	switch (item.vk)
	{
		case fabgl::VK_F1:
			s_helpReq = true;
#if PS2_DEBUG
			Serial.println("[PS2] F1 -> AJUDA");
#endif
			return -1;

		case fabgl::VK_F2:
			s_menuReq = true;
#if PS2_DEBUG
			Serial.println("[PS2] F2 -> TROCAR DISCO");
#endif
			return -1;

		case fabgl::VK_F11:
			s_colorReq = true;
#if PS2_DEBUG
			Serial.println("[PS2] F11 -> COLOR/VERDE");
#endif
			return -1;

		case fabgl::VK_F12:
			s_resetReq = true;
#if PS2_DEBUG
			Serial.println("[PS2] F12 -> RESET");
#endif
			return -1;

		default: break;
	}

	int a;
	switch (item.vk)
	{
		// Teclas do Apple II que nao tem ASCII direto
		case fabgl::VK_LEFT:       a = 0x08; break;   // seta esquerda
		case fabgl::VK_RIGHT:      a = 0x15; break;   // seta direita
		case fabgl::VK_UP:         a = 0x0B; break;   // seta cima
		case fabgl::VK_DOWN:       a = 0x0A; break;   // seta baixo
		case fabgl::VK_BACKSPACE:  a = 0x08; break;   // igual a seta esquerda
		case fabgl::VK_RETURN:
		case fabgl::VK_KP_ENTER:   a = 0x0D; break;
		case fabgl::VK_ESCAPE:     a = 0x1B; break;
		case fabgl::VK_TAB:        a = 0x09; break;

		default:
			a = item.ASCII;                          // a FabGL ja resolve layout e shift
			if (a <= 0) return -1;

			// Apple II+ so tem maiuscula
			if (a >= 'a' && a <= 'z')
				a -= 32;

			// Ctrl+letra -> codigo de controle ($01-$1F)
			if (item.CTRL && a >= '@' && a <= '_')
				a &= 0x1F;
			break;
	}

	if (a < 0 || a > 0x7F) return -1;

#if PS2_DEBUG
	Serial.printf("[PS2] vk=%d ascii=%d ctrl=%d -> apple:$%02X\n",
	              (int)item.vk, (int)item.ASCII, (int)item.CTRL, a);
#endif

	return a;
}

bool ps2_resetRequested()
{
	bool r = s_resetReq;
	s_resetReq = false;
	return r;
}

bool ps2_menuRequested()
{
	bool r = s_menuReq;
	s_menuReq = false;
	return r;
}

bool ps2_helpRequested()
{
	bool r = s_helpReq;
	s_helpReq = false;
	return r;
}

bool ps2_colorRequested()
{
	bool r = s_colorReq;
	s_colorReq = false;
	return r;
}