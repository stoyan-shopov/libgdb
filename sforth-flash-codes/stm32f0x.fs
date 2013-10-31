
\ put the sforth engine in a known state

sf-reset

hex

	( declare stm32f0x target registers of interest)
	40022000 constant FBASE
	FBASE 0 + constant FACR
	FBASE 4 + constant FKEYR
	FBASE c + constant FSR

	: bit ( bit-nr -- bitmask) 1 swap lshift ;
	: bconst ( bit-nr -- ) bit constant ;
	( bits in the flash status register)
	0 bconst BSY
	2 bconst PGERR
	4 bconst WRPERR
	5 bconst EOP

	FBASE 10 + constant FCTRL
	( bits in the flash control register)
	7 bconst LOCK
	6 bconst STRT
	2 bconst MER
	1 bconst PER
	0 bconst PG

	( flash address register)
	FBASE 14 + constant FAR

	\ reset and clock control (rcc) registers
	40021000 constant RCC-BASE
	\ rcc control register
	RCC-BASE 0 + constant RCC-CR
	\ rcc control register 2
	RCC-BASE 34 + constant RCC-CR2
	\ rcc configuration register
	RCC-BASE 4 + constant RCC-CFGR
	\ rcc configuration register 2
	RCC-BASE 24 + constant RCC-CFGR2
	\ rcc configuration register 3
	RCC-BASE 30 + constant RCC-CFGR3
	\ clock interrupt register
	RCC-BASE 8 + constant RCC-CIR
	\ ahb peripheral clock enable register
	RCC-BASE 14 + constant RCC-AHBENR
	\ port a base
	48000000 constant PORTA-BASE
	\ port a mode register
	PORTA-BASE 0 + constant PORTA-MODER
	\ port a bit set/reset register
	PORTA-BASE 18 + constant PORTA-BSRR

	\ target flash/ram memory parameters
	08000000 constant TARGET-FLASH-BASE
	10000 constant TARGET-FLASH-SIZE
	20000000 constant TARGET-RAM-BASE
	2000 constant TARGET-RAM-SIZE

	\ parameters related to executing the flasher helper routines on the target
	0 constant EXEC-RETURN-ADDR
	TARGET-RAM-BASE constant FLASHER-ROUTINE-ADDR
	TARGET-RAM-BASE 100 + constant TARGET-FLASH-BUF-ADDR
	400 + constant TARGET-FLASH-BUF-SIZE
	200 constant FLASHER-ROUTINE-STACK-SIZE
	TARGET-FLASH-BUF-ADDR TARGET-FLASH-BUF-SIZE + FLASHER-ROUTINE-STACK-SIZE + constant TARGET-FLASHER-STACK-PTR

\ machine code for the target flasher routine
\ this is the machine code generated for this c routine:

\ #ifdef COMPILING_TARGET_RESIDENT_CODE
\ 
\ #include <stdint.h>
\ 
\ #define FBASE	0x40022000
\ #define FACR	(*(volatile uint32_t *)(FBASE + 0x0))
\ #define FKEYR	(*(volatile uint32_t *)(FBASE + 0x4))
\ #define FSR	(*(volatile uint32_t *)(FBASE + 0xc))
\ #define FCTRL	(*(volatile uint32_t *)(FBASE + 0x10))
\ 
\ enum
\ {
\ 
\ 	/* bits in the flash status register */
\ 	BSY	= 1 << 0,
\ 	PGERR	= 1 << 2,
\ 	WRPERR	= 1 << 4,
\ 	EOP	= 1 << 5,
\ 
\ 	/* bits in the flash control register */
\ 	LOCK	= 1 << 7,
\ 	STRT	= 1 << 6,
\ 	MER	= 1 << 2,
\ 	PER	= 1 << 1,
\ 	PG	= 1 << 0,
\ };
\ 
\ int flash_write(volatile uint32_t * dest, uint32_t * src, uint32_t wordcnt)
\ {
\ volatile uint16_t * hwd, * hws;
\ 
\ 	while (FSR & BSY)
\ 		;
\ 	hwd = (uint16_t *) dest;
\ 	hws = (uint16_t *) src;
\ 	wordcnt <<= 1;
\ 	while (wordcnt --)
\ 	{
\ 		FCTRL = PG;
\ 		* hwd = * hws;
\ 		while (FSR & BSY)
\ 			;
\ 		if (* hwd != * hws)
\ 			return -2;
\ 		hwd ++;
\ 		hws ++;
\ 		if (FSR & (PGERR | WRPERR))
\ 			return -1;
\ 	}
\ 	return 0;
\ }

create TARGET-FLASHER-MCODE
here
4d14b5f7 , 682b2401 , 40234e12 , 0052d1fb , 
1a099201 , 24014f10 , 603ce010 , 8002882a , 
42226832 , 8802d1fc , 42aa882d , 4d09d10c , 
682a3002 , 33012514 , d107422a , 18459a01 , 
d1ea4293 , e0032000 , e0002002 , 42402001 , 
46c0bdfe , 4002200c , 40022010 , 00000000 , 
\ compute the number of words the program occupies
here swap - 1 cells / constant TARGET-FLASHER-MCODE-WORDSIZE


\ c-like operator aliases
: &= ( t-addr mask -- ) \ the c and-equals operator
	over t@ and swap t! ;
