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

#define FBASE	(0x40000000 + 0x20000 + 0x3c00)
#define FACR	(*(volatile uint32_t *)(FBASE + 0x0))
#define FKEYR	(*(volatile uint32_t *)(FBASE + 0x4))
#define FSR	(*(volatile uint32_t *)(FBASE + 0xc))
#define FCTRL	(*(volatile uint32_t *)(FBASE + 0x10))

int flash_write(volatile uint32_t * dest, uint32_t * src, uint32_t wordcnt)
{
	while (FSR & (1 << 16))
		;
	while (wordcnt --)
	{
		FCTRL = (2 << 8) | 1;
		* dest = * src;
		while (FSR & (1 << 16))
			;
		if (* dest != * src)
			return -2;
		dest ++;
		src ++;
		if (FSR)
			return -1;
	}
	return 0;
}

#else

#include <stdio.h>
#include <stdint.h>

#include "devices.h"
#include "devctl.h"


static int stm32f4x_dev_open(struct struct_devctl * dev, struct libgdb_ctx * ctx);
static int stm32f4x_flash_unlock_area(struct struct_devctl * dev, struct libgdb_ctx * ctx, const struct struct_memarea * area);
static int stm32f4x_flash_mass_erase(struct struct_devctl * dev, struct libgdb_ctx * ctx);
static int stm32f4x_flash_erase_sector(struct struct_devctl * dev, struct libgdb_ctx * ctx, uint32_t sector_nr);
static int stm32f4x_flash_program_words(struct struct_devctl * dev, struct libgdb_ctx * ctx, uint32_t dest, uint32_t * src, int wordcnt);

enum
{

	NR_SUPPORTED_DEVICES = 1,

	FBASE	= 0x40000000 + 0x20000 + 0x3c00,
	FACR	= FBASE + 0x0,
	FKEYR	= FBASE + 0x4,
	FSR	= FBASE + 0xc,
	/* bits in the flash status register */
	BSY	= 1 << 16,
	PGSERR	= 1 << 7,
	PGPERR	= 1 << 6,
	PGAERR	= 1 << 5,
	WRPERR	= 1 << 4,

	FCTRL = FBASE + 0x10,
	/* bits in the flash control register */
	STRT	= 1 << 16,
	MER	= 1 << 2,
	SER	= 1 << 1,

	STM32F4_FLASH_BASE_ADDR	=	0x08000000,
	STM32F4_FLASH_SIZE	=	0x100000,
	STM32F4_RAM_BASE_ADDR	=	0x20000000,
	STM32F4_RAM_SIZE	=	0x1c000,

	STM32F4_EXEC_RETURN_ADDR	=	STM32F4_RAM_BASE_ADDR,
	STM32F4_FLASH_WRITE_ROUTINE_ADDR	=	STM32F4_RAM_BASE_ADDR + 0x10,
	STM32F4_FLASH_BUF	=	STM32F4_RAM_BASE_ADDR + 0x100,
	STM32F4_FLASH_BUF_SIZE	=	0x400,
	STM32F4_FLASH_WRITE_ROUTINE_STACK_SIZE	=	0x200,
	STM32F4_FLASH_WRITE_ROUTINE_STACK_PTR	=	STM32F4_FLASH_BUF + STM32F4_FLASH_BUF_SIZE
								+ STM32F4_FLASH_WRITE_ROUTINE_STACK_SIZE,



};


