GDB_VERSION = 6.3.50-20050815
GDB_RC_VERSION = 966

BINUTILS_VERSION = 2.13-20021117
BINUTILS_RC_VERSION = 46

.PHONY: all clean configure build install installsrc installhdrs headers \
	build-core build-binutils build-gdb \
	install-frameworks-headers\
	install-frameworks-macosx \
	install-binutils-macosx \
	install-gdb-fat \
	install-chmod-macosx install-chmod-macosx-noprocmod \
	install-clean install-source check


# Get the correct setting for SYSTEM_DEVELOPER_TOOLS_DOC_DIR if 
# the platform-variables.make file exists.

OS=MACOS
SYSTEM_DEVELOPER_TOOLS_DOC_DIR=/Developer/Documentation/DocSets/com.apple.ADC_Reference_Library.DeveloperTools.docset/Contents/Resources/Documents/documentation/DeveloperTools


ifndef RC_ARCHS
RC_ARCHS=$(shell /usr/bin/arch)
endif

ifndef SRCROOT
SRCROOT=.
endif

ifndef OBJROOT
OBJROOT=./obj
endif

ifndef SYMROOT
SYMROOT=./sym
endif

ifndef DSTROOT
DSTROOT=./dst
endif

INSTALL=$(SRCROOT)/src/install-sh

CANONICAL_ARCHS := $(foreach arch,$(RC_ARCHS),$(foreach os,$(RC_OS),$(foreach release,$(RC_RELEASE),$(os):$(arch):$(release))))

CANONICAL_ARCHS := $(subst macos:i386:$(RC_RELEASE),i386-apple-darwin,$(CANONICAL_ARCHS))
CANONICAL_ARCHS := $(subst macos:x86_64:$(RC_RELEASE),x86_64-apple-darwin,$(CANONICAL_ARCHS))
CANONICAL_ARCHS := $(subst macos:ppc:$(RC_RELEASE),powerpc-apple-darwin,$(CANONICAL_ARCHS))
CANONICAL_ARCHS := $(subst macos:arm:$(RC_RELEASE),arm-apple-darwin,$(CANONICAL_ARCHS))
CANONICAL_ARCHS := $(subst macos:armv6:$(RC_RELEASE),arm-apple-darwin,$(CANONICAL_ARCHS))

CANONICAL_ARCHS := $(sort $(CANONICAL_ARCHS))

SRCTOP = $(shell cd $(SRCROOT) && pwd)
OBJTOP = $(shell (test -d $(OBJROOT) || $(INSTALL) -c -d $(OBJROOT)) && cd $(OBJROOT) && pwd)
SYMTOP = $(shell (test -d $(SYMROOT) || $(INSTALL) -c -d $(SYMROOT)) && cd $(SYMROOT) && pwd)
DSTTOP = $(shell (test -d $(DSTROOT) || $(INSTALL) -c -d $(DSTROOT)) && cd $(DSTROOT) && pwd)

ARCH_SAYS := $(shell /usr/bin/arch)
ifeq (i386,$(ARCH_SAYS))
BUILD_ARCH := i386-apple-darwin
else
ifeq (ppc,$(ARCH_SAYS))
BUILD_ARCH := powerpc-apple-darwin
else
ifeq (arm,$(ARCH_SAYS))
BUILD_ARCH := arm-apple-darwin
else
BUILD_ARCH := $(ARCH_SAYS)
endif
endif
endif

GDB_VERSION_STRING = $(GDB_VERSION) (Apple version gdb-$(GDB_RC_VERSION))
BINUTILS_VERSION_STRING = "$(BINUTILS_VERSION) (Apple version binutils-$(BINUTILS_RC_VERSION))"

GDB_BINARIES = gdb
GDB_FRAMEWORKS = gdb
GDB_MANPAGES = 

BINUTILS_FRAMEWORKS = bfd binutils
BINUTILS_MANPAGES = 

FRAMEWORKS = $(GDB_FRAMEWORKS) $(BINUTILS_FRAMEWORKS)

ifndef BINUTILS_BUILD_ROOT
BINUTILS_BUILD_ROOT = $(SDKROOT)
endif

BINUTILS_FRAMEWORK_PATH = $(BINUTILS_BUILD_ROOT)/System/Library/PrivateFrameworks
BINUTILS_LIB_PATH = $(BINUTILS_BUILD_ROOT)/usr/lib

BFD_FRAMEWORK = $(BINUTILS_FRAMEWORK_PATH)/bfd.framework
BFD_HEADERS = $(BFD_FRAMEWORK)/Headers

LIBERTY_FRAMEWORK = $(BINUTILS_FRAMEWORK_PATH)/liberty.framework
LIBERTY_HEADERS = $(LIBERTY_FRAMEWORK)/Headers

OPCODES_FRAMEWORK = $(BINUTILS_FRAMEWORK_PATH)/opcodes.framework
OPCODES_HEADERS = $(OPCODES_FRAMEWORK)/Headers

BINUTILS_FRAMEWORK = $(BINUTILS_FRAMEWORK_PATH)/binutils.framework
BINUTILS_HEADERS = $(BINUTILS_FRAMEWORK)/Headers

INTL_FRAMEWORK = $(BINUTILS_BUILD_ROOT)/usr/lib/libintl.dylib
INTL_HEADERS = $(BINUTILS_BUILD_ROOT)/usr/include

TAR = gnutar
CPP = cpp
CC = gcc
CXX = c++
LD = ld
AR = ar
RANLIB = ranlib
NM = nm
CC_FOR_BUILD = gcc

ifndef CDEBUGFLAGS
CDEBUGFLAGS = -gdwarf-2 -Os
endif

CFLAGS = $(CDEBUGFLAGS) $(RC_CFLAGS)
HOST_ARCHITECTURE = UNKNOWN

RC_CFLAGS_NOARCH = $(strip $(shell echo $(RC_CFLAGS) | sed -e 's/-arch [a-z0-9_]*//g'))

