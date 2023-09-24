/**
 * Converts a Mupen64 INI ROM list to a small binary representation of it.
 * Copyright (c) 2020 Mahyar Koshkouei <mk@deltabeard.com>
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

#define _GNU_SOURCE

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define PRINTERR()	\
	fprintf(stderr, "ERR %s:%d %s\n", __func__, __LINE__, strerror(errno))

enum save_types_e
{
	SAVE_EEPROM_4KB,
	SAVE_EEPROM_16KB,
	SAVE_SRAM,
	SAVE_FLASH_RAM,
	SAVE_CONTROLLER_PACK,
	SAVE_NONE
};

const char *save_types_str[] = {
	"SAVE_EEPROM_4KB", "SAVE_EEPROM_16KB", "SAVE_SRAM", "SAVE_FLASH_RAM",
	"SAVE_CONTROLLER_PACK", "SAVE_NONE"
};

struct rom_entry_s
{
	uint64_t crc;

	union rom_conf_u
	{
		struct
		{
			/* Unused value allows for packing entry in exactly 4
			 * bytes. */
			unsigned char do_not_use : 1;
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

			unsigned char mempak : 1;
			unsigned char biopak : 1;

			/* Only Tetris 64 requires this. If 1, then set to
			 * 0x100, otherwise the default of 0x900 is assumed. */
			unsigned char si_dma_duration : 1;

			/* Only "Hey You, Pikachu!" uses this. If set, then
			 * aidmamodifier should be set to 88. */
			unsigned char ai_dma_modifier : 1;
		};
		struct
		{
			/* Does this entry refer to another entry?
			* If it does, look up the rom entry at value reference_entry. */
			unsigned char reference : 1;
			uint16_t reference_entry;
		};
	} conf;

	struct
	{
		char md5[33];
		char refmd5[33];
		uint64_t refcrc;
		char goodname[64];
	} track;
};

char *cheats[32] = { NULL };
size_t cheats_tot = 1;
char *cheats_used_by[32] = { NULL };

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
 * Determine number of entries in ini by searching for occurrences of "\n[".
 */
static size_t get_num_entries(char *haystack)
{
	size_t entries = 0;
	char *str = haystack;

	while((str = strstr(str, "\n[")) != NULL)
	{
		entries++;
		str++;
	}

	return entries;
}

