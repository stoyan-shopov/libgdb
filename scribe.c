/*

Copyright (C) 2011-2012 stoyan shopov

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/


/*
 * include section follows
 */

#include <stdint.h>
#include <sys/time.h>
#include <stdbool.h>
#include <malloc.h>
#include <setjmp.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>
#include <ctype.h>

#include <windows.h>

#include "libgdb.h"
#include "devctl.h"
#include "devices.h"
#include "hexreader.h"


static bool is_vx_annotation_enabled;

static void list_devices(struct struct_devctl * devs, bool vx_annotate)
{
/* list devices */
struct struct_devctl * p;
int i;
	printf("list of supported devices:\n");
	for (p = devs; p; p = p->next)
	{
		printf("%s%s\n", vx_annotate ? "[VX-DEVLIST-ENTRY]" : "", p->name);
		/* dump ram areas */
		for (i = 0; p->ram_areas[i].len; i++)
		{
			printf(vx_annotate ? "%s\t%s\t%i\t%s\t%i\n" : "%s\t%s\t0x%08x\t%s\t0x%08x\n",
				vx_annotate ? "[VX-RAM-AREA]" : "ram region",
				vx_annotate ? "" : "start",
				p->ram_areas[i].start,
				vx_annotate ? "" : "length",
				p->ram_areas[i].len);
		}
		/* dump flash areas */
		for (i = 0; p->flash_areas[i].len; i++)
		{
			printf(vx_annotate ? "%s\t%s\t%i\t%s\t%i\n" : "%s\t%s\t0x%08x\t%s\t0x%08x\n",
				vx_annotate ? "[VX-FLASH-AREA]" : "flash region",
				vx_annotate ? "" : "start",
				p->flash_areas[i].start,
				vx_annotate ? "" : "length",
				p->flash_areas[i].len);
		}
	}
}

static int vxprinterr(const char * format, ...)
{
va_list ap;

	va_start(ap, format);
	if (is_vx_annotation_enabled)
		printf("[VX-ERROR]");
	vprintf(format, ap);
	va_end(ap);
}

static int check_device_cmdline_options(struct struct_devctl * dev)
{
int i;
	if (dev->cmdline_options)
		for (i = 0; dev->cmdline_options[i].cmdstr; i ++)
		{
			if (dev->cmdline_options[i].is_mandatory && !dev->cmdline_options[i].is_specified)
			{
				eprintf("mandatory command line option '%s' for target '%s' not specified, aborting\n",
					dev->cmdline_options[i].cmdstr, dev->name);
				return -1;
			}
		}
	/* command line options ok */
	return 0;
}

static int fill_in_cmdline_option(struct struct_devctl * dev, const char * cmdstr)
{
struct cmdline_option_info * p;
char * s, * valstr;

	p = dev->cmdline_options;
	if (!p || !p->cmdstr)
	{
		eprintf("command line option '%s' for target '%s' not recognized, aborting\n", cmdstr, dev->name);
		return -1;
	}
	s = strdup(cmdstr);
	valstr = strtok(s, "=");
	valstr = strtok(0, "=");
	if (!valstr || !*valstr)
	{
		free(s);
		eprintf("bad command line option string ('%s'), command line option string must be of the form 'option=value'; aborting\n", cmdstr);
		return -1;
	}
	for (p = dev->cmdline_options; p->cmdstr; p ++)
	{
		if (!strcmp(s, p->cmdstr))
		{
			if (p->type == PARAM_TYPE_STRING)
			{
				p->is_specified = true;
				p->str = strdup(valstr);
				free(s);
				return 0;
			}
			if (p->type == PARAM_TYPE_NUMERIC)
			{
				const char * s1;
				uint32_t x;
				x = strtol(valstr, & s1, 0);
				if (* s1)
				{
					free(s);
					eprintf("bad numeric value ('%s') for command line option '%s' for target '%s', aborting\n", valstr, p->cmdstr, dev->name);
					exit(1);
				}
				p->num = x;
				free(s);
				return 0;
			}
			else
			{
				free(s);
				eprintf("unknown type for command line option '%s' for target '%s', aborting\n", p->cmdstr, dev->name);
				return -1;
			}
		}
	}
	free(s);
	eprintf("command line option '%s' for target '%s' not recognized, aborting\n", cmdstr, dev->name);
	return -1;
}

static int open_device(struct struct_devctl * dev, struct libgdb_ctx * ctx)
{
int res;	
	if (!dev)
	{
		eprintf("target not specified, specify a target with '-d device'; aborting\n");
		return -1;
	}
	if (!dev->dev_open)
		return 0;
	if (check_device_cmdline_options(dev))
		return -1;
	if ((res = dev->dev_open(dev, ctx)))
	{
		eprintf("error opening target, aborting\n");
	}
	return res;
}


