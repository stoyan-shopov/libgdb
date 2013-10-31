/*

Copyright (C) 2011 stoyan shopov

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

#include "libgdb.h"

enum
{

	FBASE	= 0x40000000 + 0x20000 + 0x3c00,
	FACR	= FBASE + 0x0,
	FKEYR	= FBASE + 0x4,
	FSR	= FBASE + 0xc,
	/* bits in the flash status register */
	BSY	= 1 << 16,

	FCTRL = FBASE + 0x10,
	/* bits in the flash control register */
	STRT	= 1 << 16,
	MER	= 1 << 2,

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

static int stm32f4_flash_unlock(struct libgdb_ctx * ctx)
{
	if (libgdb_writewords(ctx, FKEYR, 1, (uint32_t[1]) { [0] = 0x45670123, }))
		return -1;
	if (libgdb_writewords(ctx, FKEYR, 1, (uint32_t[1]) { [0] = 0xcdef89ab, }))
		return -1;
	return 0;
}

static int stm32f4_flash_mass_erase(struct libgdb_ctx * ctx)
{
uint32_t x;
	while (1)
	{
		if (libgdb_readwords(ctx, FSR, 1, &x))
			return -1;
		if (!(x & BSY))
			break;
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
	return 0;
}
enum
{
	BUF_LEN		=	1024 * 8 / 4,
};

int main(void)
{
struct libgdb_ctx * ctx;
int i;
uint32_t buf[BUF_LEN], addr;
int wordcnt;
struct timeval tv1, tv2;
struct timezone tz;
int diff;
double dx;

/* int flash_write(uint32_t * src, uint32_t * dest, uint32_t wordcnt) */
static uint8_t stm32f4_flash_write_routine[] =
{
0xf0, 0xb5, 0x0f, 0x4c, 0x23, 0x68, 0x13, 0xf4, 0x80, 0x33, 0xfb, 0xd1, 0x0c, 0x4c, 0x0d, 0x4d, 
0x26, 0x46, 0x0d, 0xe0, 0x2f, 0x68, 0x47, 0xf0, 0x01, 0x07, 0x2f, 0x60, 0x50, 0xf8, 0x04, 0x7b, 
0x41, 0xf8, 0x04, 0x7b, 0x27, 0x68, 0xff, 0x03, 0xfc, 0xd4, 0x37, 0x68, 0x01, 0x33, 0x1f, 0xb9, 
0x93, 0x42, 0xef, 0xd1, 0x00, 0x20, 0xf0, 0xbd, 0x4f, 0xf0, 0xff, 0x30, 0xf0, 0xbd, 0x00, 0xbf, 
0x0c, 0x5c, 0x00, 0x40, 0x10, 0x5c, 0x00, 0x40, 0x00, 0x00, 0x00
};

	if (!(ctx = libgdb_init()))
	{
		printf("failed to initialize the libgdb library\n");
		exit(1);
	}
	if (libgdb_connect(ctx, "127.0.0.1", 1122))
	{
		printf("failed to connect to a gdb server\n");
		exit(2);
	}

	libgdb_send_ack(ctx);
	libgdb_sendpacket(ctx, "c");
	libgdb_sendbreak(ctx);
	libgdb_waithalted(ctx);
	libgdb_set_max_nr_words_xferred(ctx, 67);

	goto test_mem_read_speed;

	if (stm32f4_flash_unlock(ctx))
	{
		printf("error unlocking target flash, target may need reset\n");
		return - 1;
	}
	if (stm32f4_flash_mass_erase(ctx))
	{
		printf("error mass erasing target flash, target may need reset\n");
		return - 1;
	}
	/* load the flash write routine */
	if (libgdb_writewords(ctx, STM32F4_FLASH_WRITE_ROUTINE_ADDR, sizeof stm32f4_flash_write_routine >> 2, (uint32_t *) stm32f4_flash_write_routine))
	{
		printf("error loading flash writing routine into target\n");
		return - 1;
	}

	return 0;

	for (i = 0; i < 16; i ++)
		buf[i] = i + 1;
	if (libgdb_writewords(ctx, addr = STM32F4_RAM_BASE_ADDR, 16, buf))
	{
		printf("error writing target memory\n");
		exit(2);
	}
	memset(buf, 0, sizeof buf);
	libgdb_set_max_nr_words_xferred(ctx, 32);
	return 0;

test_mem_read_speed:

	gettimeofday(&tv1, &tz);
	if (libgdb_readwords(ctx, addr = STM32F4_RAM_BASE_ADDR, wordcnt = BUF_LEN, buf))
	{
		printf("error reading target memory\n");
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


	gettimeofday(&tv1, &tz);
	if (libgdb_writewords(ctx, addr = STM32F4_RAM_BASE_ADDR, wordcnt = BUF_LEN, buf))
	{
		printf("error writing target memory\n");
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


	printf("memory at 0x%08x\n", addr);
	for (i = 0; i < 16; i ++)
		printf("0x%08x, ", buf[i]);
	return 0;

	printf("target register file:\n", addr);
	for (i = 0; i < 17; i ++)
	{
		if (libgdb_readreg(ctx, i, buf))
		{
			printf("error reading register %i\n", i);
			exit(2);
		}
		printf("0x%08x, ", * buf);
	}

	return 0;
}

