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

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define PRINTERR()	\
	fprintf(stderr, "ERR %s:%d %s\n", __func__, __LINE__, strerror(errno))

struct rom_entry_s
{
	uint64_t crc;

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
	} conf __attribute__((packed));

	struct {
		char md5[33];
		char *cheat;
	} track;
};

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

/**
 * Checks for number of occurrences of the word at the start of each line.
 */
static size_t get_num_occs(const char *haystack, const char *needle)
{
	size_t occurances = 0;
	char *str = strdup(haystack);
	char *token = strtok(str, "\n");

	do {
		if(strncmp(token, needle, strlen(needle)) == 0)
			occurances++;
	}
	while((token = strtok(NULL, "\n")) != NULL);

	free(str);
	return occurances;
}

struct rom_entry_s *convert_entries(const char *ini,
				    const size_t entries)
{
	#define strncmplim(hay, needle) strncmp(hay, needle, strlen(needle))
	struct rom_entry_s *dat = calloc(entries, sizeof(struct rom_entry_s));
	struct rom_entry_s *entry = dat;
	int first = 1;
	const char *line = ini;

	if(dat == NULL)
		goto out;

	while((line = strchr(line, '\n')) != NULL)
	{
		line++;

		/* Skip if empty line. */
		if(*line == '\n')
			continue;
		/* Skip if comment. */
		else if(*line == ';')
			continue;
		else if(strncmplim(line, "[") == 0)
		{
			line++;
			strncpy(entry->track.md5, line, 32);
			entry->track.md5[32] = '\0';
		}
		else if(strncmplim(line, "CRC") == 0)
		{
			uint32_t c1, c2;
			char *endptr;
			/* Compensate for 0-based indexing. */
			if(first)
				first = 0;
			else
			{
				/* Set to new entry. */
				entry++;
			}

			/* Move to first character after equals. */
			line = strchr(line, '=');
			assert(line != NULL);
			line++;

			c1 = strtoll(line, &endptr, 16);
			assert(*endptr == ' ');

			/* Move to second CRC. */
			line = endptr;

			c2 = strtoll(line, &endptr, 16);
			assert(*endptr == '\n');
			entry->crc = ((uint64_t)c1 << 32) | c2;
		}
		else if(strncmplim(line, "RefMD5") == 0)
		{
			line = strchr(line, '=') + 1;
			entry->conf.reference = 1;

			/* TODO: Find reference. */
		}
		else if(strncmplim(line, "SaveType") == 0)
		{
			line = strchr(line, '=') + 1;
			switch(*line)
			{
			case 'E':
				if(*(line + strlen("Eeprom ")) == '4')
					entry->conf.save_type = 0;
				else if(*(line + strlen("Eeprom ")) == '1')
					entry->conf.save_type = 1;
				else
					abort();
				break;
			case 'S':
				entry->conf.save_type = 2;
				break;

			case 'F':
				entry->conf.save_type = 3;
				break;

			case 'C':
				entry->conf.save_type = 4;
				break;

			case 'N':
				entry->conf.save_type = 5;
				break;

			default:
				abort();
			}
		}
		else if(strncmplim(line, "Status") == 0)
		{
			char *endptr;
			long int status;
			line = strchr(line, '=') + 1;
			status = strtol(line, &endptr, 10);
			assert(*endptr == '\n');
			assert(status < 6);
			assert(status >= 0);
			entry->conf.status = status;
		}
		else if(strncmplim(line, "Players") == 0)
		{
			char *endptr;
			long int players;
			line = strchr(line, '=') + 1;
			players = strtol(line, &endptr, 10);
			assert(*endptr == '\n');
			assert(players < 8);
			assert(players >= 0);
			entry->conf.players = players;
		}
		else if(strncmplim(line, "Rumble") == 0)
		{
			line = strchr(line, '=') + 1;
			entry->conf.rumble = (*line == 'Y');
		}
		else if(strncmplim(line, "CountPerOp") == 0)
		{
			char *endptr;
			long int count_per_op;
			line = strchr(line, '=') + 1;
			count_per_op = strtol(line, &endptr, 10);
			assert(*endptr == '\n');
			assert(count_per_op <= 4);
			assert(count_per_op > 0);
			entry->conf.count_per_op = count_per_op;
		}
		else if(strncmplim(line, "DisableExtraMem") == 0)
		{
			line = strchr(line, '=') + 1;
			entry->conf.count_per_op = (*line == '1');
		}
		else if(strncmplim(line, "Cheat0") == 0)
		{
			char *endline = strchr(line, '\n');
			size_t len;
			line = strchr(line, '=') + 1;

			len = endline - line;
			len++; /* For null char. */
			entry->track.cheat = malloc(len);
			assert(entry->track.cheat != NULL);
			strncpy(entry->track.cheat, line, len);
			entry->track.cheat[len - 1] = '\0';
		}
		else if(strncmplim(line, "Transferpak") == 0)
		{
			line = strchr(line, '=') + 1;
			entry->conf.transferpak = (*line == 'Y');
		}
		else if(strncmplim(line, "Mempak") == 0)
		{
			line = strchr(line, '=') + 1;
			entry->conf.biopak = (*line == 'Y');
		}
		else if(strncmplim(line, "Biopak") == 0)
		{
			line = strchr(line, '=') + 1;
			entry->conf.biopak = (*line == 'Y');
		}
		else if(strncmplim(line, "SiDmaDuration") == 0)
		{
			line = strchr(line, '=') + 1;
			assert(*line == '1');
			entry->conf.si_dma_duration = 1;
		}
		else if(strncmplim(line, "GoodName") == 0)
		{
			/* Ignoring this key. */
		}
		else if(*line != '\0')
		{
			fprintf(stderr, "Unknown key %.32s\n", line);
			abort();
		}
	}

	/* All entries begin with a CRC value. */

out:
	return dat;
}