static int get_hex_fname(const char * infile, char ** outfile, bool * must_unlink)
{
char cmdline[512];
DWORD exit_code;
STARTUPINFO si;
PROCESS_INFORMATION pi;
int fd;
unsigned char x[4];

	* outfile = 0;
	* must_unlink = false;

	/* try to open the input file and detect its format */
	if ((fd = open(infile, O_RDONLY)) == -1)
	{
		eprintf("error opening input file %s\n", infile);
		return -1;
	}
	if (read(fd, x, sizeof x) != sizeof x)
	{
		eprintf("error reading input file %s (read error or file too small)\n", infile);
		return -1;
	}
	close(fd);

	if (x[0] == ':' && isalnum(x[1]) && isalnum(x[2]) && isalnum(x[3]))
	{
		/* looks like an ihex file - say it is such */
		* must_unlink = false;
		* outfile = strdup(infile);
		return 0;
	}
	else if (x[0] == 0x7f && x[1] == 'E' && x[2] == 'L' && x[3] == 'F')
	{
		/* looks like an elf file - try to make an ihex file out of it with the help of objcopy */
		int i;
		/* make an output file name by concatenating an ".ihex" suffix to the input file name */
		i = strlen(infile);
		i += 5 + 1;
		if (!(* outfile = malloc(i)))
		{
			eprintf("out of core\n");
			return -1;
		}
		**outfile = 0;
		strcat(*outfile, infile);
		strcat(*outfile, ".ihex");

		/* build the command line */
		snprintf(cmdline, sizeof cmdline, "objcopy -O ihex \"%s\" \"%s\"", infile, *outfile);
		memset(&pi, 0, sizeof pi);
		memset(&si, 0, sizeof si);
		if (!(CreateProcess(0,
		                    cmdline,
		                    0,
		                    0,
		                    FALSE, /* do not inherit handles */
		                    0,
		                    0,
		                    0,
		                    &si,
		                    &pi)))
		{
			eprintf("failed to run objcopy to create the output ihex file\n");
			eprintf("make sure that objcopy is available, and in your PATH\n");
			free(*outfile);
			return -1;
		}
		if (WaitForSingleObject(pi.hProcess, INFINITE) != WAIT_OBJECT_0)
		{
			eprintf("error waiting for objcopy to finish\n");
			free(*outfile);
			return -1;
		}
		if (!GetExitCodeProcess(pi.hProcess, &exit_code))
		{
			eprintf("error retrieving objcopy exit code\n");
			free(*outfile);
			return -1;
		}
		if (exit_code)
		{
			eprintf("error executing objcopy - objcopy returned %i\n", (int) exit_code);
			free(*outfile);
			return -1;
		}
		* must_unlink = true;
	}
	else
	{
		/* file format not recognized */
		eprintf("could not make an ihex file out of file %s, file format not recognized\n", infile);
		return -1;
	}
	return 0;
}

static const struct struct_memarea * locate_mem_area(const struct struct_memarea * areas, uint32_t start_addr)
{
	while (areas->len)
		if (areas->start <= start_addr && start_addr < areas->start + areas->len)
			break;
		else
			areas ++;
	if (areas->len)
		return areas;
	else
		return 0;
}

static int get_mem_type(struct struct_devctl * dev, struct libgdb_ctx * ctx, uint32_t start_addr, uint32_t len)
{
const struct struct_memarea * m;
int memtype;

	if (m = locate_mem_area(dev->ram_areas, start_addr))
		memtype = MEM_TYPE_RAM;
	else if (m = locate_mem_area(dev->flash_areas, start_addr))
		memtype = MEM_TYPE_FLASH;
	else
		return MEM_TYPE_INVALID;
	if (m->start <= start_addr && start_addr + len <= m->start + m->len)
		return memtype;
	else
		return MEM_TYPE_INVALID;
}


static int get_flash_area_info(struct struct_devctl * dev, struct libgdb_ctx * ctx, uint32_t start_addr, uint32_t len,
                             const struct struct_memarea ** mem_area, int * start_sector_nr, int * nr_sectors)
{
/* locate starting sector number */
int i, j;
uint32_t addr, end_addr;
const struct struct_memarea ** s;

	if (mem_area)
		* mem_area = 0;
	if (start_sector_nr)
		* start_sector_nr = -1;
	if (nr_sectors)
		* nr_sectors = -1;

	end_addr = start_addr + len;
	for (s = &dev->flash_areas; *s; s ++)
	{
		addr = (*s)->start;
		for (i = 0; (*s)->sizes[i]; i ++)
		{
			if (start_addr <= addr && addr < end_addr)
				break;
			addr += (*s)->sizes[i];
		}
		if (start_addr <= addr && addr < end_addr)
			break;
	}
	if (!(start_addr <= addr && addr < end_addr))
		/* address not found */
		return -2;

	j = i;
	do
	{
		addr += (*s)->sizes[i];
		if (!(start_addr <= addr && addr < end_addr))
			break;
		i ++;
	}
	while ((*s)->sizes[i]);
	if (start_addr <= addr && addr < end_addr)
		/* requested flash area too large */
		return -3;
	if (mem_area)
		* mem_area = * s;
	if (start_sector_nr)
		* start_sector_nr = j;
	if (nr_sectors)
		* nr_sectors = i - j + 1;
	return 0;
}

