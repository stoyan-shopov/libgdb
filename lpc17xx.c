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

//int flash_write(uint32_t * dest, uint32_t * src, uint32_t wordcnt) }

#else

#include <stdio.h>
#include <stdint.h>

#include "devices.h"
#include "devctl.h"


static int lpc17xx_dev_open(struct struct_devctl * dev, struct libgdb_ctx * ctx);
static int lpc17xx_flash_unlock_area(struct struct_devctl * dev, struct libgdb_ctx * ctx, const struct struct_memarea * area);
static int lpc17xx_flash_mass_erase(struct struct_devctl * dev, struct libgdb_ctx * ctx);
static int lpc17xx_flash_erase_sector(struct struct_devctl * dev, struct libgdb_ctx * ctx, uint32_t sector_nr);
static int lpc17xx_flash_program_words(struct struct_devctl * dev, struct libgdb_ctx * ctx, uint32_t dest, uint32_t * src, int wordcnt);

enum
{
	NR_SUPPORTED_DEVICES = 1,
	/* lpc17xx in-application-programming (iap) related constants */
	IAP_LOCATION			= 0x1fff1ff1,
	PREPARE_SECTORS_FOR_WRITING	= 50,
	COPY_RAM_TO_FLASH		= 51,
	ERASE_SECTORS			= 52,
	BLANK_CHECK_SECTORS		= 53,
	READ_PART_ID			= 54,
	READ_BOOT_CODE_VERSION		= 55,
	IAP_COMPARE			= 56,
	REINVOKE_ISP			= 57,
	READ_DEVICE_SERIAL_NUMBER	= 58,
	/* iap return codes */
	CMD_SUCCESS			= 0,
	/* pll0 register addresses, used for getting/setting target core clock settings */
	PLL0CON				= 0x400fc080,
	PLL0CFG				= 0x400fc084,
	PLL0STAT			= 0x400fc088,
	PLL0FEED			= 0x400fc08c,
	/* clock source selection register */
	CLKSRCSEL			= 0x400fc10c,
	CCLKCFG				= 0x400fc104,
	/* bit 0 of the MEMMAP register specifies what memory is mapped at
	 * address 0 - if this bit is 0 - a porion of the boot rom is mapped
	 * at address 0, if 1 - user memory is mapped at address 0 */
	MEMMAP				= 0x400fc040,

	/* parameters for running the target iap routines */
	LPC17XX_RAM_BASE		= 0x10000000,
	LPC17XX_CMD_ADDR		= LPC17XX_RAM_BASE,
	LPC17XX_RESULT_ADDR		= LPC17XX_RAM_BASE + 0x20,

	LPC17XX_BUF_ADDR_FOR_WRITE_OPERATION	= LPC17XX_RAM_BASE + 0x40,
	LPC17XX_BUF_SIZE		= 4 * 1024,

};

/* this data structure is used by the 'lpc17xx_run_iap_routine()' below -
 * it passes the 'cmd' array to the target iap routine, and retrieves
 * returned status data (if any) in the 'result' array; the error code returned
 * by an iap routine is stored it 'result[0]' */
struct lpc17xx_flash_data
{
	uint32_t	cmd[5];
	uint32_t	result[5];
	/* computed target core clock frequency, in hertz, needed for passing as a parameter to some iap routines */
	uint32_t	cclk;
};

enum
{
	/* index of the target xtal frequency in the 'cmdline_options' table below */
	XTAL_FREQ_PARAM_IDX		= 0,
};

static struct struct_devctl lpc17xx_devs[NR_SUPPORTED_DEVICES] =
{
        {
                .next = 0,
                .name = "lpc1754",
                .cmdline_options = (struct cmdline_option_info[])
                {
                        [0] = { .cmdstr = "xtal-freq-hz", .type = PARAM_TYPE_NUMERIC, .is_mandatory = true, },
                        [1] = { .cmdstr = 0, .type = PARAM_TYPE_INVALID, },
                },
                .ram_areas = (const struct struct_memarea[4])
                {
                        { .start = 0x10000000,	.len = 1024 * 16,	.sizes = 0,	},
                        { .start = 0,		.len = 0,		.sizes = 0,	},
                },
                .flash_areas = (const struct struct_memarea[2])
                {
                        {
                                .start = 0,	.len = 1024 * 128,
                                .sizes = (uint32_t[])
                                {
                                        4 * 1024, 4 * 1024, 4 * 1024, 4 * 1024,
                                        4 * 1024, 4 * 1024, 4 * 1024, 4 * 1024,
                                        4 * 1024, 4 * 1024, 4 * 1024, 4 * 1024,
                                        4 * 1024, 4 * 1024, 4 * 1024, 4 * 1024,

                                        32 * 1024, 32 * 1024,

                                        0,
                                },
                        },
                        {
                                .start = 0,		.len = 0,
                                .sizes = 0,
                        },
                },
                .dev_open = lpc17xx_dev_open,
                .dev_close = 0,
                .flash_unlock_area = 0/*lpc17xx_flash_unlock_area*/,
                .flash_erase_area = 0,
                .flash_erase_sector = lpc17xx_flash_erase_sector,
                .flash_mass_erase = 0,/* lpc17xx_flash_mass_erase */
                .flash_program_words = lpc17xx_flash_program_words,
		.validate_cmdline_options = 0,
                .pdev = & (struct lpc17xx_flash_data)
		{
		},
        },
};