void dump_crc_header(const char *filename, struct rom_entry_s *e, size_t entries)
{
	FILE *f = fopen(filename, "wb");
	time_t now = time(NULL);
	struct tm *tmp;
	char time_str[128];
	assert(f != NULL);

	tmp = localtime(&now);
	assert(tmp != NULL);

	if (strftime(time_str, sizeof(time_str), "%c", tmp) == 0)
		abort();

	fprintf(f, "/* Generated at %s using mupenini2dat */\n\n", time_str);
	fprintf(f, "const uint64_t[%zu] = {\n\t", entries);

	for(size_t i = 0; i < entries; i++)
	{
		if(i != 0 && i % 3 == 0)
		{
			fprintf(f, "\n\t");
		}
		else if(i != 0)
		{
			fprintf(f, " ");
		}

		fprintf(f, "0x%016lX%s", e[i].crc,
			i == (entries-1) ? "" : ",");
	}

	fprintf(f, "\n}\n");
	fclose(f);
}

int compare_entry(const void *in1, const void *in2)
{
	const struct rom_entry_s *e1 = in1;
	const struct rom_entry_s *e2 = in2;

	if(((__int128)e1->crc - (__int128)e2->crc) < 0)
		return -1;

	if(((__int128)e1->crc - (__int128)e2->crc) > 0)
		return 1;

	return 0;
}

void remove_dupes(struct rom_entry_s *all, size_t entries)
{

}

int main(int argc, char *argv[])
{
	size_t entries;
	char *ini;
	struct rom_entry_s *all;

	if(argc != 4)
	{
		fprintf(stderr,
			"mupenini2dat mupen64plus.ini rom.dat crc.h\n");
		return EXIT_FAILURE;
	}

	/* Load ini file. */
	ini = read_entire_file(argv[1]);
	if(ini == NULL)
		return EXIT_FAILURE;

	/* Obtain number of entries; it gives an idea as to how much memory we
	 * must allocate. */
	entries = get_num_occs(ini, "CRC=");

	printf("Processing %zu entries\n", entries);

	all = convert_entries(ini, entries);
	qsort(all, entries, sizeof(*all), compare_entry);
	remove_dupes(all, entries);
	dump_crc_header(argv[3], all, entries);

	/* Read each entry. */

	/* Sort ROM entries by CRC. */
	/* Save cheat table,  */

	return EXIT_SUCCESS;
}
