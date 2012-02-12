#
## B & I Makefile for gcc_select
#
# Copyright Apple Inc. 2002, 2003, 2007, 2009, 2011

#------------------------------------------------------------------------------#

SRCROOT = .
SRC = `cd $(SRCROOT) && pwd | sed s,/private,,`
DEVELOPER_DIR ?= /
DEST_DIR = $(DSTROOT)$(DEVELOPER_DIR)

.PHONY: all install installhdrs installsrc installdoc clean mklinks installsym
.PHONY: Embedded

all: install

Embedded:
	ARM_PLATFORM=`xcodebuild -version -sdk iphoneos PlatformPath` && \
	$(MAKE) DEST_DIR=$(DSTROOT)$$ARM_PLATFORM/Developer mklinks

$(OBJROOT)/c89.o : $(SRCROOT)/c89.c
	$(CC) -c $^ -Wall -Os -g $(RC_CFLAGS) -o $@

$(OBJROOT)/c99.o : $(SRCROOT)/c99.c
	$(CC) -c $^ -Wall -Werror -Os -g $(RC_CFLAGS) -o $@

% : %.o
	$(CC) $^ -g $(RC_CFLAGS) -o $@

%.dSYM : %
	dsymutil $^

install: installdoc mklinks $(OBJROOT)/c99 $(OBJROOT)/c89 installsym
	mkdir -p $(DEST_DIR)/usr/bin
	install -s -c -m 555 $(OBJROOT)/c99 $(DEST_DIR)/usr/bin/c99
	install -s -c -m 555 $(OBJROOT)/c89 $(DEST_DIR)/usr/bin/c89
	install -c -m 555 $(SRCROOT)/cpp $(DEST_DIR)/usr/bin/cpp

installsym: $(OBJROOT)/c99.dSYM $(OBJROOT)/c89.dSYM
	cp -rp $^ $(SYMROOT)

mklinks:
	mkdir -p $(DEST_DIR)/usr/bin
	ln -s llvm-gcc-4.2 $(DEST_DIR)/usr/bin/gcc
	ln -s llvm-g++-4.2 $(DEST_DIR)/usr/bin/g++
	ln -s gcov-4.2 $(DEST_DIR)/usr/bin/gcov
	ln -s ../llvm-gcc-4.2/bin/gcov-4.2 $(DEST_DIR)/usr/bin/gcov-4.2

installsrc:
	if [ $(SRCROOT) != . ]; then  \
	    cp Makefile cpp c99.c c89.c c99.1 c89.1 $(SRCROOT); \
	fi

installdoc:
	mkdir -p $(DEST_DIR)/usr/share/man/man1
	install -c -m 444 $(SRCROOT)/c99.1 $(DEST_DIR)/usr/share/man/man1/c99.1
	install -c -m 444 $(SRCROOT)/c89.1 $(DEST_DIR)/usr/share/man/man1/c89.1

installhdrs:

clean:
	rm -rf $(OBJROOT)/c[89]9{,.dSYM}