SYSTEM_FRAMEWORK = -L../intl -L./intl -L../intl/.libs -L./intl/.libs -lintl -framework System
FRAMEWORK_PREFIX =
FRAMEWORK_SUFFIX =
FRAMEWORK_VERSION = A
FRAMEWORK_VERSION_SUFFIX =

CONFIG_DIR=UNKNOWN
CONF_DIR=UNKNOWN
DEVEXEC_DIR=UNKNOWN
LIBEXEC_BINUTILS_DIR=UNKNOWN
LIBEXEC_GDB_DIR=UNKNOWN
LIBEXEC_LIB_DIR=UNKNOWN
MAN_DIR=UNKNOWN
PRIVATE_FRAMEWORKS_DIR=UNKNOWN

NATIVE_TARGET = unknown--unknown

NATIVE_TARGETS = $(foreach arch1,$(CANONICAL_ARCHS),$(arch1)--$(arch1))

CROSS_TARGETS = $(foreach arch1,$(CANONICAL_ARCHS),$(foreach arch2,$(filter-out $(arch1),$(CANONICAL_ARCHS)),$(arch1)--$(arch2)))

PPC_TARGET=UNKNOWN
I386_TARGET=UNKNOWN

CONFIG_VERBOSE=-v
CONFIG_ENABLE_GDBTK=--enable-gdbtk=no
CONFIG_ENABLE_GDBMI=
CONFIG_ENABLE_BUILD_WARNINGS=--enable-build-warnings
CONFIG_ENABLE_TUI=--disable-tui
CONFIG_ALL_BFD_TARGETS=
CONFIG_ALL_BFD_TARGETS=
ifeq "$(CANONICAL_ARCHS)" "arm-apple-darwin"
	CONFIG_64_BIT_BFD=
else
	CONFIG_64_BIT_BFD=--enable-64-bit-bfd
endif
CONFIG_WITH_MMAP=--with-mmap
CONFIG_ENABLE_SHARED=--disable-shared
CONFIG_MAINTAINER_MODE=
CONFIG_BUILD=--build=$(BUILD_ARCH)
CONFIG_OTHER_OPTIONS?=--disable-serial-configure

ifneq ($(findstring macosx,$(CANONICAL_ARCHS))$(findstring darwin,$(CANONICAL_ARCHS)),)
CC = gcc -arch $(HOST_ARCHITECTURE)
CC_FOR_BUILD = SDKROOT= gcc

# Unset this when building Canadian Cross.  (e.g. an arm native gdb built on an
# i386 system)  This will have a setting like "10.5" which is not valid on the
# iPhone OS platform and our compiler will get linker errors when running
# autoconf tests.
MACOSX_DEPLOYMENT_TARGET=

ifndef CDEBUGFLAGS
CDEBUGFLAGS = -g -fasynchronous-unwind-tables
endif

CFLAGS = $(strip $(RC_CFLAGS_NOARCH) $(CDEBUGFLAGS) -Wall -Wimplicit)
HOST_ARCHITECTURE = $(shell echo $* | sed -e 's/--.*//' -e 's/powerpc/ppc/' -e 's/-apple-macosx.*//' -e 's/-apple-macos.*//' -e 's/-apple-darwin.*//')
endif

MACOSX_FLAGS = \
	CONFIG_DIR=private/etc \
	CONF_DIR=usr/share/gdb \
	DEVEXEC_DIR=usr/bin \
	LIBEXEC_BINUTILS_DIR=usr/libexec/binutils \
	LIBEXEC_GDB_DIR=usr/libexec/gdb \
	LIB_DIR=usr/lib \
	MAN_DIR=usr/share/man \
	PRIVATE_FRAMEWORKS_DIR=System/Library/PrivateFrameworks \
	SOURCE_DIR=System/Developer/Source/Commands/gdb

CONFIGURE_OPTIONS = $(filter-out ,\
	$(CONFIG_VERBOSE) \
	$(CONFIG_ENABLE_GDBTK) \
	$(CONFIG_ENABLE_GDBMI) \
	$(CONFIG_ENABLE_BUILD_WARNINGS) \
	$(CONFIG_ENABLE_TUI) \
	$(CONFIG_ALL_BFD_TARGETS) \
	$(CONFIG_ALL_BFD_TARGETS) \
	$(CONFIG_64_BIT_BFD) \
	$(CONFIG_WITH_MMAP) \
	$(CONFIG_ENABLE_SHARED) \
	$(CONFIG_MAINTAINER_MODE) \
	$(CONFIG_BUILD) \
	$(CONFIG_OTHER_OPTIONS))

MAKE_OPTIONS = \
	prefix='/usr'

EFLAGS = $(filter-out ,\
	CFLAGS='$(CFLAGS)' \
	CC='$(CC)' \
	CPP='$(CPP)' \
	CXX='$(CXX)' \
	LD='$(LD)' \
	AR='$(AR)' \
	RANLIB='$(RANLIB)' \
	NM='$(NM)' \
	CC_FOR_BUILD='$(CC_FOR_BUILD)' \
	HOST_ARCHITECTURE='$(HOST_ARCHITECTURE)' \
	SDKROOT='$(SDKROOT)' \
	BINUTILS_FRAMEWORK_PATH='$(BINUTILS_FRAMEWORK_PATH)' \
	SRCROOT='$(SRCROOT)' \
	$(MAKE_OPTIONS))

SFLAGS = $(EFLAGS)

FFLAGS = \
	$(SFLAGS) \
	SYSTEM_FRAMEWORK='$(SYSTEM_FRAMEWORK)' \
	FRAMEWORK_PREFIX='$(FRAMEWORK_PREFIX)' \
	FRAMEWORK_SUFFIX='$(FRAMEWORK_SUFFIX)' \
	FRAMEWORK_VERSION_SUFFIX='$(FRAMEWORK_VERSION_SUFFIX)'

FRAMEWORK_TARGET=stamp-framework-headers all
framework=-L../$(patsubst liberty,libiberty,$(1)) -l$(patsubst liberty,iberty,$(1))

