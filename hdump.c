#include <stdio.h>
#include <stdint.h>
#include <string.h>

int main(int argc, char ** argv)
{
unsigned int c;
int i;
FILE * f;
uint32_t buf[4];

	if (argc != 2)
	{
		printf("usage: %s infile\n", * argv);
		return - 1;
	}
	if (!(f = fopen(argv[1], "rb")))
	{
		printf("cannot open input file\n");
		return -1;
	}
	i = 0;
	while (!feof(f))
	{
		memset(buf, 0, sizeof buf);
		fread(buf, 1, sizeof buf, f);
		for (i = 0; i < 4; printf("0x%08x, ", buf[i ++]))
			;
		printf("\n");
	}
	fclose(f);
	return 0;
}

