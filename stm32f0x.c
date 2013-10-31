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

#define FBASE	0x40022000
#define FACR	(*(volatile uint32_t *)(FBASE + 0x0))
#define FKEYR	(*(volatile uint32_t *)(FBASE + 0x4))
#define FSR	(*(volatile uint32_t *)(FBASE + 0xc))
#define FCTRL	(*(volatile uint32_t *)(FBASE + 0x10))

enum
{

	/* bits in the flash status register */
	BSY	= 1 << 0,
	PGERR	= 1 << 2,
	WRPERR	= 1 << 4,
	EOP	= 1 << 5,

	/* bits in the flash control register */
	LOCK	= 1 << 7,
	STRT	= 1 << 6,
	MER	= 1 << 2,
	PER	= 1 << 1,
	PG	= 1 << 0,
};

int flash_write(volatile uint32_t * dest, uint32_t * src, uint32_t wordcnt)
{
volatile uint16_t * hwd, * hws;

	while (FSR & BSY)
		;
	hwd = (uint16_t *) dest;
	hws = (uint16_t *) src;
	wordcnt <<= 1;
	while (wordcnt --)
	{
		FCTRL = PG;
		* hwd = * hws;
		while (FSR & BSY)
			;
		if (* hwd != * hws)
			return -2;
		hwd ++;
		hws ++;
		if (FSR & (PGERR | WRPERR))
			return -1;
	}
	return 0;
}

#else

#include <stdio.h>
#include <stdint.h>

#include "devices.h"
#include "devctl.h"


static int stm32f0x_dev_open(struct struct_devctl * dev, struct libgdb_ctx * ctx);
static int stm32f0x_flash_unlock_area(struct struct_devctl * dev, struct libgdb_ctx * ctx, const struct struct_memarea * area);
static int stm32f0x_flash_mass_erase(struct struct_devctl * dev, struct libgdb_ctx * ctx);
static int stm32f0x_flash_erase_sector(struct struct_devctl * dev, struct libgdb_ctx * ctx, uint32_t sector_nr);
static int stm32f0x_flash_program_words(struct struct_devctl * dev, struct libgdb_ctx * ctx, uint32_t dest, uint32_t * src, int wordcnt);

enum
{

	NR_SUPPORTED_DEVICES = 1,

	FBASE	= 0x40022000,
	FACR	= FBASE + 0x0,
	FKEYR	= FBASE + 0x4,
	FSR	= FBASE + 0xc,
	/* bits in the flash status register */
	BSY	= 1 << 0,
	PGERR	= 1 << 2,
	WRPERR	= 1 << 4,
	EOP	= 1 << 5,

	FCTRL = FBASE + 0x10,
	/* bits in the flash control register */
	LOCK	= 1 << 7,
	STRT	= 1 << 6,
	MER	= 1 << 2,
	PER	= 1 << 1,
	PG	= 1 << 0,
	/* flash address register */
	FAR	= FBASE + 0x14,

	STM32F4_FLASH_BASE_ADDR	=	0x08000000,
	STM32F4_FLASH_SIZE	=	0x10000,
	STM32F4_RAM_BASE_ADDR	=	0x20000000,
	STM32F4_RAM_SIZE	=	0x2000,

	STM32F4_EXEC_RETURN_ADDR	=	STM32F4_RAM_BASE_ADDR,
	STM32F4_FLASH_WRITE_ROUTINE_ADDR	=	STM32F4_RAM_BASE_ADDR + 0x10,
	STM32F4_FLASH_BUF	=	STM32F4_RAM_BASE_ADDR + 0x100,
	STM32F4_FLASH_BUF_SIZE	=	0x400,
	STM32F4_FLASH_WRITE_ROUTINE_STACK_SIZE	=	0x200,
	STM32F4_FLASH_WRITE_ROUTINE_STACK_PTR	=	STM32F4_FLASH_BUF + STM32F4_FLASH_BUF_SIZE
								+ STM32F4_FLASH_WRITE_ROUTINE_STACK_SIZE,

