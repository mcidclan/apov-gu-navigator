TARGET = APoV
OBJS = main.o dma.o
CFLAGS = -g0 -Wall -Ofast -fno-signed-zeros \
    -fno-trapping-math -funroll-loops -frename-registers
EXTRA_TARGETS = EBOOT.PBP
LIBS = -lm -lpspgum -lpspgu -lpsprtc -lpsppower
PSP_EBOOT_TITLE = APoV Gu

all: $(OBJS) dma.o
dma.o: dma.s
	$(CC) -I. -I$(PSPSDK)/include -c -x assembler-with-cpp $<
    
PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
