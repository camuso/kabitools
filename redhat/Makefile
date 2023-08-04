###########################################################################
#
# Makefile for kabitools
#
# This make invokes cross compilers to create the kabitools kit for the
# following architectures. Only 64 bit architectures are created.
#
#	aarch64
#	s390x
#	ppc64
#	ppc64le
#	x86_64
#
###########################################################################

# These are passed to the spec file
VERSION		:= 3.6.4
RELEASE		:= 2

# This github repo contains scripts used to build the kabiscan script.
SCRIPTLIB	:= https://raw.githubusercontent.com/camuso/scripts/master/lib

# The following absolute paths are needed to assure the relative paths
# will work.
RPMBUILD	:= $(shell which rpmbuild)
KABISRC		:= $(shell realpath $(PWD)/..)
REDHAT		:= $(shell realpath $(PWD))

# Relative paths
RPM		:= $(REDHAT)/rpm
SOURCES		:= $(RPM)/SOURCES
BUILD		:= $(RPM)/BUILD
RPMS		:= $(RPM)/RPMS
SRPMS		:= $(RPM)/SRPMS
SPECS		:= $(RPM)/SPECS
SCRIPTS		:= $(PWD)/scripts

# libsparse paths are absolute
SPARSEROOT	:= $(KABISRC)/../libsparse
SPARSERPMS	:= $(SPARSEROOT)/rpm/RPMS
SPARSESPECS	:= $(SPARSEROOT)/rpm/SPECS
SPARSELIB	:= $(SPARSEROOT)/lib

# Tool flags
TARFLAGS	:= --exclude-backups --exclude-vcs --exclude=./redhat -czf

RPMFLAGS = $(RPMBUILD) \
	--define "_topdir	$(RPM)" \
	--define "_sourcedir	$(SOURCES)" \
	--define "_builddir	$(BUILD)" \
	--define "_srcrpmdir	$(SRPMS)" \
	--define "_rpmdir	$(RPMS)" \
	--define "_specdir	$(SPECS)" \
	--define "_kbversion	$(VERSION)" \
	--define "_kbrelease	$(RELEASE)"

ARCH_LIST := \
	s390x \
	ppc64 \
	ppc64le \
	aarch64 \
	x86_64

BOOSTPKG := $(shell rpm -q boost-serialization)
DISTRO	 := $(shell echo $(BOOSTPKG) | rev | cut -d'.' -f2 | rev)
ARCH 	 := $(shell uname -m)

SPARSE_VER := $(shell grep -m1 Version $(SPARSESPECS)/libsparse.spec | cut -d '	' -f2)

CROSS_PACKAGE_LIST = binutils-powerpc64-linux-gnu \
		     binutils-s390x-linux-gnu \
		     cross-binutils-common \
		     cross-gcc-common \
		     gcc-powerpc64-linux-gnu \
		     gcc-s390x-linux-gnu \
		     glibc-static \
		     ncurses-devel \
		     numactl-devel \
		     diffstat

.PHONY: test_env get_scripts

# These are the cross-compile targets
# The final one will also build the src.rpm
#
all:	test_env get_scripts
	@echo "DISTRO: $(DISTRO)"
	@echo "SPARSE_VER: $(SPARSE_VER)"
	$(call make_kabiscan)
	$(call make_tar)

	for arch in $(ARCH_LIST); do \
		echo; echo "*****"; echo "arch = $$arch"; echo "*****"; \
		@make -C $(KABISRC) clean; \
		$(RPMFLAGS) --target $$arch --with cross -bb $(SPECS)/kabitools.spec; \
	done

one: test_env get_scripts
	$(call make_kabiscan)
	$(call make_tar)
	@make -C $(KABISRC) clean
	$(RPMFLAGS) --target x86_64 --with cross -ba $(SPECS)/kabitools.spec

test_env:
	@echo "testing environment ..."
	@if [ "$(ARCH)" != "x86_64" ]; then \
		@echo "\nArch is $(ARCH), but this make must be executed on x86_64.\n"; \
		exit 1; \
	fi
	@if ! find ../../libsparse/rpm/ -type d -name RPMS > /dev/null 2>&1; then \
		@echo "You must build libsparse at $(realpath $(KABISRC)/../libsparse)."; \
		@echo "See https://github.com/camuso/libsparse"; \
		exit 1; \
	fi
	@if ! which rpmbuild > /dev/null 2>&1; then \
		@echo "You must install the rpmbuild package."; \
	fi
	@if ! rpm -q boost-serialization > /dev/null 2>&1; then \
		@cho "You must install the boost and boost-devel packages"; \
	fi

get_scripts:
	@echo "getting scripts..."
	@[ -d $(SCRIPTS) ] || mkdir -p $(SCRIPTS)
	@wget -q -N -P $(SCRIPTS) $(SCRIPTLIB)/kabiscan.source
	@wget -q -N -P $(SCRIPTS) $(SCRIPTLIB)/cfgmgr.source
	@wget -q -N -P $(SCRIPTS) $(SCRIPTLIB)/ui.source

make_tar = \
	tar $(TARFLAGS) $(SOURCES)/kabitools-$(VERSION).tar.gz -C $(KABISRC) .

make_kabiscan = \
	cat $(SCRIPTS)/ui.source > $(KABISRC)/kabiscan; \
	cat $(SCRIPTS)/cfgmgr.source >> $(KABISRC)/kabiscan; \
	cat $(SCRIPTS)/kabiscan.source >> $(KABISRC)/kabiscan; \
	chmod +x $(SCRIPTS)/kabiscan

clean:
	make -C $(KABISRC) clean
	rm -rf $(BUILD)/*
	# find $(RPMS)/ -type f -name "*.rpm" -exec rm -vf '{}' \;