FSFLAGS = $(SFLAGS)

CONFIGURE_ENV = $(EFLAGS)
MAKE_ENV = $(EFLAGS)

SUBMAKE = $(MAKE_ENV) $(MAKE)

_all: all
		
crossarm: LIBEXEC_GDB_DIR=usr/libexec/gdb
crossarm: CDEBUGFLAGS ?= -gdwarf-2 -D__DARWIN_UNIX03=0


crossarm:;
        
	echo BUILDING CROSSARM; \
	$(RM)  $(SYMROOT)/$(LIBEXEC_GDB_DIR)/gdb-arm-apple-darwin
	for i in $(RC_ARCHS); do \
		$(RM) -r $(OBJROOT)/$${i}-apple-darwin--arm-apple-darwin; \
		$(INSTALL) -c -d $(OBJROOT)/$${i}-apple-darwin--arm-apple-darwin; \
		(cd $(OBJROOT)/$${i}-apple-darwin--arm-apple-darwin/ && \
			$(CONFIGURE_ENV) CC="cc -arch $${i}" $(MACOSX_FLAGS) $(SRCTOP)/src/configure \
				--host=$${i}-apple-darwin \
				--target=arm-apple-darwin \
                                --build=$(BUILD_ARCH) \
				CFLAGS=" -isystem $(SDKROOT)/usr/include $(CDEBUGFLAGS)" \
				$(CONFIGURE_OPTIONS) \
			); \
		mkdir -p $(SYMROOT)/$(LIBEXEC_GDB_DIR); \
		mkdir -p $(DSTROOT)/$(LIBEXEC_GDB_DIR); \
		$(SUBMAKE) $(MACOSX_FLAGS) -C $(OBJROOT)/$${i}-apple-darwin--arm-apple-darwin configure-gdb; \
		$(SUBMAKE) $(MACOSX_FLAGS) -C $(OBJROOT)/$${i}-apple-darwin--arm-apple-darwin \
				-W version.in VERSION='$(GDB_VERSION_STRING)' GDB_RC_VERSION='$(GDB_RC_VERSION)' all-gdb; \
		if [ -e $(SYMROOT)/$(LIBEXEC_GDB_DIR)/gdb-arm-apple-darwin ] ; then \
			lipo -create $(SYMROOT)/$(LIBEXEC_GDB_DIR)/gdb-arm-apple-darwin $(OBJROOT)/$${i}-apple-darwin--arm-apple-darwin/gdb/gdb -output $(SYMROOT)/$(LIBEXEC_GDB_DIR)/gdb-arm-apple-darwin; \
		else \
			lipo -create $(OBJROOT)/$${i}-apple-darwin--arm-apple-darwin/gdb/gdb -output $(SYMROOT)/$(LIBEXEC_GDB_DIR)/gdb-arm-apple-darwin; \
		fi;\
	done;
	(cd $(SYMROOT)/$(LIBEXEC_GDB_DIR)/ ; dsymutil gdb-arm-apple-darwin)
	strip -S -o $(DSTROOT)/$(LIBEXEC_GDB_DIR)/gdb-arm-apple-darwin \
		$(SYMROOT)/$(LIBEXEC_GDB_DIR)/gdb-arm-apple-darwin
	chown root:wheel $(DSTROOT)/$(LIBEXEC_GDB_DIR)/gdb-arm-apple-darwin
	chmod 755 $(DSTROOT)/$(LIBEXEC_GDB_DIR)/gdb-arm-apple-darwin
	mkdir -p ${DSTROOT}/usr/bin
	sed -e 's/version=.*/version=$(GDB_VERSION)-$(GDB_RC_VERSION)/' \
                < $(SRCROOT)/gdb.sh > ${DSTROOT}/usr/bin/gdb
	chmod 755 ${DSTROOT}/usr/bin/gdb

#
# cross
# 
# Build only a cross targets for host architectures. RC_ARCHS specifies
# all of the host architectures to build for, and RC_CROSS_ARCHS specifies
# all of the cross architectures to build for. This can save time when
# building a cross target as you don't have to build all permutations of
# RC_ARCHS. 
#
# SDKROOT can be used to specify a system root for native builds, and
# CROSS_BUILD_SDKROOT can be used to specify a root for cross builds (this
# is usually omitted and the current macos is used, but it can be specified
# if a specifiec MacOSX SDK (10.4, 10.5, etc) is to be used).
#
# The command below will build a cross armv6 gdb to be hosted on i386, ppc,
# and natively on armv6:
#
# sudo ~rc/bin/buildit -arch ppc -arch i386 -arch armv6 -target cross \
#	RC_CROSS_ARCHS=armv6 SDKROOT=/Developer/SDKs/Purple
#


