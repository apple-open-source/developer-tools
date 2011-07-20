#
## B & I Makefile for gcc_select
#
# Copyright Apple Inc. 2002, 2003, 2007, 2009, 2011

#------------------------------------------------------------------------------#

# Set V to what we wish to switch to when this project is "built".  Note, you
# can set V from the buildit command line by specifying V=LLVM.

V = LLVM

#------------------------------------------------------------------------------#

SRCROOT = .
SRC = `cd $(SRCROOT) && pwd | sed s,/private,,`

ifeq ($(shell echo $V | tr "[:lower:]" "[:upper:]"),LLVM)
PREFIX=/usr/bin/llvm-
else
PREFIX=
endif

.PHONY: all install installhdrs installsrc installdoc clean mklinks installsym
.PHONY: Embedded

all: install

Embedded: installhdrs
	ARM_PLATFORM=`xcodebuild -version -sdk iphoneos PlatformPath` && \
	$(MAKE) DSTROOT=$(DSTROOT)$$ARM_PLATFORM PREFIX=llvm- \
	    installhdrs mklinks && \
	$(MAKE) DSTROOT=$(DSTROOT)$$ARM_PLATFORM/Developer PREFIX=llvm- \
	    installhdrs mklinks
	ln -s gcc/darwin/default/stdint.h $(DSTROOT)/usr/include/stdint.h

$(OBJROOT)/c89.o : $(SRCROOT)/c89.c
	$(CC) -c $^ -Wall -Os -g $(RC_CFLAGS) -o $@

$(OBJROOT)/c99.o : $(SRCROOT)/c99.c
	$(CC) -c $^ -Wall -Werror -Os -g $(RC_CFLAGS) -o $@

% : %.o
	$(CC) $^ -g $(RC_CFLAGS) -o $@

%.dSYM : %
	dsymutil $^

install: installhdrs

install: installdoc mklinks $(OBJROOT)/c99 $(OBJROOT)/c89 installsym
	mkdir -p $(DSTROOT)/usr/bin
	install -s -c -m 555 $(OBJROOT)/c99 $(DSTROOT)/usr/bin/c99
	install -s -c -m 555 $(OBJROOT)/c89 $(DSTROOT)/usr/bin/c89
	install -c -m 555 $(SRCROOT)/cpp $(DSTROOT)/usr/bin/cpp

installsym: $(OBJROOT)/c99.dSYM $(OBJROOT)/c89.dSYM
	cp -rp $^ $(SYMROOT)

mklinks:
	mkdir -p $(DSTROOT)/usr/bin
	ln -s llvm-gcc-4.2 $(DSTROOT)/usr/bin/gcc
	ln -s llvm-g++-4.2 $(DSTROOT)/usr/bin/g++
	ln -s llvm-g++-4.2 $(DSTROOT)/usr/bin/c++
	ln -s llvm-gcc-4.2 $(DSTROOT)/usr/bin/cc
	ln -s gcov-4.2 $(DSTROOT)/usr/bin/gcov

installsrc:
	if [ $(SRCROOT) != . ]; then  \
	    cp Makefile cpp c99.c c89.c c99.1 c89.1 $(SRCROOT); \
	fi

installdoc:
	mkdir -p $(DSTROOT)/usr/share/man/man1
	install -c -m 444 $(SRCROOT)/c99.1 $(DSTROOT)/usr/share/man/man1/c99.1
	install -c -m 444 $(SRCROOT)/c89.1 $(DSTROOT)/usr/share/man/man1/c89.1
	for prog in gcc g++ c++ gcov cpp ; do \
	  ln -s $${prog}-4.2.1 $(DSTROOT)/usr/share/man/man1/$${prog}.1 || \
	    exit 1 ; \
	done
	ln -s gcc.1 $(DSTROOT)/usr/share/man/man1/cc.1

installhdrs:
	mkdir -p $(DSTROOT)/usr/include/gcc/darwin
	ln -s 4.2 $(DSTROOT)/usr/include/gcc/darwin/default

clean:
	rm -rf $(OBJROOT)/c[89]9{,.dSYM}
