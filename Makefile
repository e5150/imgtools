CFLAGS   = -g -O2
CFLAGS  += -Wall -Wextra -Wpedantic
CFLAGS  += -Wwrite-strings -Wdate-time
CFLAGS  += -Wno-unused-parameter -Wunused-but-set-parameter
CFLAGS  += -Wunused-but-set-variable
CFLAGS  += -Wshadow -Wstrict-overflow -fno-strict-aliasing
CFLAGS  +=
CFLAGS  += -I. -D_XOPEN_SOURCE=700 -D_DEFAULT_SOURCE
LDFLAGS  = -L.

SHELL   = /bin/sh
CC      = gcc
CPP     = g++
INSTALL = install

PREFIX  ?= ~/.local

HDRS = optparse.h _optparse.h thpool.h util.h imgcode.h
LIBSRC = util.c thpool.c imgcode.c imgcmp.c
LIBOBJ = $(LIBSRC:.c=.o)
CPPSRC = imgfacedetect.cc
PRGSRC = imgdups.c imghash.c jpgtrim.c
PRGOBJ = $(PRGSRC:.c=.o)
PRGBIN = $(PRGOBJ:.o=) $(CPPSRC:.cc=)
ZSHCMP = $(PRGBIN:%=.zsh/_%)

.SUFFIXES: "" .c
.SUFFIXES: .o .c
.SUFFIXES: "" .cc

all: $(PRGBIN) tags

tags: $(HDRS) $(LIBSRC) $(PRGSRC)
	ctags $^

imghash.o: _optparse.h imghash.c imgcmp.h util.h thpool.h
imgdups.o: _optparse.h imgdups.c imgcmp.h util.h
jpgtrim.o: _optparse.h jpgtrim.c

imgfacedetect: imgfacedetect.cc _optparse.h
	$(CPP) $(CFLAGS)    -o $@ $^ -lopencv_dnn -lopencv_imgcodecs -lopencv_imgproc -lopencv_core -I/usr/include/opencv4
jpgtrim: jpgtrim.o util.o
	$(CC)  $(CFLAGS)    -o $@ $^ -lturbojpeg
imgdups: imgdups.o imgcmp.o util.o
	$(CC)  $(CFLAGS)    -o $@ $^ -lyajl
imghash: imghash.o $(LIBOBJ)
	$(CC)  $(CFLAGS)    -o $@ $^ -lexif -lImlib2 -lpthread -lturbojpeg
%.o: %.c
	$(CC)  $(CFLAGS) -c -o $@ $< $(EXTRAOPTS)

clean:
	@rm -vf $(PRGBIN) $(PRGOBJ) $(LIBOBJ) $(ZSHCMP) core tags *.o *.oo vgcore.* core

install: $(PRGBIN)
	$(INSTALL) -m 755 -Dt $(DESTDIR)$(PREFIX)/bin $^

.zsh/_%: %
	./$< --zsh-comp-gen > $@
zsh: $(ZSHCMP)

zsh-install: $(ZSHCMP)
	$(INSTALL) -m 644 -t ~/.config/zsh/comp/ $^

i: install zsh-install

c: clean

.PHONY:
	all install i clean c zsh
