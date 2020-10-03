TARGET = APoV
OBJS = main.o
CFLAGS = -g0 -O0 -Wall
EXTRA_TARGETS = EBOOT.PBP
LIBS = -lpspgum -lpspgu -lpsprtc -lpsppower
PSP_EBOOT_TITLE = APoV Gu
PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