: |= ( t-addr mask -- ) \ the c or operator
	over t@ or swap t! ;

\ code for initializing and setting up the target clock system
\ look up the st manuals for register descriptions
: target-setup-clocks
	\ clear any active flash memory controller errors
	FSR t@ PGERR WRPERR EOP or or swap over and
	if
		FSR t!
	else drop
	then

	\ set target clock settings to known values

	\ Set HSION bit
	\ RCC->CR |= (uint32_t)0x00000001;
	RCC-CR 1 |=

	\ Reset SW[1:0], HPRE[3:0], PPRE[2:0], ADCPRE and MCOSEL[2:0] bits
	\ RCC->CFGR &= (uint32_t)0xF8FFB80C;
	RCC-CFGR F8FFB80C &=

	\ Reset HSEON, CSSON and PLLON bits
	\ RCC->CR &= (uint32_t)0xFEF6FFFF;
	RCC-CR FEF6FFFF &=

	\ Reset HSEBYP bit
	\ RCC->CR &= (uint32_t)0xFFFBFFFF;
	RCC-CR FFFBFFFF &=

	\ Reset PLLSRC, PLLXTPRE and PLLMUL[3:0] bits
	\ RCC->CFGR &= (uint32_t)0xFFC0FFFF;
	RCC-CFGR FFC0FFFF &=

	\ Reset PREDIV1[3:0] bits
	\ RCC->CFGR2 &= (uint32_t)0xFFFFFFF0;
	RCC-CFGR2 FFFFFFF0 &=

	\ Reset HSI14 bit
	\ RCC->CR2 &= (uint32_t)0xFFFFFFFE;
	RCC-CR2 FFFFFFFE &=

	\ Reset USARTSW[1:0], I2CSW, CECSW and ADCSW bits
	\ RCC->CFGR3 &= (uint32_t)0xFFFFFEAC;
	RCC-CFGR3 FFFFFEAC &=

	\ Disable all interrupts
	\ RCC->CIR = 0x00000000;
	0 RCC-CIR t!

	\ SYSCLK, HCLK, PCLK configuration ----------------------------------------
	\ At this stage the HSI is already enabled

	\ Enable Prefetch Buffer and set Flash Latency
	\ FLASH->ACR = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY;
	10 1 or FACR t!

	\ HCLK = SYSCLK
	\ RCC->CFGR |= (uint32_t)RCC_CFGR_HPRE_DIV1;
	RCC-CFGR 0 |=

	\ PCLK = HCLK
	\ RCC->CFGR |= (uint32_t)RCC_CFGR_PPRE_DIV1;
	RCC-CFGR 0 |=

	\ PLL configuration = (HSI/2) * 12 = ~48 MHz
	\ RCC->CFGR &= (uint32_t)((uint32_t)~(RCC_CFGR_PLLSRC | RCC_CFGR_PLLXTPRE | RCC_CFGR_PLLMULL));
	RCC-CFGR 10000 20000 3c0000 or or invert &=
	\ RCC->CFGR |= (uint32_t)(RCC_CFGR_PLLSRC_HSI_Div2 | RCC_CFGR_PLLXTPRE_PREDIV1 | RCC_CFGR_PLLMULL12);
	RCC-CFGR 0 0 280000 or or |=

	\ Enable PLL
	\ RCC->CR |= RCC_CR_PLLON;
	RCC-CR 1000000 |=

	\ Wait till PLL is ready
	\ while((RCC->CR & RCC_CR_PLLRDY) == 0) { }
	begin RCC-CR t@ 2000000 and until

	\ Select PLL as system clock source
	\ RCC->CFGR &= (uint32_t)((uint32_t)~(RCC_CFGR_SW));
	RCC-CFGR 3 invert &=
	\ RCC->CFGR |= (uint32_t)RCC_CFGR_SW_PLL;    
	RCC-CFGR 2 |=

	\ the code below is to facilitate debugging
	\ enable port a clock
	decimal
	RCC-AHBENR 17 bit |=
	\ set mco output to be sysclk - could be inspected with an oscilloscope for debug
	RCC-CFGR dup t@ 7 24 lshift dup invert and or swap t!
	\ configure port a, pin 8 to alternate function 0 - mco
	PORTA-MODER dup t@ 3 16 lshift invert and 2 16 lshift or swap t!
	\ with the pll output operating at 48 MHz, 24 MHz should be visible here on mco
	;

: is-target-flash-locked ( -- flash-locked-flag)
	FCTRL t@ LOCK and if TRUE else FALSE then ;

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

: clear-flash-errors ( -- success-flag) \ returns zero on success, nonzero on error
	FSR t@ dup BSY and if ( target flash controller busy) drop -1 exit then
	WRPERR PGERR or and dup 0= if ( no errors active) drop 0 exit then
	FSR t!
	WRPERR PGERR or and dup 0= if ( errors successfully cleared) drop 0 exit then
	-1
	;

int stm32f0x_flash_unlock_area(struct struct_devctl * dev, struct libgdb_ctx * ctx, const struct struct_memarea * area)
: target-flash-unlock ( -- success-flag)
	is-target-flash-locked FALSE = if ( target flash already unlocked) TRUE
	else
		\ write flash unlock sequence to target
		45670123 FKEYR t!
		cdef89ab FKEYR t!
		is-target-flash-locked not
	then
	;

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


