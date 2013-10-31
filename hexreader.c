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

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <malloc.h>
#include <string.h>

#include "hexreader.h"

struct data_mem_area * hexfile_read(const char * fname)
{
FILE * f;
char reclen[2 + 1], offset[4 + 1], rectyp[2 + 1], data[256 + 2 + 1];
int len, offs, rtyp, cksum;
struct data_mem_area * s, * ss;
int i;
char * endptr;
uint32_t addr;
int memlen;


uint8_t dconv(unsigned char * data)
{
int x, y;
	x = tolower(* data ++);
	y = tolower(* data ++);
	x -= isdigit(x) ? '0' : 'a' - 10;
	y -= isdigit(y) ? '0' : 'a' - 10;
	return (x << 4) | y;
}
uint8_t compute_cksum(void)
{
int i, j;
	j = len + (offs >> 8) + offs + rtyp;
	for (i = 0; data[i]; i += 2)
		j += dconv(data + i);
	return - (uint8_t) j;
}
int add_new_mem_area(void)
{
	if (!ss)
		ss = s = calloc(1, sizeof * s);
	else 	/* handle the special case of moving in an
		 * adjacent 64 kb segment */
		if (addr == s->addr + s->len)
			return 1;
	else
		s = s->next = calloc(1, sizeof * s);
	s->addr = addr;
	if (!s || !ss)
		return 0;
	return 1;
}
int add_data(void)
{
void * p;
int i;
unsigned j;
	if (!ss)
	{
		/* memory areas not yet defined - define a new one */
		uint32_t x;
		x = addr;
		addr |= offs;
		add_new_mem_area();
		addr = x;
		if (!ss)
			return 0;
	}
	if (!s->data)
	{
		s->data = malloc(memlen = 64 * sizeof(uint32_t));
		if (!s->data)
			return 0;
		s->addr = addr | offs;
	}
	j = (addr | offs) - s->addr;
	while (j + len > memlen)
	{
		p = realloc(s->data, memlen *= 2);
		if (!p)
			return 0;
		s->data = p;
	}

	for (i = 0; j < memlen && data[i]; s->data[j ++] = dconv(data + i), i +=2);
	if (j >= memlen && data[i])
	{
		printf("j i s 0x%08x memlen is 0x%08x\n", j, memlen);
		return 0;
	}
	s->len = j;
	return 1;
}

	if (!(f = fopen(fname, "r")))
		return 0;

	reclen[2] = offset[4] = rectyp[2] = 0;
	addr = 0;
	memlen = 0;
	ss = s = 0;

	while (!feof(f))
	{
		i = fscanf(f, ":%2c %4c %2c", reclen, offset, rectyp);
		if (i != 3)
			goto end;
		if (!fgets(data, sizeof data, f))
			goto end;
		i = strlen(data);
		if (data[i - 1] == '\n')
			data[-- i] = 0;
		if (i < 2 || (i & 1))
			goto end;
		len = strtol(reclen, & endptr, 16);
		if (*endptr)
			goto end;
		offs = strtol(offset, & endptr, 16);
		if (*endptr)
			goto end;
		rtyp = strtol(rectyp, & endptr, 16);
		if (*endptr)
			goto end;
		cksum = strtol(data + i - 2, & endptr, 16);
		if (*endptr)
			goto end;
		data[i - 2] = 0;
		if (cksum != compute_cksum())
			goto end;
		i -= 2;
		switch (rtyp)
		{
			case 0:
				/* data record */
				if (!add_data())
					goto end;
				break;
			case 1:
				/* end of file */
				goto end;
				break;
			case 2:
				/* extended segment address record */
				if (len != 2 || i != 4)
					goto end;
				addr = strtol(data, & endptr, 16);
				if (*endptr)
					goto end;
				addr <<= 4;
				printf("adding area\n");
				if (!add_new_mem_area())
					goto end;
				break;
			case 3:
				/* start segment address record */
				goto end;
				break;
			case 4:
				/* extended linear address record */
				if (len != 2 || i != 4)
					goto end;
				addr = strtol(data, & endptr, 16);
				if (*endptr)
					goto end;
				addr <<= 16;
				printf("adding area\n");
				if (!add_new_mem_area())
					goto end;
				break;
			case 5:
				/* start linear address record */
				break;
		}
	}
end:
	if (s)
	{
		/* merge any adjacent memory areas */
	}
	fclose(f);
	return ss;
}

void hexfile_dealloc(struct data_mem_area * areas)
{
struct data_mem_area * m, * n;

	if (!areas)
		return;
	for (m = areas; m; n = m->next, free(m), m = n)
		;
}


#if HEXREADER_TEST_DRIVE

int main(int argc, char ** argv)
{
struct data_mem_area * s;

	if (argc != 2)
	{
		printf("usage: %s hexfilename\n", * argv);
		exit(1);
	}
	s = hexfile_read(argv[1]);
	if (!s)
	{
		printf("error reading hexfile\n");
		exit(1);
	}
	else
	{
		while (s)
		{
			printf("memory area: address 0x%08x length 0x%08x\n", s->addr, s->len);
			s = s->next;
		}
	}
	return 0;
}

#endif