cross: 	LIBEXEC_GDB_DIR=usr/libexec/gdb
cross:;
	echo BUILDING CROSS $(RC_CROSS_ARCHS) for HOST $(RC_ARCHS); \
        for cross_arch in $(RC_CROSS_ARCHS); do \
		cross_arch_full=$${cross_arch}; \
		if [[ "$${cross_arch}" == "armv6" ]] ; then \
			cross_arch=arm; \
		fi; \
		target_arch_vendor_os="$${cross_arch}-apple-darwin"; \
		curr_symroot_output_file="$(SYMROOT)/$(LIBEXEC_GDB_DIR)/gdb-$${target_arch_vendor_os}" ; \
		$(RM)  "$${curr_symroot_output_file}"; \
		for host_arch in $(RC_ARCHS); do \
			host_arch_full="$${host_arch}"; \
			if [[ "$${host_arch}" == "armv6" ]] ; then \
				host_arch="arm"; \
			fi; \
			echo "BUILDING CROSS $${cross_arch_full} for HOST $${host_arch_full}"; \
			host_arch_vendor_os="$${host_arch}-apple-darwin"; \
			curr_objroot="$(OBJROOT)/$${host_arch_full}-apple-darwin--$${cross_arch_full}-apple-darwin"; \
			$(RM) -r "$${curr_objroot}"; \
			$(INSTALL) -c -d "$${curr_objroot}"; \
			if [[ "$${cross_arch}" == "$${host_arch}" ]] ; then \
				echo BUILDING native with -isystem = $(SDKROOT)/usr/include; \
				curr_sdkroot="$(SDKROOT)/usr/include"; \
			else \
				echo BUILDING cross with -isystem = $(CROSS_BUILD_SDKROOT)/usr/include; \
				curr_sdkroot="$(CROSS_BUILD_SDKROOT)/usr/include"; \
			fi; \
			(cd "$${curr_objroot}"/ && \
				$(CONFIGURE_ENV) CC="cc -arch $${host_arch_full}" $(MACOSX_FLAGS) $(SRCTOP)/src/configure \
					--host=$${host_arch_vendor_os} \
					--target=$${target_arch_vendor_os} \
					--build=$(BUILD_ARCH) \
					CFLAGS=" -isystem $${curr_sdkroot} $(CDEBUGFLAGS)" \
					$(CONFIGURE_OPTIONS) \
				); \
			mkdir -p $(SYMROOT)/$(LIBEXEC_GDB_DIR); \
			mkdir -p $(DSTROOT)/$(LIBEXEC_GDB_DIR); \
			$(SUBMAKE) $(MACOSX_FLAGS) -C "$${curr_objroot}" configure-gdb; \
			$(SUBMAKE) $(MACOSX_FLAGS) -C "$${curr_objroot}" \
				-W version.in VERSION='$(GDB_VERSION_STRING)' GDB_RC_VERSION='$(GDB_RC_VERSION)' all-gdb; \
			if [ -e "$${curr_symroot_output_file}" ] ; then \
				lipo -create "$${curr_symroot_output_file}" "$${curr_objroot}/gdb/gdb" -output "$${curr_symroot_output_file}"; \
			else \
				lipo -create "$${curr_objroot}/gdb/gdb" -output "$${curr_symroot_output_file}"; \
			fi; \
		done; \
                (cd $(SYMROOT)/$(LIBEXEC_GDB_DIR)/ ; dsymutil gdb-$${target_arch_vendor_os}); \
		strip -S -o $(DSTROOT)/$(LIBEXEC_GDB_DIR)/gdb-$${target_arch_vendor_os} "$${curr_symroot_output_file}"; \
	done; \
	mkdir -p ${DSTROOT}/usr/bin; \
	sed -e 's/version=.*/version=$(GDB_VERSION)-$(GDB_RC_VERSION)/' \
                < $(SRCROOT)/gdb.sh > ${DSTROOT}/usr/bin/gdb; \
	chmod 755 ${DSTROOT}/usr/bin/gdb; \


$(OBJROOT)/%/stamp-rc-configure:
	$(RM) -r $(OBJROOT)/$*
	$(INSTALL) -c -d $(OBJROOT)/$*
	(cd $(OBJROOT)/$* && \
		$(CONFIGURE_ENV) $(SRCTOP)/src/configure \
			--host=$(shell echo $* | sed -e 's/--.*//') \
			--target=$(shell echo $* | sed -e 's/.*--//') \
			$(CONFIGURE_OPTIONS) \
			)
	touch $@

$(OBJROOT)/%/stamp-rc-configure-cross:
	$(RM) -r $(OBJROOT)/$*
	$(INSTALL) -c -d $(OBJROOT)/$*
	(cd $(OBJROOT)/$* && \
		$(CONFIGURE_ENV) $(SRCTOP)/src/configure \
			--host=$(shell echo $* | sed -e 's/--.*//') \
			--target=$(shell echo $* | sed -e 's/.*--//') \
			$(CONFIGURE_OPTIONS) \
			)
	touch $@

$(OBJROOT)/%/stamp-build-headers:
	$(SUBMAKE) -C $(OBJROOT)/$* configure-intl configure-libiberty configure-bfd configure-opcodes configure-gdb
	$(SUBMAKE) -C $(OBJROOT)/$*/libiberty $(FFLAGS) stamp-framework-headers
	$(SUBMAKE) -C $(OBJROOT)/$*/bfd $(FFLAGS) headers stamp-framework-headers
	$(SUBMAKE) -C $(OBJROOT)/$*/opcodes $(FFLAGS) stamp-framework-headers
	$(SUBMAKE) -C $(OBJROOT)/$* $(FFLAGS) stamp-framework-headers-binutils
	$(SUBMAKE) -C $(OBJROOT)/$*/gdb/doc $(MFLAGS) VERSION='$(GDB_VERSION_STRING)'
	$(SUBMAKE) -C $(OBJROOT)/$* $(MFLAGS) stamp-framework-headers-gdb
	#touch $@

$(OBJROOT)/%/stamp-build-core:
	$(SUBMAKE) -C $(OBJROOT)/$* configure-intl configure-libiberty configure-bfd configure-opcodes
	$(SUBMAKE) -C $(OBJROOT)/$*/intl $(SFLAGS) libintl.la
	$(SUBMAKE) -C $(OBJROOT)/$*/libiberty $(FFLAGS) $(FRAMEWORK_TARGET)
	$(SUBMAKE) -C $(OBJROOT)/$*/bfd $(FFLAGS) headers
	$(SUBMAKE) -C $(OBJROOT)/$*/bfd $(FFLAGS) $(FRAMEWORK_TARGET)
	$(SUBMAKE) -C $(OBJROOT)/$*/opcodes $(FFLAGS) $(FRAMEWORK_TARGET)
	$(SUBMAKE) -C $(OBJROOT)/$* configure-readline configure-intl
	$(SUBMAKE) -C $(OBJROOT)/$*/readline $(MFLAGS) all $(FRAMEWORK_TARGET)
	$(SUBMAKE) -C $(OBJROOT)/$*/intl $(MFLAGS)
	#touch $@