struct rom_entry_s *convert_entries(const char *ini,
                                    const size_t entries)
{
#define strncmplim(hay, needle) memcmp(hay, needle, sizeof(needle)-1)
	struct rom_entry_s *dat = calloc(entries + 1, sizeof(struct rom_entry_s));
	struct rom_entry_s *entry = dat;
	int first = 1;
	const char *line = ini;

	assert(dat != NULL);

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
			/* New entry. */
			/* Compensate for 0-based indexing. */
			if(first)
				first = 0;
			else
			{
				/* Set to new entry. */
				entry++;
			}

			line++;
			memcpy(entry->track.md5, line, 32);
			entry->track.md5[32] = '\0';
		}
		else if(strncmplim(line, "CRC") == 0)
		{
			uint32_t c1, c2;
			char *endptr;

			/* Move to first character after equals. */
			line = strchr(line, '=');
			assert(line != NULL);
			line++;

			c1 = strtoul(line, &endptr, 16);
			assert(*endptr == ' ');

			/* Move to second CRC. */
			line = endptr;

			c2 = strtoul(line, &endptr, 16);
			assert(*endptr == '\n');
			entry->crc = ((uint64_t)c1 << 32) | c2;

			/* Init variables to default values. */
			entry->conf.status = 0;
			entry->conf.save_type = 5;
			entry->conf.players = 4;
			entry->conf.rumble = 1;
			entry->conf.transferpak = 0;
			entry->conf.mempak = 1;
			entry->conf.biopak = 0;
			entry->conf.count_per_op = 2;
			entry->conf.disable_extra_mem = 0;
			entry->conf.si_dma_duration = 0;
			entry->conf.ai_dma_modifier = 0;
		}
		else if(strncmplim(line, "RefMD5") == 0)
		{
			line = strchr(line, '=') + 1;
			entry->conf.reference = 1;
			memcpy(entry->track.refmd5, line, 32);
			entry->track.refmd5[32] = '\0';

			/* TODO: Find reference. */
		}
		else if(strncmplim(line, "SaveType") == 0)
		{
			line = strchr(line, '=');
			assert(line != NULL);
			line++;

			switch(*line)
			{
			case 'E':
				if(*(line + strlen("Eeprom ")) == '4')
					entry->conf.save_type = SAVE_EEPROM_4KB;
				else if(*(line + strlen("Eeprom ")) == '1')
					entry->conf.save_type = SAVE_EEPROM_16KB;
				else
					abort();
				break;
			case 'S':
				entry->conf.save_type = SAVE_SRAM;
				break;

			case 'F':
				entry->conf.save_type = SAVE_FLASH_RAM;
				break;

			case 'C':
				entry->conf.save_type = SAVE_CONTROLLER_PACK;
				break;

			case 'N':
				entry->conf.save_type = SAVE_NONE;
				break;

			default:
				abort();
			}
		}
		else if(strncmplim(line, "Status") == 0)
		{
			char *endptr;
			unsigned status;
			line = strchr(line, '=');
			assert(line != NULL);
			line++;

			status = strtoul(line, &endptr, 10);
			assert(*endptr == '\n');
			assert(status < 6);
			entry->conf.status = status & 0x7;
		}
		else if(strncmplim(line, "Players") == 0)
		{
			char *endptr;
			unsigned players;
			line = strchr(line, '=');
			assert(line != NULL);
			line++;

			players = strtoul(line, &endptr, 10);
			assert(*endptr == '\n');
			assert(players < 8);
			entry->conf.players = players & 0x7;
		}
		else if(strncmplim(line, "Rumble") == 0)
		{
			line = strchr(line, '=');
			assert(line != NULL);
			line++;
			entry->conf.rumble = (*line == 'Y');
		}
		else if(strncmplim(line, "CountPerOp") == 0)
		{
			char *endptr;
			unsigned count_per_op;
			line = strchr(line, '=');
			assert(line != NULL);
			line++;

			count_per_op = strtoul(line, &endptr, 10);
			assert(*endptr == '\n');
			assert(count_per_op <= 4);
			entry->conf.count_per_op = count_per_op & 0x7;
		}
		else if(strncmplim(line, "DisableExtraMem") == 0)
		{
			line = strchr(line, '=');
			assert(line != NULL);
			line++;
			entry->conf.count_per_op = (*line == '1');
		}
		else if(strncmplim(line, "Cheat0") == 0)
		{
			uint8_t cheat_found = 0;
			char *endline = strchr(line, '\n');
			size_t len;
			line = strchr(line, '=');
			assert(line != NULL);
			line++;

			len = endline - line;
			len++; /* For null char. */

			for(size_t ci = 1; ci < cheats_tot; ci++)
			{
				if(memcmp(cheats[ci], line, len - 1) == 0)
				{
					char *tmp;
					asprintf(&tmp, "%s\t * %s\n",
					         cheats_used_by[ci] == NULL ? "" :
							cheats_used_by[ci],
					         entry->track.goodname);
					free(cheats_used_by[ci]);
					cheats_used_by[ci] = tmp;
					cheat_found = ci;
					break;
				}
			}

			if(cheat_found)
			{
				entry->conf.cheat_lut = cheat_found;
				fprintf(stderr, "DEBUG: Cheat for %s found in"
						" entry %d\n",
						entry->track.goodname,
						cheat_found);
				continue;
			}

			cheats[cheats_tot] = malloc(len);
			assert(cheats[cheats_tot] != NULL);
			memcpy(cheats[cheats_tot], line, len);
			cheats[cheats_tot][len - 1] = '\0';
			entry->conf.cheat_lut = cheats_tot;
			asprintf(&cheats_used_by[cheats_tot], "\t * %s\n",
					         entry->track.goodname);

			fprintf(stderr, "DEBUG: Cheat %lld added for %s\n",
					cheats_tot, entry->track.goodname);
			cheats_tot++;
		}
		else if(strncmplim(line, "Transferpak") == 0)
		{
			line = strchr(line, '=');
			assert(line != NULL);
			line++;
			entry->conf.transferpak = (*line == 'Y');
		}
		else if(strncmplim(line, "Mempak") == 0)
		{
			line = strchr(line, '=');
			assert(line != NULL);
			line++;
			entry->conf.biopak = (*line == 'Y');
		}
		else if(strncmplim(line, "Biopak") == 0)
		{
			line = strchr(line, '=');
			assert(line != NULL);
			line++;
			entry->conf.biopak = (*line == 'Y');
		}
		else if(strncmplim(line, "SiDmaDuration") == 0)
		{
			line = strchr(line, '=');
			assert(line != NULL);
			line++;
			assert(*line == '1');
			entry->conf.si_dma_duration = 1;
		}
		else if(strncmplim(line, "AiDmaModifier") == 0)
		{
			unsigned dma_mod;
			line = strchr(line, '=');
			assert(line != NULL);
			line++;
			dma_mod = strtoul(line, NULL, 10);
			if(dma_mod == 88)
				entry->conf.ai_dma_modifier = 1;
			else
			{
				fprintf(stderr, "WARNING: AiDmaModifier of %ul "
						"is not supported\n",
						dma_mod);
			}
		}
		else if(strncmplim(line, "GoodName") == 0)
		{
			char *endline = strchr(line, '\n');
			size_t len;
			line = strchr(line, '=');
			assert(line != NULL);
			line++;

			len = endline - line;
			if(len >= 64)
				len = 63;

			assert(entry->track.goodname != NULL);
			memcpy(entry->track.goodname, line, len);
			entry->track.goodname[len] = '\0';
		}
		else if(*line != '\0')
		{
			char *endline = strchr(line, '\n');
			int len = endline - line;
			fprintf(stderr, "WARNING: Unknown key '%.*s'\n",
					len, line);
		}
	}

	/* All entries begin with a CRC value. */
	return dat;
}

