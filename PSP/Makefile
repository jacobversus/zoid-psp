TARGET = zoidpsp
PSPSDK ?= $(shell psp-config --pspsdk-path)
OBJS = main.o softdec.o m4a.o mp4.o video.o net.o radio.o comic.o page.o third_party/bearssl/libbearssl.a
LIBS = -lpng16 -lz -ljpeg -lpspgu -lpsppower -lm -lpspaudio -lpspmp3 -lpspaudiocodec -lpspmpeg -lpspmpegbase -lpspnet -lpspnet_apctl -lpspnet_inet -lpspnet_resolver -lpsputility -lpsprtc -lpspwlan
BUILD_PRX = 1
CFLAGS = -O2 -G0 -Wall
PSP_EBOOT_TITLE = ZOID
EXTRA_TARGETS = EBOOT.PBP
include $(PSPSDK)/lib/build.mak