$(OBJROOT)/%/stamp-build-binutils:
	$(SUBMAKE) -C $(OBJROOT)/$* configure-binutils
	$(SUBMAKE) -C $(OBJROOT)/$*/binutils $(FSFLAGS) VERSION='$(BINUTILS_VERSION)' VERSION_STRING='$(BINUTILS_VERSION_STRING)' all
	$(SUBMAKE) -C $(OBJROOT)/$* $(FFLAGS) stamp-framework-headers-binutils
	#touch $@

$(OBJROOT)/%/stamp-build-gdb:
	$(SUBMAKE) -C $(OBJROOT)/$* configure-gdb
	$(SUBMAKE) -C $(OBJROOT)/$*/gdb -W version.in $(MFLAGS) $(FSFLAGS) VERSION='$(GDB_VERSION_STRING)' GDB_RC_VERSION='$(GDB_RC_VERSION)' gdb
ifeq "$(CANONICAL_ARCHS)" "arm-apple-darwin"
	$(SUBMAKE) -C $(OBJROOT)/$* configure-gdbserver
	$(SUBMAKE) -C $(OBJROOT)/$*/gdb/gdbserver $(MFLAGS) $(FSFLAGS) VERSION='$(GDB_VERSION_STRING)' gdbserver
endif

$(OBJROOT)/%/stamp-build-gdb-framework:
	$(SUBMAKE) -C $(OBJROOT)/$* $(FFLAGS) stamp-framework-headers-gdb

$(OBJROOT)/%/stamp-build-gdb-docs:
	$(SUBMAKE) -C $(OBJROOT)/$*/gdb/doc $(MFLAGS) VERSION='$(GDB_VERSION_STRING)' gdb.info
	#touch $@

TEMPLATE_HEADERS = config.h tm.h xm.h nm.h

install-frameworks-headers:

	$(INSTALL) -c -d $(CURRENT_ROOT)/$(PRIVATE_FRAMEWORKS_DIR)

	set -e;	for i in $(FRAMEWORKS); do \
		framedir=$(CURRENT_ROOT)/$(PRIVATE_FRAMEWORKS_DIR)/$${i}.framework; \
		$(INSTALL) -c -d $${framedir}/Versions/A/PrivateHeaders; \
		$(INSTALL) -c -d $${framedir}/Versions/A/Headers; \
		ln -sf A $${framedir}/Versions/Current; \
		ln -sf Versions/Current/PrivateHeaders $${framedir}/PrivateHeaders; \
		ln -sf Versions/Current/Headers $${framedir}/Headers; \
	done

	set -e; for i in $(FRAMEWORKS); do \
		l=`echo $${i} | sed -e 's/liberty/libiberty/;' -e 's/binutils/\./;' -e 's/gdb/\./;'`; \
		(cd $(OBJROOT)/$(firstword $(NATIVE_TARGETS))/$${l}/$${i}.framework/Versions/A \
		 && $(TAR) --exclude=CVS -cf - Headers) \
		| \
		(cd $(CURRENT_ROOT)/$(PRIVATE_FRAMEWORKS_DIR)/$${i}.framework/Versions/A \
		 && $(TAR) -xf -); \
	done

	set -e; for i in gdb; do \
		l=`echo $${i} | sed -e 's/liberty/libiberty/;' -e 's/binutils/\./;' -e 's/gdb/\./;'`; \
		rm -rf $(CURRENT_ROOT)/$(PRIVATE_FRAMEWORKS_DIR)/$${i}.framework/Versions/A/Headers/machine; \
		mkdir -p $(CURRENT_ROOT)/$(PRIVATE_FRAMEWORKS_DIR)/$${i}.framework/Versions/A/Headers/machine; \
		for j in $(NATIVE_TARGETS); do \
			mkdir -p $(CURRENT_ROOT)/$(PRIVATE_FRAMEWORKS_DIR)/$${i}.framework/Versions/A/Headers/machine/$${j}; \
			(cd $(OBJROOT)/$${j}/$${l}/$${i}.framework/Versions/A/Headers/machine \
			 && $(TAR) --exclude=CVS -cf - *) \
			| \
			(cd $(CURRENT_ROOT)/$(PRIVATE_FRAMEWORKS_DIR)/$${i}.framework/Versions/A/Headers/machine/$${j} \
			 && $(TAR) -xf -) \
		done; \
		for h in $(TEMPLATE_HEADERS); do \
			hg=`echo $${h} | sed -e 's/\.h//' -e 'y/abcdefghijklmnopqrstuvwxyz/ABCDEFGHIJKLMNOPQRSTUVWXYZ/'`; \
			rm -f $(CURRENT_ROOT)/$(PRIVATE_FRAMEWORKS_DIR)/$${i}.framework/Versions/A/Headers/$${h}; \
			ln -s machine/$${h} $(CURRENT_ROOT)/$(PRIVATE_FRAMEWORKS_DIR)/$${i}.framework/Versions/A/Headers/$${h}; \
			cat template.h | sed -e "s/@file@/$${h}/g" -e "s/@FILEGUARD@/_CONFIG_$${hg}_H_/" \
				> $(CURRENT_ROOT)/$(PRIVATE_FRAMEWORKS_DIR)/$${i}.framework/Versions/A/Headers/machine/$${h}; \
		done; \
	done