static int lpc17xx_run_iap_routine(struct struct_devctl * dev, struct libgdb_ctx * ctx)
{
struct lpc17xx_flash_data * d;

	d = (struct lpc17xx_flash_data *) dev->pdev;
	if (libgdb_writewords(ctx, LPC17XX_CMD_ADDR, 5, d->cmd))
	{
		eprintf("libgdb_writewords()\n");
		return -1;
	}
	if (libgdb_armv7m_run_target_routine(ctx, IAP_LOCATION, dev->ram_areas[0].start + dev->ram_areas[0].len,
				0, 0, LPC17XX_CMD_ADDR, LPC17XX_RESULT_ADDR, 0, 0))
	{
		eprintf("libgdb_armv7m_run_target_routine()\n");
		return -1;
	}
	if (libgdb_readwords(ctx, LPC17XX_RESULT_ADDR, 5, d->result))
	{
		eprintf("libgdb_readwords()\n");
		return -1;
	}
	return 0;
}

static int lpc17xx_prepare_sectors_for_writing(struct struct_devctl * dev, struct libgdb_ctx * ctx, int first, int last)
{
struct lpc17xx_flash_data * d;

	d = (struct lpc17xx_flash_data *) dev->pdev;

	d->cmd[0] = PREPARE_SECTORS_FOR_WRITING;
	d->cmd[1] = first;
	d->cmd[2] = last;

	if (lpc17xx_run_iap_routine(dev, ctx))
		return -1;
	if (d->result[0] != CMD_SUCCESS)
		return -1;
	else
		return 0;

}

