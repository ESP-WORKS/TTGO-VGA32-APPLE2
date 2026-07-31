#pragma once

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include "./Tools/Log.h"

// ---------------------------------------------------------------------------
// Pinos do cartao SD na TTGO VGA32 / LilyGO ESP32 VGA
//
// ATENCAO: GPIO12 e o MTDI, pino de strapping que escolhe a tensao da flash
// no boot. A placa foi projetada para isso, mas se ela parar de bootar com o
// cartao inserido, o suspeito e esse.
// ---------------------------------------------------------------------------
#define SD_CLK   14
#define SD_MISO   2
#define SD_MOSI  12
#define SD_CS    13

#define PATHLEN     64
#define MAXENTRIES 256   // entradas de UM diretorio (nao do cartao inteiro)

// Uma entrada do diretorio atual. A navegacao e preguicosa: so lemos a pasta
// em que o usuario esta. Varrer o cartao inteiro seria inviavel -- um cartao
// de 30 GB pode ter milhares de arquivos.
struct DirEntry
{
	char path[PATHLEN];   // caminho completo no cartao
	bool isDir;
};

class FileSystem
{
	bool _sdOk = false;

	// Monta o caminho completo de uma entrada, tolerando as duas convencoes
	// de File::name() (com ou sem o caminho embutido, varia com a versao do core).
	static void FullPath(const char *dir, const char *nm, char *out, int outlen)
	{
		if (nm[0] == '/')
			snprintf(out, outlen, "%s", nm);
		else if (strcmp(dir, "/") == 0)
			snprintf(out, outlen, "/%s", nm);
		else
			snprintf(out, outlen, "%s/%s", dir, nm);
	}

public:
	FileSystem()
	{
		DEBUG_PRINTLN("Construct FileSystem");
	}

	~FileSystem()
	{
		SD.end();
	}

	// Monta o cartao. 4 MHz de proposito: a 20 MHz essa fiacao dava crc error,
	// e a imagem e lida uma vez so -- velocidade ali nao vale nada.
	bool BeginSD()
	{
		if (_sdOk) return true;

		SPI.begin(SD_CLK, SD_MISO, SD_MOSI, SD_CS);
		if (!SD.begin(SD_CS, SPI, 4000000))
		{
			Serial.println("[SD] cartao nao montou");
			_sdOk = false;
			return false;
		}
		uint64_t mb = SD.cardSize() / (1024ULL * 1024ULL);
		Serial.printf("[SD] montado: %llu MB  (CLK=%d MISO=%d MOSI=%d CS=%d)\n",
		              mb, SD_CLK, SD_MISO, SD_MOSI, SD_CS);
		_sdOk = true;
		return true;
	}

	bool SDReady() { return _sdOk; }

	// Lixo do macOS: "._foo.dsk" (AppleDouble), ".DS_Store", ".Spotlight-V100",
	// ".Trashes", ".fseventsd". Tudo que comeca com ponto sai da lista.
	static bool IsJunk(const char *path)
	{
		const char *b = strrchr(path, '/');
		b = b ? b + 1 : path;
		return (b[0] == '.');
	}

	// Ignora arquivos ocultos e o lixo que o macOS espalha em cartao FAT:
	// ._nome (AppleDouble), .DS_Store, .Spotlight-V100, .Trashes, .fseventsd...
	// Basta filtrar tudo que comeca com ponto. O File::name() pode vir com
	// caminho, entao olhamos so o ultimo componente.
	static bool IsHidden(const char *name)
	{
		const char *base = strrchr(name, '/');
		base = base ? base + 1 : name;
		return base[0] == '.';
	}

	static bool IsDiskImage(const char *name)
	{
		int n = strlen(name);
		if (n >= 4 && strcasecmp(name + n - 4, ".dsk") == 0) return true;
		if (n >= 4 && strcasecmp(name + n - 4, ".nib") == 0) return true;
		if (n >= 3 && strcasecmp(name + n - 3, ".do")  == 0) return true;
		return false;
	}

	// Lista SOMENTE o diretorio pedido -- sem recursao.
	// Diretorios primeiro, depois as imagens (a ordem do FAT e arbitraria).
	int ListDir(const char *dir, DirEntry *out, int maxn)
	{
		int n = 0;
		if (!_sdOk) return 0;

		File root = SD.open(dir);
		if (!root || !root.isDirectory()) return 0;

		// passo 1: subdiretorios
		File f = root.openNextFile();
		while (f && n < maxn)
		{
			if (f.isDirectory())
			{
				char full[PATHLEN];
				FullPath(dir, f.name(), full, sizeof(full));
				if (!IsJunk(full))
				{
					strncpy(out[n].path, full, PATHLEN - 1);
					out[n].path[PATHLEN - 1] = 0;
					out[n].isDir = true;
					n++;
				}
			}
			f = root.openNextFile();
		}

		// passo 2: imagens de disco
		root.rewindDirectory();
		f = root.openNextFile();
		while (f && n < maxn)
		{
			if (!f.isDirectory())
			{
				char full[PATHLEN];
				FullPath(dir, f.name(), full, sizeof(full));
				if (IsDiskImage(full) && !IsJunk(full))
				{
					strncpy(out[n].path, full, PATHLEN - 1);
					out[n].path[PATHLEN - 1] = 0;
					out[n].isDir = false;
					n++;
				}
			}
			f = root.openNextFile();
		}
		root.close();

		return n;
	}

	int ReadFile(const char *path, unsigned char *buffer, int len)
	{
		if (!_sdOk)
		{
			Serial.println("- cartao SD nao montado");
			return -1;
		}

		File file = SD.open(path);
		if (!file)
		{
			Serial.printf("- failed to open file for reading: %s\n", path);
			return -1;
		}

		int readlen = file.read(buffer, len);
		Serial.printf("- read from file : %d\n", readlen);
		file.close();

		return readlen;
	}
};