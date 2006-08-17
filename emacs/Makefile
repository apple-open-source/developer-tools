##
# Makefile for emacs
##

Extra_CC_Flags = -no-cpp-precomp -mdynamic-no-pic
Extra_LD_Flags = -Wl,-headerpad,0x1000
Extra_Configure_Flags = --without-x

# Project info
Project  = emacs
UserType = Developer
ToolType = Commands
CommonNoInstallSource = YES
GnuAfterInstall = remove-dir install-dumpemacs

# It's a GNU Source project
include $(MAKEFILEPATH)/CoreOS/ReleaseControl/GNUSource.make

# Regenerate the .elc files after copying the source, since RCS string
# substitution has corrupted several of them.  This is an unavoidable result of
# storing the sources in cvs.

installsrc : CC_Archs = 
installsrc :
	if test ! -d $(SRCROOT) ; then mkdir -p $(SRCROOT); fi;
	tar cf - . | (cd $(SRCROOT) ; tar xfp -)
	for i in `find $(SRCROOT) | grep "CVS$$"` ; do \
		if test -d $$i ; then \
			rm -rf $$i; \
		fi; \
	done
	$(SHELL) -ec \
	'unset CC_PRINT_OPTIONS_FILE CC_PRINT_OPTIONS; cd $(SRCROOT)/emacs; \
	$(Environment) $(Configure) $(Configure_Flags); \
	$(MAKE) bootstrap; \
	$(MAKE) distclean'

remove-dir :
	rm $(DSTROOT)/usr/share/info/dir

install-dumpemacs:
	$(CC) $(CFLAGS) -o $(SYMROOT)/dumpemacs -g $(SRCROOT)/dumpemacs.c \
		$(SRCROOT)/isemacsvalid.c $(SRCROOT)/runit.c
	$(INSTALL) -s -o root -g wheel -m 4555 $(SYMROOT)/dumpemacs $(DSTROOT)/usr/libexec/dumpemacs
	$(CC) $(CFLAGS) -o $(SYMROOT)/emacswrapper -g $(SRCROOT)/emacswrapper.c \
		$(SRCROOT)/isemacsvalid.c $(SRCROOT)/runit.c
	$(INSTALL) -s -o root -g wheel -m 555 $(SYMROOT)/emacswrapper $(DSTROOT)/usr/bin/emacs