static int lpc17xx_dev_open(struct struct_devctl * dev, struct libgdb_ctx * ctx)
{
#if 0
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
#else
struct lpc17xx_flash_data * d;
uint32_t clksrcsel, cclkcfg, pll0con, pll0cfg, pll0stat;
uint32_t cclk, fin;
int res;
int i;

	res = libgdb_readwords(ctx, CLKSRCSEL, 1, & clksrcsel)
		+ libgdb_readwords(ctx, PLL0CON, 1, & pll0con)
		+ libgdb_readwords(ctx, PLL0CFG, 1, & pll0cfg)
		+ libgdb_readwords(ctx, CCLKCFG, 1, & cclkcfg)
		+ libgdb_readwords(ctx, PLL0STAT, 1, & pll0stat);
	if (res)
	{
		eprintf("error retrieving target clock configuration settings, aborting\n");
		return -1;
	}

	printf("retrieved target clock configuration settings: cclkcfg: 0x%08x, clksrcsel: 0x%08x, pll0con: 0x%08x, pll0cfg: 0x%08x, pll0stat: 0x%08x\n",
			cclkcfg, clksrcsel, pll0con, pll0cfg, pll0stat);


	d = (struct lpc17xx_flash_data *) dev->pdev;

	/* try to set the target cclk frequency to a known value */
	/* if the pll is already enabled and connected - first disconnect and disable it for a clean start */
	if (pll0stat & (3 << 24) == 3 << 24)
	{
		/* disconnect pll0 */
		res += libgdb_writewords(ctx, PLL0CON, 1, (uint32_t[1]) {1, });
		res += libgdb_writewords(ctx, PLL0FEED, 1, (uint32_t[1]) {0xaa, } );
		res += libgdb_writewords(ctx, PLL0FEED, 1, (uint32_t[1]) {0x55, } );
		/* wait for the pll to get disconnected */
		i = 0;
		do
			res += libgdb_readwords(ctx, PLL0STAT, 1, & pll0stat);
		while ((pll0stat & (1 << 25)) && ++ i != 10);
		if (res || i == 10)
		{
			eprintf("could not disconnect target pll\n");
			return -1;
		}

		/* disable pll0 */
		res += libgdb_writewords(ctx, PLL0CON, 1, (uint32_t[1]) {0, } );
		res += libgdb_writewords(ctx, PLL0FEED, 1, (uint32_t[1]) {0xaa, } );
		res += libgdb_writewords(ctx, PLL0FEED, 1, (uint32_t[1]) {0x55, } );
		/* wait for the pll to get disabled */
		i = 0;
		do
			res += libgdb_readwords(ctx, PLL0STAT, 1, & pll0stat);
		while ((pll0stat & (1 << 24)) && ++ i != 10);
		if (res || i == 10)
		{
			eprintf("could not disable target pll\n");
			return -1;
		}
	}
	printf("ok, pll disabled\n");

	/* use internal oscillator as clock source - its nominal value is 4 MHz */
	res += libgdb_writewords(ctx, CLKSRCSEL, 1, (uint32_t[1]) {0, } );

	/* set pll0 multiplier to 36, predivider both to 1 -
	 * this yields an output pll0 frequency (fcco) of 288 MHz */
	res += libgdb_writewords(ctx, PLL0CFG, 1, (uint32_t[1]) {0x23, } );
	res += libgdb_writewords(ctx, PLL0FEED, 1, (uint32_t[1]) {0xaa, } );
	res += libgdb_writewords(ctx, PLL0FEED, 1, (uint32_t[1]) {0x55, } );

	/* set cpu clock divider to 3 - so the cpu clock will be 96 MHz */
	res += libgdb_writewords(ctx, CCLKCFG, 1, (uint32_t[1]) {2, } );

	/* enable pll0 */
	res += libgdb_writewords(ctx, PLL0CON, 1, (uint32_t[1]) {1, } );
	res += libgdb_writewords(ctx, PLL0FEED, 1, (uint32_t[1]) {0xaa, } );
	res += libgdb_writewords(ctx, PLL0FEED, 1, (uint32_t[1]) {0x55, } );

	/* pll0 output clock (fcco) is 288 MHz here */
	/* wait for pll0 to get enabled */
	i = 0;
	do
		res += libgdb_readwords(ctx, PLL0STAT, 1, & pll0stat);
	while (!(pll0stat & (1 << 24)) && ++ i != 10);

	if (res || i == 10)
	{
		eprintf("error waiting for target pll0 to get enabled\n");
		return -1;
	}

	/* wait for pll0 to lock */
	i = 0;
	do
		res += libgdb_readwords(ctx, PLL0STAT, 1, & pll0stat);
	while (!(pll0stat & (1 << 26)) && ++ i != 10);

	if (res || i == 10)
	{
		eprintf("error waiting for target pll0 to lock\n");
		return -1;
	}

	printf("pll0 acquired lock, connecting pll0 now\n");

	/* connect pll0 */
	res += libgdb_writewords(ctx, PLL0CON, 1, (uint32_t[1]) {3, } );
	res += libgdb_writewords(ctx, PLL0FEED, 1, (uint32_t[1]) {0xaa, } );
	res += libgdb_writewords(ctx, PLL0FEED, 1, (uint32_t[1]) {0x55, } );
	/* wait for pll0 to connect */
	i = 0;
	do
		res += libgdb_readwords(ctx, PLL0STAT, 1, & pll0stat);
	while (!(pll0stat & (1 << 25)) && ++ i != 10);
	if (res || i == 10)
	{
		eprintf("error waiting for target pll0 to connect\n");
		return -1;
	}
	printf("ok, pll0 connected, target core clock is 96 MHz\n");

	d->cclk = 96 * 1000000;

	printf("forcing user memory mapping at address 0...\n");
	if (libgdb_writewords(ctx, MEMMAP, 1, (uint32_t[1]) {1, } ))
	{
		eprintf("error remapping memory at address 0\n");
		return 0;
	}


	d->cmd[0] = READ_PART_ID;
	printf("attempting to retrieve lpc17xx part id number...\n");
	if (lpc17xx_run_iap_routine(dev, ctx) || d->result[0] != CMD_SUCCESS)
	{
		eprintf("error retrieving target part number\n");
		return -1;
	}
	printf("retrieved target part number: 0x%08x\n", d->result[1]);

	return 0;
#endif
}

static bool is_target_flash_locked(struct libgdb_ctx * ctx)
{
#if 0
uint32_t x;

	if (libgdb_readwords(ctx, FCTRL, 1, &x) || x & 0x80000000)
		return true;
	return false;
#else
	return -1;
#endif
}