void dump_header(const char *filename, struct rom_entry_s *e, size_t entries)
{
	FILE *f = fopen(filename, "wb");
	time_t now = time(NULL);
	struct tm *tmp;
	char time_str[128];
	assert(f != NULL);

	tmp = localtime(&now);
	assert(tmp != NULL);

	assert(strftime(time_str, sizeof(time_str), "%c", tmp) != 0);

	fprintf(f, "/* Generated at %s using mupenini2dat */\n\n", time_str);
	fprintf(f, "#pragma once\n");
	fprintf(f, "#include <stdint.h>\n\n");

	fprintf(f, "struct rom_entry_s\n"
		"{\n"
		"\tunion\n"
		"\t{\n"
		"\t\tstruct\n"
		"\t\t{\n"
		"\t\t\tunsigned char do_not_use : 1;\n"
		"\t\t\tunsigned char save_type : 3;\n"
		"\t\t\tunsigned char players : 3;\n"
		"\t\t\tunsigned char rumble : 1;\n"
		"\t\t\tunsigned char transferpak : 1;\n"
		"\t\t\tunsigned char status : 3;\n"
		"\t\t\tunsigned char count_per_op : 3;\n"
		"\t\t\tunsigned char disable_extra_mem : 1;\n"
		"\t\t\tunsigned char cheat_lut : 5;\n"
		"\t\t\tunsigned char mempak : 1;\n"
		"\t\t\tunsigned char biopak : 1;\n"
		"\t\t\tunsigned char si_dma_duration : 1;\n"
		"\t\t\tunsigned char ai_dma_modifier : 1;\n"
		"\t\t};\n"
		"\t\tstruct\n"
		"\t\t{\n"
		"\t\t\tunsigned char reference : 1;\n"
		"\t\t\tuint16_t reference_entry;\n"
		"\t\t};\n"
		"\t};\n"
		"};\n\n");

	fprintf(f, "enum save_types_e\n"
		"{\n"
		"\tSAVE_EEPROM_4KB = 0,\n"
		"\tSAVE_EEPROM_16KB,\n"
		"\tSAVE_SRAM,\n"
		"\tSAVE_FLASH_RAM,\n"
		"\tSAVE_CONTROLLER_PACK,\n"
		"\tSAVE_NONE\n"
		"};\n\n");

	fprintf(f, "const uint64_t rom_crc[%zu] = {\n\t", entries);
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

		fprintf(f, "0x%016"PRIX64"%s", e[i].crc,
		        i == (entries - 1) ? "" : ",");
	}
	fprintf(f, "\n};\n\n");

	fprintf(f, "const struct rom_entry_s rom_dat[%zu] = {\n", entries);
	struct rom_entry_s *last = e + entries;
	for(struct rom_entry_s *i = e; i < last; i++)
	{
		fprintf(f, "\t/* %s\n", i->track.goodname);
		fprintf(f, "\t * CRC: %08"PRIX32" %08"PRIX32"\n",
				(uint32_t)(i->crc >> 32),
				(uint32_t)(i->crc & 0xFFFFFFFF));
		fprintf(f, "\t * Entry: %zu */\n", i - e);
		fprintf(f, "\t{\n");

		/* This entry refers to another. */
		if(i->conf.reference == 1)
		{
			unsigned ref_i;
			for(ref_i = 0; ref_i < entries; ref_i++)
			{
				if(i->track.refcrc == e[ref_i].crc)
					break;
			}

			/* Check if reference actually exists. If it doesn't, it
			 * probably used default values. */
			if(i->track.refcrc != e[ref_i].crc)
				continue;

			fprintf(f, "\t\t.reference = %u,\n", i->conf.reference);
			fprintf(f, "\t\t.reference_entry = %u\n", ref_i);
			fprintf(f, "\t}%s\n", i == (last - 1) ? "" : ",");
			continue;
		}

		fprintf(f, "\t\t.status = %u,\n", i->conf.status);
		fprintf(f, "\t\t.save_type = %s,\n", save_types_str[i->conf.save_type]);
		fprintf(f, "\t\t.players = %u,\n", i->conf.players);
		fprintf(f, "\t\t.rumble = %u,\n", i->conf.rumble);
		fprintf(f, "\t\t.transferpak = %u,\n", i->conf.transferpak);
		fprintf(f, "\t\t.mempak = %u,\n", i->conf.mempak);
		fprintf(f, "\t\t.biopak = %u,\n", i->conf.biopak);
		fprintf(f, "\t\t.count_per_op = %u,\n", i->conf.count_per_op);
		fprintf(f, "\t\t.disable_extra_mem = %u,\n", i->conf.disable_extra_mem);
		fprintf(f, "\t\t.si_dma_duration = %u,\n", i->conf.si_dma_duration);
		fprintf(f, "\t\t.ai_dma_modifier = %u,\n", i->conf.ai_dma_modifier);
		fprintf(f, "\t\t.cheat_lut = %u,\n", i->conf.cheat_lut);
		fprintf(f, "\t}%s\n", i == (last - 1) ? "" : ",");
	}
	fprintf(f, "};\n");

	if(cheats_tot == 0)
		goto out;

	fprintf(f, "const char *const cheats[%zu] = {\n", cheats_tot);
	fprintf(f, "\t\"\",\n");
	for(size_t i = 1; i < cheats_tot; i++)
	{
		if(cheats_used_by[i] != NULL)
		{
			fprintf(f, "%s\t/**\n%s\t */\n", i == 0 ? "" : "\n",
				cheats_used_by[i]);
		}

		fprintf(f, "\t\"%s\"%s\n", cheats[i],
			i == (entries - 1) ? "" : ",");
	}
	fprintf(f, "};\n");