static struct struct_devctl stm32f4x_devs[NR_SUPPORTED_DEVICES] =
{
	{
		.next = 0,
		.name = "stm32f407g",
                .cmdline_options = 0,
		.ram_areas = (const struct struct_memarea[4])
			{
				{ .start = 0x10000000,	.len = 1024 * 64, 	.sizes = 0,	},
				{ .start = 0x20000000,	.len = 1024 * 112,	.sizes = 0,	},
				{ .start = 0x2001c000,	.len = 1024 * 16,	.sizes = 0,	},
				{ .start = 0,		.len = 0,		.sizes = 0,	},
			},
		.flash_areas = (const struct struct_memarea[2])
			{
				{
					.start = 0x08000000,	.len = 1024 * 1024,
					.sizes = (uint32_t[])
					{
						16 * 1024, 16 * 1024, 16 * 1024, 16 * 1024,
						64 * 1024,
						128 * 1024, 128 * 1024, 128 * 1024, 128 * 1024,
						128 * 1024, 128 * 1024,
						0,
					},
				},
				{
					.start = 0,		.len = 0,
					.sizes = 0,
				},
			},
		.dev_open = stm32f4x_dev_open,
		.dev_close = 0,
		.flash_unlock_area = stm32f4x_flash_unlock_area,
		.flash_erase_area = 0,
		.flash_erase_sector = stm32f4x_flash_erase_sector,
		.flash_mass_erase = stm32f4x_flash_mass_erase,
		.flash_program_words = stm32f4x_flash_program_words,
		.validate_cmdline_options = 0,
		.pdev = &(struct pdev)
			{
				.code_load_addr = 0x20000000,
				.write_buf_addr = 0x20000000 + 0x100,
				.write_buf_size = 4000,
				.stack_size = 0x200,
		       	},
	},
};

/* int flash_write(uint32_t * src, uint32_t * dest, uint32_t wordcnt) */
static uint32_t stm32f4x_flash_write_routine[] =
{
/* directly include the machine code of the target 'flash_write()' routine */
#include "stm32f4x-flash-write-mcode.h"
};


static int stm32f4x_dev_open(struct struct_devctl * dev, struct libgdb_ctx * ctx)
{
uint32_t x;

	/* clear any active flash memory controller errors */
	if (libgdb_readwords(ctx, FSR, 1, &x))
		return -1;
	if (x & 0xf0)
	{
		if (libgdb_writewords(ctx, FSR, 1, (uint32_t[1]) { [0] = x & 0xf0, }))
			return -1;
		if (libgdb_readwords(ctx, FSR, 1, &x))
			return -1;
		if (x & 0xf0)
			return -1;
	}

	return 0;
}

static bool is_target_flash_locked(struct libgdb_ctx * ctx)
{
uint32_t x;

	if (libgdb_readwords(ctx, FCTRL, 1, &x) || x & 0x80000000)
		return true;
	return false;
}

static int check_error_flags(uint32_t flags)
{
int res;
	res = 0;

	if (flags & PGSERR)
		eprintf("%s(): programming sequence error\n", __func__), res = -1;
	if (flags & PGPERR)
		eprintf("%s(): programming parallelism error\n", __func__), res = -1;
	if (flags & PGAERR)
		eprintf("%s(): programming alignment error\n", __func__), res = -1;
	if (flags & WRPERR)
		eprintf("%s(): write protection error\n", __func__), res = -1;

	return res;
}

static int clear_flash_errors(struct libgdb_ctx * ctx)
{
uint32_t x;

	if (libgdb_readwords(ctx, FSR, 1, &x))
		return -1;
	if (x & BSY)
		return -1;
	x &= PGSERR | PGPERR | PGAERR | WRPERR;
	if (!x)
		/* no errors currently active */
		return 0;
	if (libgdb_writewords(ctx, FSR, 1, &x))
		return -1;
	if (libgdb_readwords(ctx, FSR, 1, &x))
		return -1;
	x &= PGSERR | PGPERR | PGAERR | WRPERR;
	if (!x)
		/* no errors currently active */
		return 0;
	return -1;
}