	/* reset and clock control (rcc) registers */
	RCC_BASE	= 0x40021000,
	/* rcc control register */
	RCC_CR		= RCC_BASE + 0,
	/* rcc control register 2 */
	RCC_CR2		= RCC_BASE + 0x34,
	/* rcc configuration register */
	RCC_CFGR	= RCC_BASE + 4,
	/* rcc configuration register 2 */
	RCC_CFGR2	= RCC_BASE + 0x24,
	/* rcc configuration register 3 */
	RCC_CFGR3	= RCC_BASE + 0x30,
	/* clock interrupt register */
	RCC_CIR		= RCC_BASE + 8,
	/* ahb peripheral clock enable register */
	RCC_AHBENR	= RCC_BASE + 0x14,

	/* port a base */
	PORTA_BASE	= 0x48000000,
	/* port a mode register */
	PORTA_MODER 	= PORTA_BASE + 0,
	/* port a bit set/reset register */
	PORTA_BSRR 	= PORTA_BASE + 0x18,

};

static struct struct_devctl stm32f0x_devs[NR_SUPPORTED_DEVICES] =
{
	{
		.next = 0,
		.name = "stm32f051x6",
                .cmdline_options = 0,
		.ram_areas = (const struct struct_memarea[4])
			{
				{ .start = 0x20000000,	.len = 1024 * 8,	.sizes = 0,	},
				{ .start = 0,		.len = 0,		.sizes = 0,	},
			},
		.flash_areas = (const struct struct_memarea[2])
			{
				{
					.start = 0x08000000,	.len = 64 * 1024,
					.sizes = (uint32_t[])
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
				{
					.start = 0,		.len = 0,
					.sizes = 0,
				},
			},
		.dev_open = stm32f0x_dev_open,
		.dev_close = 0,
		.flash_unlock_area = stm32f0x_flash_unlock_area,
		.flash_erase_area = 0,
		.flash_erase_sector = stm32f0x_flash_erase_sector,
		.flash_mass_erase = stm32f0x_flash_mass_erase,
		.flash_program_words = stm32f0x_flash_program_words,
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
static uint32_t stm32f0x_flash_write_routine[] =
{
/* directly include the machine code of the target 'flash_write()' routine */
#include "stm32f0x-flash-write-mcode.h"
};


static int stm32f0x_dev_open(struct struct_devctl * dev, struct libgdb_ctx * ctx)
{
uint32_t x, cr, cr2, cfgr, cfgr2, cfgr3;
int res;

	/* clear any active flash memory controller errors */
	if (libgdb_readwords(ctx, FSR, 1, &x))
		return -1;
	if (x & (PGERR | WRPERR | EOP))
	{
		if (libgdb_writewords(ctx, FSR, 1, (uint32_t[1]) { [0] = x & (PGERR | WRPERR | EOP), }))
			return -1;
		if (libgdb_readwords(ctx, FSR, 1, &x))
			return -1;
		if (x & (PGERR | WRPERR | EOP))
			return -1;
	}
	/* set target clock settings to known values */
	res = 0;
	/* Set HSION bit */
	//RCC->CR |= (uint32_t)0x00000001;
	res += libgdb_readwords(ctx, RCC_CR, 1, & cr);
	cr |= 1;
	res += libgdb_writewords(ctx, RCC_CR, 1, & cr);

	/* Reset SW[1:0], HPRE[3:0], PPRE[2:0], ADCPRE and MCOSEL[2:0] bits */
	//RCC->CFGR &= (uint32_t)0xF8FFB80C;
	res += libgdb_readwords(ctx, RCC_CFGR, 1, & cfgr);
	cfgr &= 0xf8ffb80c;
	res += libgdb_writewords(ctx, RCC_CFGR, 1, & cfgr);

	/* Reset HSEON, CSSON and PLLON bits */
	//RCC->CR &= (uint32_t)0xFEF6FFFF;
	res += libgdb_readwords(ctx, RCC_CR, 1, & cr);
	cr &= 0xfef6ffff;
	res += libgdb_writewords(ctx, RCC_CR, 1, & cr);

	/* Reset HSEBYP bit */
	//RCC->CR &= (uint32_t)0xFFFBFFFF;
	res += libgdb_readwords(ctx, RCC_CR, 1, & cr);
	cr &= 0xfffbffff;
	res += libgdb_writewords(ctx, RCC_CR, 1, & cr);

	/* Reset PLLSRC, PLLXTPRE and PLLMUL[3:0] bits */
	//RCC->CFGR &= (uint32_t)0xFFC0FFFF;
	res += libgdb_readwords(ctx, RCC_CFGR, 1, & cfgr);
	cfgr &= 0xffc0ffff;
	res += libgdb_writewords(ctx, RCC_CFGR, 1, & cfgr);

	/* Reset PREDIV1[3:0] bits */
	//RCC->CFGR2 &= (uint32_t)0xFFFFFFF0;
	res += libgdb_readwords(ctx, RCC_CFGR2, 1, & cfgr2);
	cfgr2 &= 0xfffffff0;
	res += libgdb_writewords(ctx, RCC_CFGR2, 1, & cfgr2);

	/* Reset USARTSW[1:0], I2CSW, CECSW and ADCSW bits */
	//RCC->CFGR3 &= (uint32_t)0xFFFFFEAC;
	res += libgdb_readwords(ctx, RCC_CFGR3, 1, & cfgr3);
	cfgr3 &= 0xfffffeac;
	res += libgdb_writewords(ctx, RCC_CFGR3, 1, & cfgr3);

	/* Reset HSI14 bit */
	//RCC->CR2 &= (uint32_t)0xFFFFFFFE;
	res += libgdb_readwords(ctx, RCC_CR2, 1, & cr2);
	cr2 &= 0xfffffffe;
	res += libgdb_writewords(ctx, RCC_CR2, 1, & cr2);

	/* Disable all interrupts */
	//RCC->CIR = 0x00000000;
	res += libgdb_writewords(ctx, RCC_CIR, 1, (uint32_t[1]) { [0] = 0, } );
  
	/* SYSCLK, HCLK, PCLK configuration ----------------------------------------*/
	//#if defined (PLL_SOURCE_HSI)
	/* At this stage the HSI is already enabled */

	/* Enable Prefetch Buffer and set Flash Latency */
	//FLASH->ACR = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY;
	res += libgdb_writewords(ctx, FACR, 1, (uint32_t[1]) { [0] = 0x10 | 1, } );

	/* HCLK = SYSCLK */
	//RCC->CFGR |= (uint32_t)RCC_CFGR_HPRE_DIV1;
	res += libgdb_readwords(ctx, RCC_CFGR, 1, & cfgr);
	cfgr |= 0;
	res += libgdb_writewords(ctx, RCC_CFGR, 1, & cfgr);

	/* PCLK = HCLK */
	//RCC->CFGR |= (uint32_t)RCC_CFGR_PPRE_DIV1;
	res += libgdb_readwords(ctx, RCC_CFGR, 1, & cfgr);
	cfgr |= 0;
	res += libgdb_writewords(ctx, RCC_CFGR, 1, & cfgr);

	/* PLL configuration = (HSI/2) * 12 = ~48 MHz */
	//RCC->CFGR &= (uint32_t)((uint32_t)~(RCC_CFGR_PLLSRC | RCC_CFGR_PLLXTPRE | RCC_CFGR_PLLMULL));
	//RCC->CFGR |= (uint32_t)(RCC_CFGR_PLLSRC_HSI_Div2 | RCC_CFGR_PLLXTPRE_PREDIV1 | RCC_CFGR_PLLMULL12);
	res += libgdb_readwords(ctx, RCC_CFGR, 1, & cfgr);
	cfgr &= ~(0x10000 | 0x20000 | 0x3c0000);
	res += libgdb_writewords(ctx, RCC_CFGR, 1, & cfgr);

	res += libgdb_readwords(ctx, RCC_CFGR, 1, & cfgr);
	cfgr |= 0 | 0 | 0x280000;
	res += libgdb_writewords(ctx, RCC_CFGR, 1, & cfgr);

	/* Enable PLL */
	//RCC->CR |= RCC_CR_PLLON;
	res += libgdb_readwords(ctx, RCC_CR, 1, & cr);
	cr |= 0x01000000;
	res += libgdb_writewords(ctx, RCC_CR, 1, & cr);

	/* Wait till PLL is ready */
	//while((RCC->CR & RCC_CR_PLLRDY) == 0) { }
	do
		res += libgdb_readwords(ctx, RCC_CR, 1, & cr);
	while (!res && !(cr & 0x02000000));

	/* Select PLL as system clock source */
	//RCC->CFGR &= (uint32_t)((uint32_t)~(RCC_CFGR_SW));
	//RCC->CFGR |= (uint32_t)RCC_CFGR_SW_PLL;    
	res += libgdb_readwords(ctx, RCC_CFGR, 1, & cfgr);
	cfgr &= ~ 3;
	res += libgdb_writewords(ctx, RCC_CFGR, 1, & cfgr);

	res += libgdb_readwords(ctx, RCC_CFGR, 1, & cfgr);
	cfgr |= 2;
	res += libgdb_writewords(ctx, RCC_CFGR, 1, & cfgr);

	/* Wait till PLL is used as system clock source */
	//while ((RCC->CFGR & (uint32_t)RCC_CFGR_SWS) != (uint32_t)RCC_CFGR_SWS_PLL) { }
	do
		res += libgdb_readwords(ctx, RCC_CFGR, 1, & cfgr);
	while (!res && (cfgr & 0xc) != 8);

	/* enable port a clock */
	res += libgdb_readwords(ctx, RCC_AHBENR, 1, & x);
	x |= 1 << 17;
	res += libgdb_writewords(ctx, RCC_AHBENR, 1, & x);
	/* set mco output to be sysclk - could be inspected with an oscilloscope for debug */
	res += libgdb_readwords(ctx, RCC_CFGR, 1, & cfgr);
	cfgr &= ~ (7 << 24);
	cfgr |= 7 << 24;
	res += libgdb_writewords(ctx, RCC_CFGR, 1, & cfgr);
	/* configure port a, pin 8 to alternate function 0 - mco */
	res += libgdb_readwords(ctx, PORTA_MODER, 1, & x);
	x &=~ (3 << 16);
	x |= 2 << 16;
	res += libgdb_writewords(ctx, PORTA_MODER, 1, & x);
	/* with the pll output operating at 48 MHz, 24 MHz should be visible here on mco */

	if (res)
	{
		eprintf("error initializing target system\n");
		return -1;
	}
	else
	{
		printf("target system successfully initialized\n");
		return 0;
	}
}

static bool is_target_flash_locked(struct libgdb_ctx * ctx)
{
uint32_t x;

	if (libgdb_readwords(ctx, FCTRL, 1, &x) || (x & LOCK))
		return true;
	return false;
}

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

static int clear_flash_errors(struct libgdb_ctx * ctx)
{
uint32_t x;

	if (libgdb_readwords(ctx, FSR, 1, &x))
		return -1;
	if (x & BSY)
		return -1;
	x &= WRPERR | PGERR;
	if (!x)
		/* no errors currently active */
		return 0;
	if (libgdb_writewords(ctx, FSR, 1, &x))
		return -1;
	if (libgdb_readwords(ctx, FSR, 1, &x))
		return -1;
	x &= WRPERR | PGERR;
	if (!x)
		/* no errors currently active */
		return 0;
	return -1;
}

static int stm32f0x_flash_unlock_area(struct struct_devctl * dev, struct libgdb_ctx * ctx, const struct struct_memarea * area)
{
	if (!is_target_flash_locked(ctx))
		/* flash already unlocked - nothing to do */
		return 0;
	if (libgdb_writewords(ctx, FKEYR, 1, (uint32_t[1]) { [0] = 0x45670123, }))
		return -1;
	if (libgdb_writewords(ctx, FKEYR, 1, (uint32_t[1]) { [0] = 0xcdef89ab, }))
		return -1;

	if (is_target_flash_locked(ctx))
		return -1;
	return 0;
}

static int stm32f0x_flash_mass_erase(struct struct_devctl * dev, struct libgdb_ctx * ctx)
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

static int stm32f0x_flash_erase_sector(struct struct_devctl * dev, struct libgdb_ctx * ctx, uint32_t sector_nr)
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

static int stm32f0x_flash_program_words(struct struct_devctl * dev, struct libgdb_ctx * ctx, uint32_t dest, uint32_t * src, int wordcnt)
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
				sizeof stm32f0x_flash_write_routine >> 2,
				(uint32_t *) stm32f0x_flash_write_routine))
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

struct struct_devctl * stm32f0x_get_devs(void)
{
	return stm32f0x_devs;
}

#endif /* COMPILING_TARGET_RESIDENT_CODE */