install-frameworks-resources:

	$(INSTALL) -c -d $(CURRENT_ROOT)/$(PRIVATE_FRAMEWORKS_DIR)

	set -e;	for i in $(GDB_FRAMEWORKS); do \
		framedir=$(CURRENT_ROOT)/$(PRIVATE_FRAMEWORKS_DIR)/$${i}.framework; \
		$(INSTALL) -c -d $${framedir}/Versions/A/Resources; \
		ln -sf Versions/Current/Resources $${framedir}/Resources; \
		$(INSTALL) -c -d $${framedir}/Versions/A/Resources/English.lproj; \
		cat $(SRCROOT)/Info-macos.template | sed -e "s/@NAME@/$$i/g" -e 's/@VERSION@/$(GDB_RC_VERSION)/g' > \
			$${framedir}/Versions/A/Resources/Info-macos.plist; \
	done
	set -e;	for i in $(BINUTILS_FRAMEWORKS); do \
		framedir=$(CURRENT_ROOT)/$(PRIVATE_FRAMEWORKS_DIR)/$${i}.framework; \
		$(INSTALL) -c -d $${framedir}/Versions/A/Resources; \
		ln -sf Versions/Current/Resources $${framedir}/Resources; \
		$(INSTALL) -c -d $${framedir}/Versions/A/Resources/English.lproj; \
		cat $(SRCROOT)/Info-macos.template | sed -e "s/@NAME@/$$i/g" -e 's/@VERSION@/$(BINUTILS_RC_VERSION)/g' > \
			$${framedir}/Versions/A/Resources/Info-macos.plist; \
	done

install-frameworks-macosx:

	$(SUBMAKE) CURRENT_ROOT=$(SYMROOT) install-frameworks-headers
	$(SUBMAKE) CURRENT_ROOT=$(DSTROOT) install-frameworks-headers
	$(SUBMAKE) CURRENT_ROOT=$(SYMROOT) install-frameworks-resources
	$(SUBMAKE) CURRENT_ROOT=$(DSTROOT) install-frameworks-resources

install-gdb-common:

	set -e; for dstroot in $(SYMROOT) $(DSTROOT); do \
		\
		$(INSTALL) -c -d $${dstroot}/$(DEVEXEC_DIR); \
		$(INSTALL) -c -d $${dstroot}/$(CONFIG_DIR); \
		$(INSTALL) -c -d $${dstroot}/$(CONF_DIR); \
		$(INSTALL) -c -d $${dstroot}/$(MAN_DIR); \
		\
		docroot="$${dstroot}/$(SYSTEM_DEVELOPER_TOOLS_DOC_DIR)/gdb"; \
		\
		$(INSTALL) -c -d "$${docroot}"; \
		\
		$(INSTALL) -c -m 644 $(SRCROOT)/doc/refcard.pdf "$${docroot}/refcard.pdf"; \
		\
	done;

install-gdb-macosx-common: install-gdb-common

	set -e; for dstroot in $(SYMROOT) $(DSTROOT); do \
		\
		$(INSTALL) -c -d $${dstroot}/$(LIBEXEC_GDB_DIR); \
		\
		docroot="$${dstroot}/$(SYSTEM_DEVELOPER_TOOLS_DOC_DIR)/gdb"; \
		\
		for i in gdb gdbint stabs; do \
			$(INSTALL) -c -d "$${docroot}/$${i}"; \
			(cd "$${docroot}/$${i}" && \
				$(SRCROOT)/texi2html \
					-split_chapter \
					-I$(OBJROOT)/$(firstword $(NATIVE_TARGETS))/gdb/doc \
					-I$(SRCROOT)/src/readline/doc \
					-I$(SRCROOT)/src/gdb/mi \
					$(SRCROOT)/src/gdb/doc/$${i}.texinfo); \
		done; \
		\
		$(INSTALL) -c -d $${dstroot}/$(MAN_DIR)/man1; \
		$(INSTALL) -c -m 644 $(SRCROOT)/src/gdb/gdb.1 $${dstroot}/$(MAN_DIR)/man1/gdb.1; \
		perl -pi -e 's,GDB_DOCUMENTATION_DIRECTORY,$(SYSTEM_DEVELOPER_TOOLS_DOC_DIR)/gdb,' $${dstroot}/$(MAN_DIR)/man1/gdb.1; \
		\
		$(INSTALL) -c -d $${dstroot}/usr/local/OpenSourceLicenses; \
		$(INSTALL) -c -d $${dstroot}/usr/local/OpenSourceVersions; \
		$(INSTALL) -c -m 644 $(SRCROOT)/gdb.plist $${dstroot}/usr/local/OpenSourceVersions; \
		$(INSTALL) -c -m 644 $(SRCROOT)/gdb.txt $${dstroot}/usr/local/OpenSourceLicenses; \
		\
		$(INSTALL) -c -d $${dstroot}/$(CONFIG_DIR); \
		$(INSTALL) -c -m 644 $(SRCROOT)/gdb.conf $${dstroot}/$(CONFIG_DIR)/gdb.conf; \
		\
		$(INSTALL) -c -d $${dstroot}/$(CONF_DIR); \
		for j in $(SRCROOT)/conf/*.gdb; do \
			$(INSTALL) -c -m 644 $$j $${dstroot}/$(CONF_DIR)/; \
		done; \
		\
		sed -e 's/version=.*/version=$(GDB_VERSION)-$(GDB_RC_VERSION)/' \
			< $(SRCROOT)/gdb.sh > $${dstroot}/usr/bin/gdb; \
		chmod 755 $${dstroot}/usr/bin/gdb; \
		\
	done;

install-gdb-macosx: install-gdb-macosx-common

	set -e; for target in $(CANONICAL_ARCHS); do \
		lipo -create $(OBJROOT)/$${target}--$${target}/gdb/gdb \
			-output $(SYMROOT)/$(LIBEXEC_GDB_DIR)/gdb-$${target}; \
		dsymutil -o $(SYMROOT)/$(LIBEXEC_GDB_DIR)/gdb-$${target}.dSYM \
                         $(SYMROOT)/$(LIBEXEC_GDB_DIR)/gdb-$${target}; \
	 	strip -S -o $(DSTROOT)/$(LIBEXEC_GDB_DIR)/gdb-$${target} \
			$(SYMROOT)/$(LIBEXEC_GDB_DIR)/gdb-$${target}; \
		cp $(DSTROOT)/$(LIBEXEC_GDB_DIR)/gdb-$${target} $(SYMROOT)/$(LIBEXEC_GDB_DIR)/gdb-$${target}; \
	done

