LDFLAGS = -lws2_32
CFLAGS = -g
TARGET_CFLAGS = -g -ffunction-sections -mcpu=cortex-m3 -mthumb -Os
TARGET_CFLAGS_CM0 = -g -ffunction-sections -mcpu=cortex-m0 -mthumb -Os
CC = i386-mingw32-gcc
TARGET_CC = arm-none-eabi-gcc
TARGET_OBJCOPY = arm-none-eabi-objcopy
OBJECTS = libgdb.dll scribe.o stm32f10x.o stm32f4x.o lpc17xx.o stm32f0x.o hexreader.o
GENERATED_MCODE_HEADERS = stm32f4x-flash-write-mcode.h stm32f10x-flash-write-mcode.h lpc17xx-flash-write-mcode.h \
		stm32f0x-flash-write-mcode.h 
TARGET_OBJECTS = stm32f10x-target.o stm32f4x-target.o stm32f0x-target.o

scribe: $(OBJECTS)
	$(CC) -o $@ $^ $(LDFLAGS)

clean:
	-del $(OBJECTS) $(GENERATED_MCODE_HEADERS) $(TARGET_OBJECTS)

scribe.o: scribe.c
	$(CC) $(CFLAGS) -c -o $@ $<

hexreader.o: hexreader.c
	$(CC) $(CFLAGS) -c -o $@ $<

libgdb.dll:	libgdb.c libgdb.h
	$(CC) $(CFLAGS) -o $@ $< -shared -lws2_32

stm32f10x.o:	stm32f10x.c stm32f10x-flash-write-mcode.h
	$(CC) $(CFLAGS) -c -o $@ $<

stm32f10x-target.o:	stm32f10x.c
	$(TARGET_CC) -DCOMPILING_TARGET_RESIDENT_CODE $(TARGET_CFLAGS) -c -o $@ $<

stm32f10x-flash-write-mcode.h: stm32f10x-target.o
	$(TARGET_OBJCOPY) -j .text.flash_write $< x.bin -O binary
	hdump x.bin > $@

stm32f4x.o:	stm32f4x.c stm32f4x-flash-write-mcode.h
	$(CC) $(CFLAGS) -c -o $@ $<

stm32f4x-target.o:	stm32f4x.c
	$(TARGET_CC) -DCOMPILING_TARGET_RESIDENT_CODE $(TARGET_CFLAGS) -c -o $@ $<

stm32f4x-flash-write-mcode.h: stm32f4x-target.o
	$(TARGET_OBJCOPY) -j .text.flash_write $< x.bin -O binary
	hdump x.bin > $@

stm32f0x.o:	stm32f0x.c stm32f0x-flash-write-mcode.h
	$(CC) $(CFLAGS) -c -o $@ $<

stm32f0x-target.o:	stm32f0x.c
	$(TARGET_CC) -DCOMPILING_TARGET_RESIDENT_CODE $(TARGET_CFLAGS_CM0) -c -o $@ $<

stm32f0x-flash-write-mcode.h: stm32f0x-target.o
	$(TARGET_OBJCOPY) -j .text.flash_write $< x.bin -O binary
	hdump x.bin > $@


lpc17xx.o:	lpc17xx.c lpc17xx-flash-write-mcode.h
	$(CC) $(CFLAGS) -c -o $@ $<

lpc17xx-target.o:	lpc17xx.c
	$(TARGET_CC) -DCOMPILING_TARGET_RESIDENT_CODE $(TARGET_CFLAGS) -c -o $@ $<

lpc17xx-flash-write-mcode.h: lpc17xx-target.o
	$(TARGET_OBJCOPY) -j .text.flash_write $< x.bin -O binary
	hdump x.bin > $@