static int check_error_flags(uint32_t flags)
{
#if 0
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
#else
	return -1;
#endif
}

static int clear_flash_errors(struct libgdb_ctx * ctx)
{
#if 0
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
#else
	return -1;
#endif
}

static int lpc17xx_flash_unlock_area(struct struct_devctl * dev, struct libgdb_ctx * ctx, const struct struct_memarea * area)
{
#if 0
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
#else
	return -1;
#endif
}

static int lpc17xx_flash_mass_erase(struct struct_devctl * dev, struct libgdb_ctx * ctx)
{
#if 0
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
#else
	/*
int i;
	for (i = 0; dev->flash_areas[0].sizes[i]; i ++)
		;
	lpc17xx_prepare_sectors_for_writing();
	*/

	return -1;
#endif
}

static int lpc17xx_flash_erase_sector(struct struct_devctl * dev, struct libgdb_ctx * ctx, uint32_t sector_nr)
{
#if 0

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

#else
struct lpc17xx_flash_data * d;

	if (lpc17xx_prepare_sectors_for_writing(dev, ctx, sector_nr, sector_nr))
		return -1;

	d = (struct lpc17xx_flash_data *) dev->pdev;

	d->cmd[0] = ERASE_SECTORS;
	d->cmd[1] = sector_nr;
	d->cmd[2] = sector_nr;
	d->cmd[3] = d->cclk / 1000;

	if (lpc17xx_run_iap_routine(dev, ctx))
		return -1;
	if (d->result[0] != CMD_SUCCESS)
		return -1;
	else
		return 0;
#endif
}

static int lpc17xx_flash_program_words(struct struct_devctl * dev, struct libgdb_ctx * ctx, uint32_t dest, uint32_t * src, int wordcnt)
{
bool is_annotation_enabled;
struct lpc17xx_flash_data * pdev;
int i, sector_nr, sector_idx;


	pdev = (struct pdev *) dev->pdev;

	/* make sure the destination address falls on a 256 byte boundary */
	if (dest & (0xff))
	{
		eprintf("destination address does not fall on a 256-byte boundary, aborting\n");
		return -1;
	}

	sector_nr = sector_idx = 0;
	while (wordcnt)
	{
		/*! \todo	special case - checksum - do this properly */
		if (dest == 0)
		{
			eprintf("warning: performing hacked vector checksum calculation\n");
			uint32_t cksum;
			for (cksum = i = 0; i < 7; i ++)
				cksum += src[i];
			eprintf("checksum is 0x%08x\n", cksum);
			src[i] = -cksum;
		}
		i = (wordcnt > LPC17XX_BUF_SIZE / sizeof(uint32_t)) ? LPC17XX_BUF_SIZE : wordcnt * sizeof(uint32_t);
		if (libgdb_writewords(ctx, LPC17XX_BUF_ADDR_FOR_WRITE_OPERATION, LPC17XX_BUF_SIZE / sizeof(uint32_t), src))
			return -1;
		if (lpc17xx_prepare_sectors_for_writing(dev, ctx, sector_nr, sector_nr))
			return -1;
		pdev->cmd[0] = COPY_RAM_TO_FLASH;
		pdev->cmd[1] = dest;
		pdev->cmd[2] = LPC17XX_BUF_ADDR_FOR_WRITE_OPERATION;
		pdev->cmd[3] = LPC17XX_BUF_SIZE;
		pdev->cmd[4] = pdev->cclk / 1000;
		if (lpc17xx_run_iap_routine(dev, ctx) || pdev->result[0] != CMD_SUCCESS)
			return -1;
		sector_idx += i;
		if (sector_idx == dev->flash_areas[0].sizes[sector_nr])
		{
			/* skip to next sector */
			sector_idx = 0;
			sector_nr ++;
		}
		else if (sector_idx > dev->flash_areas[0].sizes[sector_nr])
		{
			eprintf("flash sector boundary surpassed, don't know what to do\n");
			return -1;
		}
		dest += i;
		src += i / sizeof(uint32_t);
		wordcnt -= i / sizeof(uint32_t);
	}

	is_annotation_enabled = libgdb_set_annotation(ctx, false);

	libgdb_set_annotation(ctx, is_annotation_enabled);
	return 0;
}

struct struct_devctl * lpc17xx_get_devs(void)
{
    return lpc17xx_devs;
}

#endif /* COMPILING_TARGET_RESIDENT_CODE */