out:
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

	return ((int)e1->conf.reference - (int)e2->conf.reference);
}

void remove_dupes(struct rom_entry_s *first, size_t *entries)
{
	struct rom_entry_s *r = calloc(*entries, sizeof(struct rom_entry_s));
	struct rom_entry_s *r_i = r;
	struct rom_entry_s *last = first + *entries;

	assert(r != NULL);

	for(struct rom_entry_s *e = first; e < last; e++)
	{
		memcpy(r_i++, e, sizeof(*e));

		while(e->crc == (e + 1)->crc)
		{
			if((e + 1)->conf.reference == 0)
			{
				memcpy(r_i, e + 1, sizeof(*e));
			}

			e++;
		}
	}

	*entries = (r_i - r);
	memcpy(first, r, *entries * sizeof(*r));
	last = first + *entries;

	/* Remove entries that only use defaults. */
	r_i = r;
	for(struct rom_entry_s *e = first; e < last; e++)
	{
		if(e->conf.status != 0 || e->conf.save_type != SAVE_NONE ||
			e->conf.players != 4 || e->conf.rumble != 1 ||
			e->conf.transferpak != 0 || e->conf.mempak != 1 ||
			e->conf.biopak != 0 || e->conf.count_per_op != 2 ||
			e->conf.disable_extra_mem != 0 ||
			e->conf.si_dma_duration != 0 || e->conf.reference != 1)
		{
			memcpy(r_i++, e, sizeof(*e));
		}
	}

	*entries = (r_i - r);
	memcpy(first, r, *entries * sizeof(*r));
	free(r);
}

