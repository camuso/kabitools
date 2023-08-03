###########################################################################
#
# Makefile for kabitools
#
# This make invokes cross compilers to create the kabitools kit for the
# following architectures. Only 64 bit architectures are created.
#
#	s390x
#	ppc64
#	ppc64le
#	x86_64
#
###########################################################################

VERSION		:= 3.6.3


SCRIPTLIB	:= https://raw.githubusercontent.com/camuso/scripts/master/lib
RPMBUILD	:= $(shell which rpmbuild)
REDHAT		:= $(PWD)
KABISRC		:= $(REDHAT)/..
RPM		:= $(REDHAT)/rpm
SOURCES 	:= $(RPM)/SOURCES
BUILD		:= $(RPM)/BUILD
RPMS		:= $(RPM)/RPMS
SRPMS		:= $(RPM)/SRPMS
SPECS		:= $(RPM)/SPECS
SCRIPTS		:= $(PWD)/scripts

RPM2CPIO	:= $(shell which /usr/bin/rpm2cpio)
CPIO		:= $(shell which /usr/bin/cpio)
CPIOFLAGS	:= -iv --to-stdout
SPARSEROOT	:= ../../libsparse
SPARSERPMS	:= $(SPARSEROOT)/rpm/RPMS
SPARSESPECS	:= $(SPARSEROOT)/rpm/SPECS
SPARSELIB	:= $(SPARSEROOT)/lib

TARFLAGS	:= --exclude-vcs --exclude-backups --exclude=$(REDHAT) -czf

RPMFLAGS = $(RPMBUILD) \
	--define "_topdir	$(RPM)" \
	--define "_sourcedir	$(SOURCES)" \
	--define "_builddir	$(BUILD)" \
	--define "_srcrpmdir	$(SRPMS)" \
	--define "_rpmdir	$(RPMS)" \
	--define "_specdir	$(SPECS)" \
	--define "_kbversion	$(VERSION)"

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
		echo; echo "*****"; echo arch = $$arch; echo "*****"; \
		$(call prepare_lib,$$arch); \
		$(RPMFLAGS) --target $$arch --with cross -bb $(SPECS)/kabitools.spec; \
	done

one: test_env get_scripts
	$(call make_kabiscan)
	$(call make_tar)
	$(call prepare_lib,x86_64)
	$(RPMFLAGS) --target x86_64 --with cross -ba $(SPECS)/kabitools.spec

test_env:
	@echo "testing environment ..."
	@if [ "$(ARCH)" != "x86_64" ]; then \
		@echo "\nArch is $(ARCH), but this make must be executed on x86_64.\n"; \
		exit 1; \
	fi
	@if ! find ../../libsparse/rpm/ -type d -name RPMS > /dev/null 2>&1; then \
		@echo "You must build libsparse at $(realpath ../../libsparse)."; \
		@echo "See https://github.com/camuso/libsparse"; \
		exit 1; \
	fi
	@if ! which rpmbuild > /dev/null 2>&1; then $(call get_package,"rpmbuild"); fi
	@if ! rpm -q boost-serialization > /dev/null 2>&1; then \
		$(call get_package, "boost boost-devel"); \
	fi
	@which cpio > /dev/null 2>&1 || $(call get_package,"cpio")
	@which rpm2cpio > /dev/null 2>&1 || $(call get_package,"cpio")

get_scripts:
	@echo "getting scripts..."
	@[ -d $(PWD)/scripts ] || mkdir -p $(PWD)/scripts
	@wget -q -N -P $(SCRIPTS) $(SCRIPTLIB)/kabiscan.source
	@wget -q -N -P $(SCRIPTS) $(SCRIPTLIB)/cfgmgr.source
	@wget -q -N -P $(SCRIPTS) $(SCRIPTLIB)/ui.source

get_package = \
	if ! sudo dnf install -y $(1); then \
		@echo "You must install $(1) first."; \
		exit 1; \
	fi

report_missingtool = \
	@echo "You must install $(1)"; \
	state=1

make_tar = \
	set -x; \
	tar $(TARFLAGS) $(SOURCES)/kabitools-$(VERSION).tar.gz -C $(KABISRC) .; \
	set +x

make_kabiscan = \
	cat $(SCRIPTS)/ui.source > $(KABISRC)/kabiscan; \
	cat $(SCRIPTS)/project.source >> $(KABISRC)/kabiscan; \
	cat $(SCRIPTS)/kabiscan.source >> $(KABISRC)/kabiscan; \
	chmod +x $(SCRIPTS)/kabiscan

# This procedure requires that the cross-compile of libsparse.a has been
# accomplished and is in the directory tree defined by SPARSEROOT.
# See https://github.com/camuso/libsparse
#
prepare_lib = \
	@echo "sparseroot: $(SPARSEROOT) $(SPARSELIB)"; \
	mkdir -p $(SPARSELIB)/lib/$(1); \
	$(RPM2CPIO) \
	$(SPARSERPMS)/$(1)/libsparse-$(SPARSE_VER)-*.$(DISTRO).$(1).rpm \
	| $(CPIO) $(CPIOFLAGS) ./usr/lib64/libsparse.a > $(SPARSELIB)/libsparse.a

clean:
	make -C $(KABISRC) clean
	rm -rf $(BUILD)/*
	find $(RPMS)/ -type f -name "*.rpm" -exec rm -vf '{}' \;