static int stm32f4x_flash_unlock_area(struct struct_devctl * dev, struct libgdb_ctx * ctx, const struct struct_memarea * area)
{
	if (!is_target_flash_locked(ctx))
		/* flash already unlocked - nothing to do */
		return 0;
	if (libgdb_writewords(ctx, FKEYR, 1, (uint32_t[1]) { [0] = 0x45670123, }))
		return -1;
	if (libgdb_writewords(ctx, FKEYR, 1, (uint32_t[1]) { [0] = 0xcdef89ab, }))
		return -1;
	/*! \todo	this sets flash access speed to the lowest value possible
	 *		(7 wait states) - set this properly based on current target
	 *		clock settings */
	if (libgdb_writewords(ctx, FACR, 1, (uint32_t[1]) { [0] = 0x7, }))
	{
		eprintf("could not set flash speed (wait states)\n");
		return -1;
	}
	if (is_target_flash_locked(ctx))
		return -1;
	return 0;
}

static int stm32f4x_flash_mass_erase(struct struct_devctl * dev, struct libgdb_ctx * ctx)
{
uint32_t x;

	if (is_target_flash_locked(ctx))
	{
		eprintf("%s(): target flash is locked, aborting mass erase operation\n", __func__);
		return -1;
	}
	while (1)
	{
		if (libgdb_readwords(ctx, FSR, 1, &x))
			return -1;
		if (!(x & BSY))
			break;
	}
	if (check_error_flags(x))
	{
		printf("target flash errors detected, attempting flash error recovery\n");
		if (clear_flash_errors(ctx) == -1)
		{
			printf("target flash error recovery failed, aborting\n");
			return -1;
		}
		else
			printf("target flash error recovery successful\n");
	}

	if (libgdb_writewords(ctx, FCTRL, 1, (uint32_t[1]) { [0] = MER, }))
		return -1;
	if (libgdb_writewords(ctx, FCTRL, 1, (uint32_t[1]) { [0] = MER | STRT, }))
		return -1;
	while (1)
	{
		if (libgdb_readwords(ctx, FSR, 1, &x))
			return -1;
		if (check_error_flags(x))
			return -1;
		if (!(x & BSY))
			break;
	}
	return 0;
}

static int stm32f4x_flash_erase_sector(struct struct_devctl * dev, struct libgdb_ctx * ctx, uint32_t sector_nr)
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
	if (libgdb_writewords(ctx, FCTRL, 1, (uint32_t[1]) { [0] = SER | (sector_nr << 3), }))
		return -1;
	if (libgdb_writewords(ctx, FCTRL, 1, (uint32_t[1]) { [0] = SER | STRT | (sector_nr << 3), }))
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

static int stm32f4x_flash_program_words(struct struct_devctl * dev, struct libgdb_ctx * ctx, uint32_t dest, uint32_t * src, int wordcnt)
{
int idx, wcnt, i;
uint32_t res;
uint32_t stackbase;
uint32_t total, cur;
bool is_annotation_enabled;
struct pdev * pdev;

	if (is_target_flash_locked(ctx))
	{
		eprintf("%s(): target flash is locked, aborting write operation\n", __func__);
		return -1;
	}

	pdev = (struct pdev *) dev->pdev;

	is_annotation_enabled = libgdb_set_annotation(ctx, false);
	total = wordcnt * sizeof(uint32_t);
	cur = 0;
	/* load the flash write routine */
	if (libgdb_writewords(ctx,
				pdev->code_load_addr,
				sizeof stm32f4x_flash_write_routine >> 2,
				(uint32_t *) stm32f4x_flash_write_routine))
	{
		eprintf("error loading flash writing routine into target\n");
		libgdb_set_annotation(ctx, is_annotation_enabled);
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
			libgdb_set_annotation(ctx, is_annotation_enabled);
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
			libgdb_set_annotation(ctx, is_annotation_enabled);
			return -1;
		}
		if (res)
		{
			eprintf("error writing target flash, target returned error code: %i\n", (int) res);
			libgdb_set_annotation(ctx, is_annotation_enabled);
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

struct struct_devctl * stm32f4x_get_devs(void)
{
	return stm32f4x_devs;
}

#endif /* COMPILING_TARGET_RESIDENT_CODE */

