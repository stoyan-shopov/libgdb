/*

Copyright (C) 2012 stoyan shopov

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


#ifdef COMPILING_TARGET_RESIDENT_CODE

#include <stdint.h>

/* stm32f10x flash access */
#define FBASE	(0x40000000 + 0x20000 + 0x2000)
#define FACR	(*(volatile uint32_t *)(FBASE + 0x0))
#define FKEYR	(*(volatile uint32_t *)(FBASE + 0x4))
#define FSR	(*(volatile uint32_t *)(FBASE + 0xc))
#define FCTRL	(*(volatile uint32_t *)(FBASE + 0x10))
#define FAR	(*(volatile uint32_t *)(FBASE + 0x14))

void flash_mass_erase(uint32_t * dest, uint32_t * src, uint32_t wordcnt)
{
	while (FSR & (1 << 0))
		;
	FCTRL |= 4;
	FCTRL |= 0x40;
	while (FSR & (1 << 0))
		;
}

int flash_write(uint32_t * dest, uint32_t * src, uint32_t wordcnt)
{
uint16_t * hws, * hwd;

	hws = (uint16_t *) src;
	hwd = (uint16_t *) dest;
	while (FSR & (1 << 0))
		;

	wordcnt <<= 1;
	while (wordcnt --)
	{
		FCTRL = 1;
		* hwd = * hws;
		while (FSR & (1 << 0))
			;
		if (FSR & (0x14))
			return -1;
		if (* hws != * hwd)
			return -2;
		hws ++;
		hwd ++;
	}
	return 0;
}


#else

#include <stdio.h>
#include <stdint.h>

#include "devices.h"
#include "devctl.h"


static int stm32f10x_flash_unlock_area(struct struct_devctl * dev, struct libgdb_ctx * ctx, const struct struct_memarea * area);
static int stm32f10x_flash_mass_erase(struct struct_devctl * dev, struct libgdb_ctx * ctx);
static int stm32f10x_flash_program_words(struct struct_devctl * dev, struct libgdb_ctx * ctx, uint32_t dest, uint32_t * src, int wordcnt);
static int stm32f10x_flash_erase_sector(struct struct_devctl * dev, struct libgdb_ctx * ctx, uint32_t sector_nr);

enum
{

	NR_SUPPORTED_DEVICES = 1,

	FBASE	= 0x40000000 + 0x20000 + 0x2000,
	FACR	= FBASE + 0x0,
	FKEYR	= FBASE + 0x4,
	FSR	= FBASE + 0xc,
	/* bits in the flash status register */
	BSY	= 1 << 0,
	PGERR	= 1 << 2,
	WRPERR	= 1 << 4,

	/* flash address register */
	FAR	= FBASE + 0x14,

	FCTRL = FBASE + 0x10,
	/* bits in the flash control register */
	STRT	= 1 << 6,
	MER	= 1 << 2,
	PER	= 1 << 1,
	LOCK	= 1 << 7,
};