install-gdb-fat: install-gdb-macosx-common

	lipo -create $(patsubst %,$(OBJROOT)/%/gdb/gdb,$(PPC_TARGET)--$(PPC_TARGET) $(I386_TARGET)--$(PPC_TARGET)) \
		-output $(SYMROOT)/$(LIBEXEC_GDB_DIR)/gdb-$(PPC_TARGET)
	lipo -create $(patsubst %,$(OBJROOT)/%/gdb/gdb,$(PPC_TARGET)--$(I386_TARGET) $(I386_TARGET)--$(I386_TARGET)) \
		-output $(SYMROOT)/$(LIBEXEC_GDB_DIR)/gdb-$(I386_TARGET)

	set -e; for target in $(CANONICAL_ARCHS); do \
		dsymutil -o $(SYMROOT)/$(LIBEXEC_GDB_DIR)/gdb-$${target}.dSYM \
                         $(SYMROOT)/$(LIBEXEC_GDB_DIR)/gdb-$${target}; \
	 	strip -S -o $(DSTROOT)/$(LIBEXEC_GDB_DIR)/gdb-$${target} \
			$(SYMROOT)/$(LIBEXEC_GDB_DIR)/gdb-$${target}; \
		cp $(DSTROOT)/$(LIBEXEC_GDB_DIR)/gdb-$${target} \
                   $(SYMROOT)/$(LIBEXEC_GDB_DIR)/gdb-$${target}; \
		if echo $${target} | egrep '^[^-]*-apple-darwin' > /dev/null; then \
			echo "stripping __objcInit"; \
			echo "__objcInit" > /tmp/macosx-syms-to-remove; \
			strip -R /tmp/macosx-syms-to-remove -X $(DSTROOT)/$(LIBEXEC_GDB_DIR)/gdb-$${target} || true; \
			rm -f /tmp/macosx-syms-to-remove; \
		fi; \
	done

# At some point this should be some lipo & fat kind of thing.  Only ARM for now...
install-gdbserver:
	set -e; for dstroot in $(SYMROOT) $(DSTROOT); do \
		$(INSTALL) -c -m 755 $(OBJROOT)/$(ARM_TARGET)--$(ARM_TARGET)/gdb/gdbserver/gdbserver $${dstroot}/usr/bin/gdbserver; \
	done
	strip -S $(DSTROOT)/usr/bin/gdbserver

install-binutils-macosx:

	set -e; for i in $(BINUTILS_BINARIES); do \
		instname=`echo $${i} | sed -e 's/\\-new//'`; \
		lipo -create $(patsubst %,$(OBJROOT)/%/binutils/$${i},$(NATIVE_TARGETS)) \
			-output $(SYMROOT)/$(LIBEXEC_BINUTILS_DIR)/$${instname}; \
		strip -S -o $(DSTROOT)/$(LIBEXEC_BINUTILS_DIR)/$${instname} $(SYMROOT)/$(LIBEXEC_BINUTILS_DIR)/$${instname}; \
	done