static int generic_flash_erase_area(struct struct_devctl * dev, struct libgdb_ctx * ctx, uint32_t start_addr, uint32_t len)
{
/* locate starting sector number */
int i, sector_nr, cnt;

	if (!dev->flash_erase_sector)
	{
		eprintf("flash_erase_sector() routine unavailable, aborting\n");
		return -1;
	}
	if (!len)
		/* nothing to do */
		return 0;
	if (get_flash_area_info(dev, ctx, start_addr, len, 0, & sector_nr, & cnt))
		return -1;
	for (i = 0; i < cnt; i ++, sector_nr ++)
	{
		if (dev->flash_erase_sector(dev, ctx, sector_nr))
			return -1;
	}
	return 0;
}


static int generic_flash_mass_erase(struct struct_devctl * dev, struct libgdb_ctx * ctx)
{
/* locate starting sector number */
int i, n;
const struct struct_memarea * m;

	for (n = 0, m = dev->flash_areas; m->len; m ++)
	{
		for (i = 0; m->sizes[i]; i ++, n ++)
		{
			if (dev->flash_erase_sector(dev, ctx, n))
				return -1;
		}
	}

	return 0;
}


int main(int argc, char ** argv)
{
struct libgdb_ctx * ctx;
int i, argnr;
uint32_t * buf, * rbuf, addr;
int wordcnt;
struct timeval tv1, tv2;
struct timezone tz;
int diff;
double dx;
bool is_target_connected;
const char * devname;
struct struct_devctl * devs, * pdev;


void dump_target_regfile(void)
{
uint32_t reg;
int i;

	printf("target register file:\n");
	for (i = 0; i < 16; i ++)
	{
		if (libgdb_readreg(ctx, i, &reg))
		{
			eprintf("error reading register %i\n", i);
			exit(2);
		}
		printf("r%i: 0x%08x, ", i, reg);
	}
	/* read xpsr */
	if (libgdb_readreg(ctx, 16, &reg))
	{
		eprintf("error reading xpsr\n");
		exit(2);
	}
	printf("xpsr: 0x%08x, ", reg);
	/* read main stack pointer (msp) */
	if (libgdb_readreg(ctx, 17, &reg))
	{
		eprintf("error reading msp\n");
		exit(2);
	}
	printf("msp: 0x%08x, ", reg);
	/* read process stack pointer (psp) */
	if (libgdb_readreg(ctx, 18, &reg))
	{
		eprintf("error reading psp\n");
		exit(2);
	}
	printf("psp: 0x%08x, ", reg);
	/* read control, primask, faultmask, basepri */
	if (libgdb_readreg(ctx, 19, &reg))
	{
		eprintf("error reading register 20\n");
		exit(2);
	}
	printf("control: 0x%08x, ", reg >> 24);
	printf("faultmask: 0x%08x, ", (reg >> 16) & 255);
	printf("basepri: 0x%08x, ", (reg >> 8) & 255);
	printf("primask: 0x%08x, ", (reg >> 0) & 255);
	printf("\n");
}

void connect_to_target(void)
{
	if (is_target_connected)
		return;
	is_target_connected = true;
	if (!(ctx = libgdb_init()))
	{
		eprintf("failed to initialize the libgdb library\n");
		exit(1);
	}
	if (is_vx_annotation_enabled)
		libgdb_set_annotation(ctx, true);
	if (libgdb_connect(ctx, "127.0.0.1", 1122))
	{
		eprintf("failed to connect to a gdb server\n");
		exit(2);
	}

	libgdb_send_ack(ctx);
	libgdb_sendpacketraw(ctx, "c");
	libgdb_sendbreak(ctx);
	libgdb_waithalted(ctx);
	libgdb_set_max_nr_words_xferred(ctx, 2);
	libgdb_set_max_nr_words_xferred(ctx, 67);
	libgdb_set_max_nr_words_xferred(ctx, 67 * /* this seems to be optimal */ 11);
	libgdb_set_max_nr_words_xferred(ctx, 67 * 11);

	libgdb_set_max_nr_words_xferred(ctx, 300);

	libgdb_set_max_nr_words_xferred(ctx, 67 * 11);
}

struct struct_devctl * merge_dev_lists(struct struct_devctl * l1, struct struct_devctl * l2)
{
struct struct_devctl * d;

	if (!l1)
		return l2;
	if (!l2)
		return l1;
	for (d = l1; d->next; d = d-> next);
	d->next = l2;
	return l1;
}

struct struct_devctl * find_device(struct struct_devctl * devs, const char * devname)
{
	while (devs && strcmp(devs->name, devname))
		devs = devs->next;
	return devs;
}

	if (0) test_objcopy("c:/shopov/src/vxgen0/vxgen0.elf", "c:/shopov/vx.hex");

	/* build a list of supported devices */
	devs = 0;
	devs = merge_dev_lists(devs, stm32f10x_get_devs());
	devs = merge_dev_lists(devs, stm32f4x_get_devs());
	devs = merge_dev_lists(devs, stm32f0x_get_devs());
	devs = merge_dev_lists(devs, lpc17xx_get_devs());
	is_target_connected = false;
	is_vx_annotation_enabled = false;

	devname = 0;
	pdev = 0;

	for (argnr = 1; argnr < argc; )
	{
		if (!strcmp(argv[argnr], "--help") || !strcmp(argv[argnr], "-h"))
		{
			/* print usage infiormation */
			printf("usage: %s [--enable-vx-annotation] [-h|--help] -d device-name [--erase-sector sector-number] [-l] [--regs] [-r addr wordcnt outfile] [-w addr infile] [--erase-area addr len] [-x hexfile] [-t] [-e] [--cont] [--stop]\n", * argv);
			exit(0);
		}
		else if (!strcmp(argv[argnr], "--enable-vx-annotation"))
		{
			argnr ++;
			is_vx_annotation_enabled = true;
		}
		else if (!strcmp(argv[argnr], "--regs"))
		{
			argnr ++;
			is_target_connected = true;
			if (!(ctx = libgdb_init()))
			{
				eprintf("failed to initialize the libgdb library\n");
				exit(1);
			}
			if (libgdb_connect(ctx, "127.0.0.1", 1122))
			{
				eprintf("failed to connect to a gdb server\n");
				exit(2);
			}

			libgdb_send_ack(ctx);
			libgdb_sendpacketraw(ctx, "c");
			libgdb_sendbreak(ctx);
			libgdb_waithalted(ctx);
			libgdb_set_max_nr_words_xferred(ctx, 2);
			libgdb_set_max_nr_words_xferred(ctx, 67);

			dump_target_regfile();
			return 0;

		}
		else if (!strcmp(argv[argnr], "-l"))
		{
			/* list devices */
			argnr ++;
			list_devices(devs, is_vx_annotation_enabled);
		}
		else if (!strcmp(argv[argnr], "-d"))
		{
			/* specify device */
			argnr ++;
			if (argnr == argc)
			{
				eprintf("missing device name\n");
				exit(1);
			}
			devname = argv[argnr ++];
			if (!(pdev = find_device(devs, devname)))
			{
				eprintf("unknown device name (%s); type '%s -l' to get a list of supported devices\n", devname, * argv);
				exit(1);
			}
		}



		else if (!strcmp(argv[argnr], "--hack-test"))
		{
			/* perform hack tests */
			argnr ++;
			if (!pdev)
			{
				eprintf("device not specified, use the '-d' switch to specify a target device\n");
				exit(1);
			}
			if (!pdev->ram_areas)
			{
				eprintf("device does not have ram areas defined, unable to perform memory read/write speed tests, aborting\n");
				exit(1);
			}
			connect_to_target();
			if (libgdb_readwords(ctx, 0x400fc080, 3, (uint32_t[3]){}))
				printf("hack tests failed!!!\n");
			else
				printf("hack tests succeeded\n");


		}



		else if (!strcmp(argv[argnr], "--hack"))
		{
			uint32_t x[3];

			if (!pdev)
			{
				eprintf("device not specified, use the '-d' switch to specify a target device\n");
				exit(1);
			}
			if (!pdev->ram_areas)
			{
				eprintf("device does not have ram areas defined, unable to perform memory read/write speed tests, aborting\n");
				exit(1);
			}
			connect_to_target();
			if (libgdb_readwords(ctx, 0x20000000 + 0x1ff4, 3, x))
			{
				printf("error\n");
				exit(1);
			}
			printf("0x%x 0x%x 0x%x\n", 0[x], 1[x], 2[x]);
			exit(1);

		}


		else if (!strcmp(argv[argnr], "-t"))
		{
			/* perform memory read/write speed tests */
			argnr ++;
			if (!pdev)
			{
				eprintf("device not specified, use the '-d' switch to specify a target device\n");
				exit(1);
			}
			if (!pdev->ram_areas)
			{
				eprintf("device does not have ram areas defined, unable to perform memory read/write speed tests, aborting\n");
				exit(1);
			}
			/* use the first device ram area to do the tests */
			addr = pdev->ram_areas[0].start;
			wordcnt = pdev->ram_areas[0].len >> 2;

			if (!wordcnt)
			{
				eprintf("invalid device ram area length, unable to perform memory read/write speed tests, aborting\n");
				exit(1);
			}

			if (!((buf = malloc(wordcnt * sizeof(uint32_t))) && (rbuf = calloc(wordcnt, sizeof(uint32_t)))))
			{
				eprintf("cannot allocate memory for tests, unable to perform memory read/write speed tests, aborting\n");
				exit(1);
			}
			/* initialize test pattern */
			for (i = 0; i < wordcnt; i ++)
				buf[i] = i;

			connect_to_target();

#if 1
			/* memory write test */
			printf("performing memory write test...\n");
			gettimeofday(&tv1, &tz);
			if (libgdb_writewords(ctx, addr, wordcnt, buf))
			{
				eprintf("error writing target memory\n");
				exit(2);
			}
			gettimeofday(&tv2, &tz);

			diff = tv2.tv_sec - tv1.tv_sec;
			diff *= 1000000;
			diff += (int) tv2.tv_usec - (int) tv1.tv_usec;
			printf("\n\n\nwrite speed:\n");
			printf("%i bytes written in %i.%i seconds\n", wordcnt * 4, (diff + 5000) / 1000000, ((diff + 5000) % 1000000) / 10000);
			dx = ((double) (wordcnt * 4)) / (double) diff;
			dx *= 1000000.;
			printf("average speed: %i.%i bytes/second\n", (int)(dx), (int)((fmod(dx, 1.)) * 100.));
			printf("\n\n\n");
#endif
#if 1
			/* memory read test */
			printf("performing memory read test...\n");
			gettimeofday(&tv1, &tz);
			if (libgdb_readwords(ctx, addr, wordcnt, rbuf))
			{
				eprintf("error reading target memory\n");
				exit(2);
			}
			gettimeofday(&tv2, &tz);

			diff = tv2.tv_sec - tv1.tv_sec;
			diff *= 1000000;
			diff += (int) tv2.tv_usec - (int) tv1.tv_usec;
			printf("\n\n\nread speed:\n");
			printf("%i bytes read in %i.%i seconds\n", wordcnt * 4, (diff + 5000) / 1000000, ((diff + 5000) % 1000000) / 10000);
			dx = ((double) (wordcnt * 4)) / (double) diff;
			dx *= 1000000.;
			printf("average speed: %i.%i bytes/second\n", (int)(dx), (int)((fmod(dx, 1.)) * 100.));
			printf("\n\n\n");

			if (memcmp(buf, rbuf, wordcnt * sizeof(uint32_t)))
			{
				eprintf("fatal error: data written and data read do not match!!!\n");
				exit(1);
			}
#endif

			free(buf);
			free(rbuf);

		}
		else if (!strcmp(argv[argnr], "-e"))
		{
			/* mass erase device */
			argnr ++;
			if (!pdev)
			{
				eprintf("device not specified, use the '-d' switch to specify a target device\n");
				exit(1);
			}
			connect_to_target();
			if (open_device(pdev, ctx))
				exit(1);
			/*! \todo	properly unlock flash here */

			if (pdev->flash_unlock_area && pdev->flash_unlock_area(pdev, ctx, 0))
			{
				eprintf("error unlocking target flash, target may need reset\n");
				return - 1;
			}
			/*

			if (pdev->flash_erase_area)
			{
				if (pdev->flash_erase_area(pdev, ctx, addr, wordcnt * sizeof(uint32_t)))
				{
					eprintf("error erasing flash\n");
					exit(1);
				}
			}
			else
			{
				printf("flash_erase_area() routine unavailable, invoking generic flash erase area routine\n");
				if (generic_flash_erase_area(pdev, ctx, addr, wordcnt * sizeof(uint32_t)))
				{
					eprintf("error erasing flash\n");
					exit(1);
				}
			}
			*/

			if (pdev->flash_mass_erase)
			{
				if (pdev->flash_mass_erase(pdev, ctx))
				{
					eprintf("error mass erasing target flash, target may need reset\n");
					return - 1;
				}
			}
			else if (pdev->flash_erase_sector)
			{
				printf("flash_mass_erase() routine unavailable, invoking generic flash erase area routine\n");
				for (i = 0; pdev->flash_areas[i].len; i ++)
				{
					if (generic_flash_erase_area(pdev, ctx, pdev->flash_areas[i].start, pdev->flash_areas[i].len))
					{
						eprintf("error erasing flash\n");
						exit(1);
					}
				}
			}
			else
			{
				eprintf("neither mass erase, nor erase sector routines specified\n");
				eprintf("aborting mass erase request\n");
				return -1;
			}
			printf("ok, chip successfully mass erased\n");
		}
		else if (!strcmp(argv[argnr], "--cont"))
		{
			argnr ++;
			connect_to_target();
			libgdb_sendpacket(ctx, "c");
			return 0;
		}
		else if (!strcmp(argv[argnr], "--stop") || !strcmp(argv[argnr], "--halt"))
		{

			argnr ++;
			is_target_connected = true;
			if (!(ctx = libgdb_init()))
			{
				eprintf("failed to initialize the libgdb library\n");
				exit(1);
			}
			if (libgdb_connect(ctx, "127.0.0.1", 1122))
			{
				eprintf("failed to connect to a gdb server\n");
				exit(2);
			}

			libgdb_sendbreak(ctx);
			libgdb_waithalted(ctx);
			return 0;
		}
		else if (!strcmp(argv[argnr], "--erase-area"))
		{
			/* write to flash */
			int len;
			char * s;

			argnr ++;
			if (!pdev)
			{
				eprintf("device not specified, use the '-d' switch to specify a target device\n");
				exit(1);
			}
			connect_to_target();
			if (open_device(pdev, ctx))
				exit(1);

			if (argnr == argc)
			{
				eprintf("missing destination address for erase command\n");
				exit(1);
			}
			addr = strtoul(argv[argnr], & s, 0);
			if (* s)
			{
				eprintf("bad destination address for erase command\n");
				exit(1);
			}

			argnr ++;
			if (argnr == argc)
			{
				eprintf("missing length argument for write command\n");
				exit(1);
			}
			len = strtoul(argv[argnr], & s, 0);
			if (* s)
			{
				eprintf("bad length argument for erase command\n");
				exit(1);
			}
			argnr ++;

			connect_to_target();
			gettimeofday(&tv1, &tz);
			if (pdev->flash_unlock_area && pdev->flash_unlock_area(pdev, ctx, 0))
			{
				eprintf("error unlocking target flash, target may need reset\n");
				return - 1;
			}

			if (pdev->flash_erase_area)
			{
				if (pdev->flash_erase_area(pdev, ctx, addr, len))
				{
					eprintf("error erasing flash\n");
					exit(1);
				}
			}
			else
			{
				printf("flash_erase_area() routine unavailable, invoking generic flash erase area routine\n");
				if (generic_flash_erase_area(pdev, ctx, addr, len))
				{
					eprintf("error erasing flash\n");
					exit(1);
				}
			}

			gettimeofday(&tv2, &tz);

			diff = tv2.tv_sec - tv1.tv_sec;
			diff *= 1000000;
			diff += (int) tv2.tv_usec - (int) tv1.tv_usec;
			printf("\n\n\nflash erase speed:\n");
			dx = ((double) len) / (double) diff;
			dx *= 1000000.;
			printf("average speed: %i.%i bytes/second\n", (int)(dx), (int)((fmod(dx, 1.)) * 100.));
			printf("\n\n\n");

		}
		else if (!strcmp(argv[argnr], "-w"))
		{
			/* write to flash */
			int fd, idx, len;
			char * s;
			struct stat stat;

			argnr ++;
			if (!pdev)
			{
				eprintf("device not specified, use the '-d' switch to specify a target device\n");
				exit(1);
			}

			if (argnr == argc)
			{
				eprintf("missing destination address for write command\n");
				exit(1);
			}
			addr = strtoul(argv[argnr], & s, 0);
			if (* s)
			{
				eprintf("bad destination address for write command\n");
				exit(1);
			}

			argnr ++;
			if (argnr == argc)
			{
				eprintf("missing filename for write command\n");
				exit(1);
			}
			if ((fd = open(argv[argnr], O_RDONLY | O_BINARY)) == -1)
			{
				eprintf("error opening file for reading\n");
				exit(1);
			}
			argnr ++;
			/* obtain file length and allocate buffer memory */

			if (fstat(fd, &stat))
			{
				eprintf("error fstat()-ing input file\n");
				exit(1);
			}

			wordcnt = stat.st_size >> 2;

			if (!(buf = malloc(wordcnt * sizeof(uint32_t))))
			{
				eprintf("cannot allocate buffer memory for reading input file\n");
				exit(1);
			}

			len = read(fd, buf, wordcnt * sizeof(uint32_t));
			close(fd);
			if (len == -1)
			{
				eprintf("error reading input file\n");
				exit(1);
			}

			connect_to_target();
			if (open_device(pdev, ctx))
				exit(1);
			if (pdev->flash_unlock_area && pdev->flash_unlock_area(pdev, ctx, 0))
			{
				eprintf("error unlocking target flash, target may need reset\n");
				return - 1;
			}

			if (pdev->flash_erase_area)
			{
				if (pdev->flash_erase_area(pdev, ctx, addr, wordcnt * sizeof(uint32_t)))
				{
					eprintf("error erasing flash\n");
					exit(1);
				}
			}
			else
			{
				printf("flash_erase_area() routine unavailable, invoking generic flash erase area routine\n");
				if (generic_flash_erase_area(pdev, ctx, addr, wordcnt * sizeof(uint32_t)))
				{
					eprintf("error erasing flash\n");
					exit(1);
				}
			}
			
			gettimeofday(&tv1, &tz);
			if (!pdev->flash_program_words)
			{
				eprintf("target flash write routine not specified, not performing flash write\n");
				exit(1);
			}
			else if (pdev->flash_program_words(pdev, ctx, addr, buf, wordcnt))
			{
				eprintf("error writing flash\n");
				exit(1);
			}
			else
				printf("flash successfully programmed\n");
			gettimeofday(&tv2, &tz);

			diff = tv2.tv_sec - tv1.tv_sec;
			diff *= 1000000;
			diff += (int) tv2.tv_usec - (int) tv1.tv_usec;
			printf("\n\n\nflash write speed:\n");
			printf("%i bytes written in %i.%i seconds\n", wordcnt * 4, (diff + 5000) / 1000000, ((diff + 5000) % 1000000) / 10000);
			dx = ((double) (wordcnt * 4)) / (double) diff;
			dx *= 1000000.;
			printf("average speed: %i.%i bytes/second\n", (int)(dx), (int)((fmod(dx, 1.)) * 100.));
			printf("\n\n\n");

			free(buf);

		}
		else if (!strcmp(argv[argnr], "-x"))
		{
			/* write file to flash - file can be in intel hex format,
			 * or an elf file - in this case it is first converted
			 * to intel hex format by running objcopy */
			struct data_mem_area * mem_areas, * s;
			bool must_unlink;
			char * hexfile_name;

			uint32_t * wbuf;
			argnr ++;
			if (!pdev)
			{
				eprintf("device not specified, use the '-d' switch to specify a target device\n");
				exit(1);
			}

			if (argnr == argc)
			{
				eprintf("missing filename for file write command\n");
				exit(1);
			}
			if (get_hex_fname(argv[argnr ++], &hexfile_name, & must_unlink) == -1)
			{
				eprintf("failed to obtain an ihex-formatted file to load into target\n");
				exit(1);
			}

			mem_areas = hexfile_read(hexfile_name);
			if (!mem_areas)
			{
				eprintf("error reading hex file\n");
				exit(1);
			}

			connect_to_target();
			if (open_device(pdev, ctx))
				exit(1);
			for (s = mem_areas; s; s = s->next)
			{
				int memtype;
				int wlen;
				printf("start: 0x%08x\tlen: 0x%08x\n", s->addr, s->len);
				memtype = get_mem_type(pdev, ctx, s->addr, s->len);

				wlen = s->len / sizeof(uint32_t);

				if (memtype == MEM_TYPE_INVALID)
				{
					eprintf("invalid memory area: start 0x%08x, size 0x%08x, aborting\n", s->addr, s->len);
					exit(1);
				}
				else if (memtype == MEM_TYPE_FLASH)
				{
					if (pdev->flash_unlock_area && pdev->flash_unlock_area(pdev, ctx, 0))
					{
						eprintf("error unlocking target flash, target may need reset\n");
						return - 1;
					}
					if (pdev->flash_erase_area)
					{
						if (pdev->flash_erase_area(pdev, ctx, s->addr, s->len))
						{
							eprintf("error erasing flash\n");
							exit(1);
						}
					}
					else
					{
						printf("flash_erase_area() routine unavailable, invoking generic flash erase area routine\n");
						if (generic_flash_erase_area(pdev, ctx, s->addr, s->len))
						{
							eprintf("error erasing flash\n");
							exit(1);
						}
					}
					if (!pdev->flash_program_words)
					{
						eprintf("target flash write routine not specified, not performing flash write\n");
						exit(1);
					}
					else if (pdev->flash_program_words(pdev, ctx, s->addr, (uint32_t *) s->data, wlen))
					{
						eprintf("error writing flash area: start 0x%08x, size 0x%08x, aborting\n", s->addr, s->len);
						exit(1);
					}
					else
						printf("flash area successfully programmed\n");
				}
				else if (memtype == MEM_TYPE_RAM)
				{
					if (libgdb_writewords(ctx, s->addr, wlen, s->data))
					{
						eprintf("error writing ram area: start 0x%08x, size 0x%08x, aborting\n", s->addr, s->len);
						exit(1);
					}
				}
				else /* should never happen */
				{
					eprintf("bad memory area type: start 0x%08x, size 0x%08x, aborting\n", s->addr, s->len);
					exit(1);
				}
				/* read back the memory area and verify it */
				wbuf = (uint32_t *) malloc(wlen * sizeof * wbuf);
				if (!wbuf)
				{
					eprintf("out of core\n");
					exit(1);
				}
				if (!memcmp(wbuf, s->data, wlen * sizeof * wbuf))
				{
					eprintf("verification failed, memory read and written mismatch\n");
					exit(1);
				}
				free(wbuf);
			}
			if (0) if (must_unlink)
				unlink(hexfile_name);
			free(hexfile_name);
			hexfile_dealloc(mem_areas);
		}
		else if (!strcmp(argv[argnr], "--erase-sector"))
		{
			/* erase sector */
			char * s;
			static volatile int xxx = 0;

			while (xxx == 1);

			argnr ++;
			if (!pdev)
			{
				eprintf("device not specified, use the '-d' switch to specify a target device\n");
				exit(1);
			}


			if (argnr == argc)
			{
				eprintf("missing sector number for erase command\n");
				exit(1);
			}
			addr = strtoul(argv[argnr], & s, 0);
			if (* s)
			{
				eprintf("bad source address for write command\n");
				exit(1);
			}

			argnr ++;

			connect_to_target();
			if (open_device(pdev, ctx))
				exit(1);

			gettimeofday(&tv1, &tz);
			/*! \todo	properly unlock flash here */
			if (pdev->flash_unlock_area && pdev->flash_unlock_area(pdev, ctx, 0))
			{
				eprintf("error unlocking target flash, target may need reset\n");
				return - 1;
			}
			if (!pdev->flash_erase_sector)
			{
				eprintf("flash sector erase routine unavailable, aborting\n");
				exit(1);
			}
			else if (pdev->flash_erase_sector(pdev, ctx, addr))
			{
				eprintf("error erasing flash sector %i\n", addr);
				exit(1);
			}

			gettimeofday(&tv2, &tz);

			diff = tv2.tv_sec - tv1.tv_sec;
			diff *= 1000000;
			diff += (int) tv2.tv_usec - (int) tv1.tv_usec;
			printf("sector erased in %i.%i seconds\n", (diff + 5000) / 1000000, ((diff + 5000) % 1000000) / 10000);
			printf("\n\n");

		}
		else if (!strcmp(argv[argnr], "-r"))
		{
			/* read memory */
			int fd, idx, len;
			char * s;
			struct stat stat;

			argnr ++;
			if (!pdev)
			{
				eprintf("device not specified, use the '-d' switch to specify a target device\n");
				exit(1);
			}

			if (argnr == argc)
			{
				eprintf("missing source address for read command\n");
				exit(1);
			}
			addr = strtoul(argv[argnr], & s, 0);
			if (* s)
			{
				eprintf("bad source address for read command (%s)\n", argv[argnr]);
				exit(1);
			}

			argnr ++;

			if (argnr == argc)
			{
				eprintf("missing word count for read command\n");
				exit(1);
			}
			wordcnt = strtoul(argv[argnr], & s, 0);
			if (* s)
			{
				eprintf("bad word count for read command\n");
				exit(1);
			}

			argnr ++;
			if (argnr == argc)
			{
				eprintf("missing filename for read command\n");
				exit(1);
			}
			if ((fd = open(argv[argnr], O_CREAT | O_BINARY | O_TRUNC | O_RDWR, 0666)) == -1)
			{
				eprintf("error opening file for writing\n");
				exit(1);
			}
			argnr ++;
			/* allocate buffer memory */
			if (!(buf = malloc(wordcnt * sizeof(uint32_t))))
			{
				eprintf("cannot allocate buffer memory for reading input file\n");
				exit(1);
			}

			connect_to_target();
			if (open_device(pdev, ctx))
				exit(1);
			gettimeofday(&tv1, &tz);
			if (libgdb_readwords(ctx, addr, wordcnt, buf))
			{
				eprintf("error reading target memory\n");
				exit(1);
			}
			gettimeofday(&tv2, &tz);

			diff = tv2.tv_sec - tv1.tv_sec;
			diff *= 1000000;
			diff += (int) tv2.tv_usec - (int) tv1.tv_usec;
			printf("\n\n\nmemory read speed:\n");
			printf("%i bytes read in %i.%i seconds\n", wordcnt * 4, (diff + 5000) / 1000000, ((diff + 5000) % 1000000) / 10000);
			dx = ((double) (wordcnt * 4)) / (double) diff;
			dx *= 1000000.;
			printf("average speed: %i.%i bytes/second\n", (int)(dx), (int)((fmod(dx, 1.)) * 100.));
			printf("\n\n\n");

			len = write(fd, buf, wordcnt * sizeof(uint32_t));
			if (len == -1)
			{
				eprintf("error writing output file, error %i (%s)\n", errno, strerror(errno));
				exit(1);
			}
			printf("ok\n");

			free(buf);
			close(fd);

		}
		else
		{
			/* attempt to parse a target specific command line option */
			if (!pdev)
			{
				eprintf("command line option %s not recognized, and no target specified\n", argv[argnr]);
				eprintf("if you intended to specify a target specific command line option, "
						"you must first specify the target with '-d device'\n");
				exit(1);
			}
			if (fill_in_cmdline_option(pdev, argv[argnr]))
				exit(1);
			argnr ++;
		}

	}

	return 0;
}