static struct struct_devctl stm32f10x_devs[NR_SUPPORTED_DEVICES] =
{
	{
		.next = 0,
		.name = "stm32f100xb",
                .cmdline_options = 0,
		.ram_areas = (const struct struct_memarea[2])
			{
				{ .start = 0x20000000,	.len = 1024 * 8,	.sizes = 0,	},
				{ .start = 0,		.len = 0,		.sizes = 0,	},
			},
		.flash_areas = (const struct struct_memarea[2])
			{
				{ .start = 0x08000000,	.len = 1024 * 128,
					.sizes = (uint32_t[64 + 1])
					{
						1 * 1024, 1 * 1024, 1 * 1024, 1 * 1024, 
						1 * 1024, 1 * 1024, 1 * 1024, 1 * 1024, 
						1 * 1024, 1 * 1024, 1 * 1024, 1 * 1024, 
						1 * 1024, 1 * 1024, 1 * 1024, 1 * 1024, 
						1 * 1024, 1 * 1024, 1 * 1024, 1 * 1024, 
						1 * 1024, 1 * 1024, 1 * 1024, 1 * 1024, 
						1 * 1024, 1 * 1024, 1 * 1024, 1 * 1024, 
						1 * 1024, 1 * 1024, 1 * 1024, 1 * 1024, 
						1 * 1024, 1 * 1024, 1 * 1024, 1 * 1024, 
						1 * 1024, 1 * 1024, 1 * 1024, 1 * 1024, 
						1 * 1024, 1 * 1024, 1 * 1024, 1 * 1024, 
						1 * 1024, 1 * 1024, 1 * 1024, 1 * 1024, 
						1 * 1024, 1 * 1024, 1 * 1024, 1 * 1024, 
						1 * 1024, 1 * 1024, 1 * 1024, 1 * 1024, 
						1 * 1024, 1 * 1024, 1 * 1024, 1 * 1024, 
						1 * 1024, 1 * 1024, 1 * 1024, 1 * 1024, 
						0,
					},
				},
				{ .start = 0,		.len = 0,		.sizes = 0,	},
			},
		.dev_open = 0,
		.dev_close = 0,
		.flash_unlock_area = stm32f10x_flash_unlock_area,
		.flash_erase_area = 0,
                /*! \todo	code the erase_area() function */
		.flash_erase_sector = stm32f10x_flash_erase_sector,
		.flash_mass_erase = stm32f10x_flash_mass_erase,
		.flash_program_words = stm32f10x_flash_program_words,
		.validate_cmdline_options = 0,
		.pdev = &(struct pdev)
			{
				.code_load_addr = 0x20000000,
				.write_buf_addr = 0x20000000 + 0x100,
				.write_buf_size = 0x1800,
				.stack_size = 0x200,
		       	},
	},
};

/* int flash_write(uint32_t * src, uint32_t * dest, uint32_t wordcnt) */
static uint32_t stm32f10x_flash_write_routine[] =
{
/* directly include the machine code of the target 'flash_write()' routine */
#include "stm32f10x-flash-write-mcode.h"
};


static int check_error_flags(uint32_t flags)
{
int res;
	res = 0;

	if (flags & WRPERR)
		eprintf("%s(): write protection error\n", __func__), res = -1;
	if (flags & PGERR)
		eprintf("%s(): programming error\n", __func__), res = -1;

	return res;
}

static bool is_target_flash_locked(struct libgdb_ctx * ctx)
{
uint32_t x;

	if (libgdb_readwords(ctx, FCTRL, 1, &x) || (x & LOCK))
		return true;
	return false;
}

static int stm32f10x_flash_unlock_area(struct struct_devctl * dev, struct libgdb_ctx * ctx, const struct struct_memarea * area)
{
uint32_t x;
int res;
	if (!is_target_flash_locked(ctx))
		/* flash already unlocked - nothing to do */
		return 0;
	res = 0;
	res += libgdb_writewords(ctx, FKEYR, 1, (uint32_t[1]) { [0] = 0x45670123, });
	res += libgdb_writewords(ctx, FKEYR, 1, (uint32_t[1]) { [0] = 0xcdef89ab, });
	res += libgdb_writewords(ctx, FACR, 1, (uint32_t[1]) { [0] = 0x32, });
	if (res)
		return -1;
	return 0;
}

static int stm32f10x_flash_mass_erase(struct struct_devctl * dev, struct libgdb_ctx * ctx)
{
uint32_t x;
	while (1)
	{
		if (libgdb_readwords(ctx, FSR, 1, &x))
			return -1;
		if (!(x & BSY))
			break;
	}
	if (0)
	{
		printf("mass erase status (prior to mass erase): 0x%08x\n", x);
		if (libgdb_readwords(ctx, FCTRL, 1, &x))
			return -1;
		printf("FCR before mass erase: 0x%08x\n", x);
	}
	if (libgdb_writewords(ctx, FCTRL, 1, (uint32_t[1]) { [0] = MER, }))
		return -1;
	if (libgdb_writewords(ctx, FCTRL, 1, (uint32_t[1]) { [0] = MER | STRT, }))
		return -1;
	while (1)
	{
		if (libgdb_readwords(ctx, FSR, 1, &x))
			return -1;
		if (!(x & BSY))
			break;
	}
	if (0)
	{
		printf("mass erase status (after mass erase): 0x%08x\n", x);
		if (libgdb_readwords(ctx, FCTRL, 1, &x))
			return -1;
		printf("FCR after mass erase: 0x%08x\n", x);
	}
	return 0;
}