install-chmod-macosx:
	set -e; for dstroot in $(SYMROOT) $(DSTROOT); do \
			chown -R root:wheel $${dstroot}; \
			chmod -R  u=rwX,g=rX,o=rX $${dstroot}; \
			chmod a+x $${dstroot}/$(LIBEXEC_GDB_DIR)/*; \
			chmod a+x $${dstroot}/$(DEVEXEC_DIR)/*; \
		done
	-set -e; for dstroot in $(SYMROOT) $(DSTROOT); do \
			chgrp procmod $${dstroot}/$(LIBEXEC_GDB_DIR)/gdb* && chmod g+s $${dstroot}/$(LIBEXEC_GDB_DIR)/gdb*; \
			chgrp procmod $${dstroot}/$(LIBEXEC_GDB_DIR)/plugins/MacsBug/MacsBug_plugin && chmod g+s $${dstroot}/$(LIBEXEC_GDB_DIR)/plugins/MacsBug/MacsBug_plugin; \
		done

install-chmod-macosx-noprocmod:
	set -e; for dstroot in $(SYMROOT) $(DSTROOT); do \
			chown -R root:wheel $${dstroot}; \
			chmod -R  u=rwX,g=rX,o=rX $${dstroot}; \
			chmod a+x $${dstroot}/$(LIBEXEC_GDB_DIR)/*; \
			chmod a+x $${dstroot}/$(DEVEXEC_DIR)/*; \
			chmod 755 $${dstroot}/$(LIBEXEC_GDB_DIR)/gdb-*-apple-darwin; \
		done

install-source:
	$(INSTALL) -c -d $(DSTROOT)/$(SOURCE_DIR)
	$(TAR) --exclude=CVS -C $(SRCROOT) -cf - . | $(TAR) -C $(DSTROOT)/$(SOURCE_DIR) -xf -

all: build

clean:
	$(RM) -r $(OBJROOT)

check-args:
ifeq "$(CANONICAL_ARCHS)" "i386-apple-darwin"
else
ifeq "$(CANONICAL_ARCHS)" "powerpc-apple-darwin"
else
ifeq "$(CANONICAL_ARCHS)" "arm-apple-darwin"
else
ifeq "$(CANONICAL_ARCHS)" "i386-apple-darwin powerpc-apple-darwin"
else
ifeq "$(CANONICAL_ARCHS)" "arm-apple-darwin i386-apple-darwin"
else
ifeq "$(CANONICAL_ARCHS)" "arm-apple-darwin i386-apple-darwin powerpc-apple-darwin"
else
	echo "Unknown architecture string: \"$(CANONICAL_ARCHS)\""
	exit 1
endif
endif
endif
endif
endif
endif

configure: 
ifneq ($(findstring darwin,$(CANONICAL_ARCHS)),)
ifneq ($(NATIVE_TARGETS),)
	$(SUBMAKE) $(patsubst %,$(OBJROOT)/%/stamp-rc-configure, $(NATIVE_TARGETS))
endif
ifneq ($(CROSS_TARGETS),)
	$(SUBMAKE) $(patsubst %,$(OBJROOT)/%/stamp-rc-configure-cross, $(CROSS_TARGETS))
endif
endif

build-headers:
	$(SUBMAKE) configure
ifneq ($(NATIVE_TARGETS),)
	$(SUBMAKE) $(patsubst %,$(OBJROOT)/%/stamp-build-headers, $(NATIVE_TARGETS)) 
endif
ifneq ($(CROSS_TARGETS),)
	$(SUBMAKE) $(patsubst %,$(OBJROOT)/%/stamp-build-headers, $(CROSS_TARGETS))
endif

build-core:
	$(SUBMAKE) configure
ifneq ($(NATIVE_TARGETS),)
	$(SUBMAKE) $(patsubst %,$(OBJROOT)/%/stamp-build-core, $(NATIVE_TARGETS)) 
endif
ifneq ($(CROSS_TARGETS),)
	$(SUBMAKE) $(patsubst %,$(OBJROOT)/%/stamp-build-core, $(CROSS_TARGETS))
endif

build-binutils:
	$(SUBMAKE) configure
ifneq ($(NATIVE_TARGETS),)
	$(SUBMAKE) $(patsubst %,$(OBJROOT)/%/stamp-build-binutils, $(NATIVE_TARGETS))
endif

build-gdb:
	$(SUBMAKE) configure
ifneq ($(NATIVE_TARGETS),)
	$(SUBMAKE) $(patsubst %,$(OBJROOT)/%/stamp-build-gdb, $(NATIVE_TARGETS))
	$(SUBMAKE) $(patsubst %,$(OBJROOT)/%/stamp-build-gdb-framework, $(NATIVE_TARGETS))
endif
ifneq ($(CROSS_TARGETS),)
	$(SUBMAKE) $(patsubst %,$(OBJROOT)/%/stamp-build-gdb, $(CROSS_TARGETS))
endif

build-gdb-docs:
	$(MAKE) configure
ifneq ($(NATIVE_TARGETS),)
	$(MAKE) $(patsubst %,$(OBJROOT)/%/stamp-build-gdb-docs, $(NATIVE_TARGETS))
endif
ifneq ($(CROSS_TARGETS),)
	$(MAKE) $(patsubst %,$(OBJROOT)/%/stamp-build-gdb-docs, $(CROSS_TARGETS))
endif

build:
	$(SUBMAKE) check-args
	$(SUBMAKE) build-core
	$(SUBMAKE) build-binutils
	$(SUBMAKE) build-gdb 
	$(SUBMAKE) build-gdb-docs 

install-clean:
	$(RM) -r $(DSTROOT)

install-macosx:
	$(SUBMAKE) install-clean
ifeq "$(CANONICAL_ARCHS)" "i386-apple-darwin powerpc-apple-darwin"
	$(SUBMAKE) install-frameworks-macosx NATIVE_TARGET=unknown--unknown
	$(SUBMAKE) install-binutils-macosx NATIVE_TARGET=unknown--unknown
	$(SUBMAKE) install-gdb-fat NATIVE_TARGET=unknown--unknown PPC_TARGET=powerpc-apple-darwin I386_TARGET=i386-apple-darwin
	$(SUBMAKE) install-macsbug
else
	$(SUBMAKE) install-frameworks-macosx
	$(SUBMAKE) install-binutils-macosx
	$(SUBMAKE) install-gdb-macosx
ifeq "$(CANONICAL_ARCHS)" "powerpc-apple-darwin"
	$(SUBMAKE) install-macsbug
endif
ifeq "$(CANONICAL_ARCHS)" "arm-apple-darwin"
	$(SUBMAKE) install-gdbserver ARM_TARGET=arm-apple-darwin
endif
endif
ifeq "$(CANONICAL_ARCHS)" "arm-apple-darwin"
	$(SUBMAKE) install-chmod-macosx-noprocmod
else
	$(SUBMAKE) install-chmod-macosx
endif

install-macsbug:
	$(SUBMAKE) -C $(SRCROOT)/macsbug GDB_BUILD_ROOT=$(DSTROOT) BINUTILS_BUILD_ROOT=$(DSTROOT) SRCROOT=$(SRCROOT)/macsbug OBJROOT=$(OBJROOT)/powerpc-apple-darwin--powerpc-apple-darwin/macsbug SYMROOT=$(SYMROOT) DSTROOT=$(DSTROOT) install
 
install:
	$(SUBMAKE) check-args
	$(SUBMAKE) build
	$(SUBMAKE) $(MACOSX_FLAGS) install-macosx
ifeq "$(CANONICAL_ARCHS)" "arm-apple-darwin"
	$(SUBMAKE) $(MACOSX_FLAGS) install-chmod-macosx-noprocmod
else
	$(SUBMAKE) $(MACOSX_FLAGS) install-chmod-macosx
endif

installhdrs:
	$(SUBMAKE) check-args
	$(SUBMAKE) configure 
	$(SUBMAKE) build-headers
	$(SUBMAKE) install-clean
	$(SUBMAKE) $(MACOSX_FLAGS) CURRENT_ROOT=$(SYMROOT) install-frameworks-headers
	$(SUBMAKE) $(MACOSX_FLAGS) CURRENT_ROOT=$(DSTROOT) install-frameworks-headers

installsrc:
	$(SUBMAKE) check
	$(TAR) --dereference --exclude=CVS --exclude=src/contrib --exclude=src/dejagnu --exclude=src/etc --exclude=src/expect --exclude=src/sim --exclude=src/tcl --exclude=src/texinfo --exclude=src/utils -cf - . | $(TAR) -C $(SRCROOT) -xf -



check:
	@[ -z "`find . -name \*~ -o -name .\#\*`" ] || \
	   (echo; echo 'Emacs or CVS backup files present; not copying:'; \
           find . \( -name \*~ -o -name .#\* \) -print | sed 's,^[.]/,  ,'; \
           echo Suggest: ; \
           echo '    ' find . \\\( -name \\\*~ -o -name .#\\\* \\\) -exec rm -f \{\} \\\; -print ; \
           echo; \
           exit 1)
