/**
 * Converts a Mupen64 INI ROM list to a small binary representation of it.
 * Copyright (c) 2020 Mahyar Koshkouei
 *
 * Permission to use, copy, modify, and/or distribute this software for any purpose
 * with or without fee is hereby granted.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF
 * THIS SOFTWARE.
 */

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define PRINTERR()	\
	fprintf(stderr, "ERR %s:%d %s\n", __func__, __LINE__, strerror(errno))

struct crc_entry_s
{
	uint64_t crc;
};

struct rom_entry_s
{
	union
	{
		struct
		{
			/* Unused value allows for packing entry in exactly 4
			 * bytes. */
			unsigned char unused : 1;

			unsigned char save_type : 3;
			unsigned char players : 3;
			unsigned char rumble : 1;
			unsigned char transferpak : 1;
			unsigned char status : 3;
			unsigned char count_per_op : 3;
			unsigned char disable_extra_mem : 1;

			/* Actual cheat data isn't stored in rom data entry, but
			 * in a look-up table. This value is the index for the
			 * cheats look-up table. */
			unsigned char cheat_lut : 5;

			unsigned char mempack : 1;
			unsigned char biopak : 1;

			/* Only Tetris 64 requires this. If 1, then set to 0x100. */
			unsigned char si_dma_duration : 1;
		};
		struct
		{
			/* Does this entry refer to another entry?
			* If it does, look up the rom entry at value reference_entry. */
			unsigned char reference : 1;
			uint16_t reference_entry;
		};
	};
}__attribute__((packed));

static char *read_entire_file(const char *filename)
{
	FILE *f = fopen(filename, "rb");
	size_t fsz;
	char *ini = NULL;

	if(f == NULL)
	{
		PRINTERR();
		goto out;
	}

	fseek(f, 0, SEEK_END);
	fsz = ftell(f);
	fseek(f, 0, SEEK_SET);

	ini = malloc(fsz + 1);
	if(ini == NULL)
	{
		PRINTERR();
		fclose(f);
		goto out;
	}

	fread(ini, 1, fsz, f);
	fclose(f);

	ini[fsz] = '\0';

out:
	return ini;
}

static size_t get_num_occs(char *haystack, const char *needle)
{
	size_t occurances = 0;
	char *str = haystack;
	char *token = strtok(str, "\n");

	do {
		if(strncmp(token, needle, strlen(needle)) == 0)
			occurances++;
	}
	while((token = strtok(NULL, "\n")) != NULL);

	return occurances;
}

int main(int argc, char *argv[])
{
	size_t entries;
	char *ini;

	if(argc != 3)
	{
		fprintf(stderr,
			"mupenini2dat mupen64plus.ini mupen_rominfo.dat\n");
		return EXIT_FAILURE;
	}

	ini = read_entire_file(argv[1]);
	if(ini == NULL)
		return EXIT_FAILURE;

	/* Obtain number of entries; it gives an idea as to how much memory we
	 * must allocate. */
	entries = get_num_occs(ini, "CRC=");

	printf("Entries: %zu\n", entries);
	/* Load ini file. */
	/* Read each entry. */
	/* Sort ROM entries by CRC. */
	/* Save cheat table,  */

	return EXIT_SUCCESS;
}