static int stm32f10x_flash_erase_sector(struct struct_devctl * dev, struct libgdb_ctx * ctx, uint32_t sector_nr)
{

/* locate sector number */
uint32_t x;

	if (is_target_flash_locked(ctx))
	{
		eprintf("%s(): target flash is locked, aborting erase operation\n", __func__);
		return -1;
	}

	while (1)
	{
		if (libgdb_readwords(ctx, FSR, 1, &x))
			return -1;
		/* check error flags */
		if (check_error_flags(x))
			return -1;
		if (!(x & BSY))
			break;
	}
	printf("erasing flash sector %i...\n", sector_nr);
	if (libgdb_writewords(ctx, FCTRL, 1, (uint32_t[1]) { [0] = PER, }))
		return -1;
	if (libgdb_writewords(ctx, FAR, 1, (uint32_t[1]) { [0] = sector_nr * 1024, }))
		return -1;
	if (libgdb_writewords(ctx, FCTRL, 1, (uint32_t[1]) { [0] = PER | STRT, }))
		return -1;
	while (1)
	{
		if (libgdb_readwords(ctx, FSR, 1, &x))
			return -1;
		/* check error flags */
		if (check_error_flags(x))
			return -1;
		if (!(x & BSY))
			break;
	}

	return 0;

}

static int stm32f10x_flash_program_words(struct struct_devctl * dev, struct libgdb_ctx * ctx, uint32_t dest, uint32_t * src, int wordcnt)
{
int idx, wcnt, i;
uint32_t res;
uint32_t stackbase;
struct pdev * pdev;
uint32_t total, cur;
bool is_annotation_enabled;

	pdev = (struct pdev *) dev->pdev;

	is_annotation_enabled = libgdb_set_annotation(ctx, false);
	total = wordcnt * sizeof(uint32_t);
	cur = 0;
	/* load the flash write routine */
	if (libgdb_writewords(ctx,
				pdev->code_load_addr,
				sizeof stm32f10x_flash_write_routine >> 2,
				(uint32_t *) stm32f10x_flash_write_routine))
	{
		eprintf("error loading flash writing routine into target\n");
		return - 1;
	}

	idx = 0;
	wcnt = pdev->write_buf_size / sizeof(uint32_t);
	stackbase = pdev->write_buf_addr + pdev->write_buf_size + pdev->stack_size;
	while (wordcnt)
	{
		i = (wcnt < wordcnt) ? wcnt : wordcnt;
		if (libgdb_writewords(ctx, pdev->write_buf_addr, i, src + idx))
		{
			eprintf("error writing target memory\n");
			return -1;
		}

		if (libgdb_armv7m_run_target_routine(ctx,
					pdev->code_load_addr,
					stackbase,
					0,
					& res,
					dest + idx * sizeof(uint32_t),
					pdev->write_buf_addr,
					i,
					0))
		{
			eprintf("error executing flash writing routine\n");
			return -1;
		}
		if (res)
		{
			eprintf("error writing target flash, target returned error code: %i\n", (int) res);
			return -1;
		}
		idx += i;
		wordcnt -= i;
		printf("%i bytes written\n", idx * sizeof(uint32_t));
		cur += i * sizeof(uint32_t);
		printf("[VX-FLASH-WRITE-PROGRESS]\t%i\t%i\n", cur, total);
	}

	libgdb_set_annotation(ctx, is_annotation_enabled);
	return 0;
}

struct struct_devctl * stm32f10x_get_devs(void)
{
	return stm32f10x_devs;
}

#endif /* COMPILING_TARGET_RESIDENT_CODE */