void dump_filtered_ini(struct rom_entry_s *all, size_t entries)
{
	FILE *f = fopen("fil.ini", "w");

	assert(f != NULL);

	for(size_t i = 0; i < entries; i++)
	{
		fprintf(f, "[%s]\n", all[i].track.md5);
		fprintf(f, "GoodName=%s\n", all[i].track.goodname);
		fprintf(f, "CRC=0x%016"PRIX64"\n", all[i].crc);
		if(all[i].conf.reference)
			fprintf(f, "RefMD5=%s\n", all[i].track.refmd5);

		fprintf(f, "\n");
	}

	fclose(f);
}

void resolve_deps(struct rom_entry_s *all, size_t entries)
{
	for(struct rom_entry_s *e = all; e < all + entries; e++)
	{
		uint16_t i;

		if(e->conf.reference == 0)
			continue;

		for(i = 0; i < entries; i++)
		{
			if(memcmp(e->track.refmd5, all[i].track.md5, 32) == 0)
				break;
		}

		e->conf.reference_entry = i;
		e->track.refcrc = all[i].crc;
	}
}

int main(int argc, char *argv[])
{
	size_t entries;
	char *ini;
	struct rom_entry_s *all;

	if(argc != 3)
	{
		fprintf(stderr,
		        "Usage: mupenini2dat mupen64plus.ini rom_dat.h\n");
		return EXIT_FAILURE;
	}

	/* Load ini file. */
	ini = read_entire_file(argv[1]);
	if(ini == NULL)
		return EXIT_FAILURE;

	/* Obtain number of entries; it gives an idea as to how much memory we
	 * must allocate. */
	entries = get_num_entries(ini);

	printf("Processing %zu entries\n", entries);
	all = convert_entries(ini, entries);

	qsort(all, entries, sizeof(*all), compare_entry);
	resolve_deps(all, entries);
	remove_dupes(all, &entries);
	dump_header(argv[2], all, entries);

	dump_filtered_ini(all, entries);

	/* Free allocations. */
	for(size_t i = 1; i < cheats_tot; i++)
	{
		free(cheats_used_by[i]);
		free(cheats[i]);
	}

	free(all);
	free(ini);

	return EXIT_SUCCESS;
}
// kate: indent-mode cstyle; indent-width 8; replace-tabs off; tab-width 8;
