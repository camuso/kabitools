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
RELEASE		:= 3

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

BOOSTPKG := $(shell rpm -q boost-serialization)
DISTRO	 := $(shell echo $(BOOSTPKG) | rev | cut -d'.' -f2 | rev)
ARCH 	 := $(shell uname -m)

SPARSE_VER := $(shell grep -m1 Version $(SPARSESPECS)/libsparse.spec | cut -d '	' -f2)

.PHONY: test_env get_scripts

one: test_env get_scripts make_kabiscan
	$(call make_kabiscan)
	$(call make_tar)
	@make -C $(KABISRC) clean
	$(RPMFLAGS) -ba $(SPECS)/kabitools.spec

make_kabiscan:
	cat $(SCRIPTS)/ui.source > $(KABISRC)/kabiscan
	cat $(SCRIPTS)/cfgmgr.source >> $(KABISRC)/kabiscan
	cat $(KABISRC)/kabiscan.source >> $(KABISRC)/kabiscan
	chmod +x $(KABISRC)/kabiscan

test_env:
	@echo "testing environment ..."
	@if ! stat /usr/lib64/libsparse.a > /dev/null 2>&1; then \
		@echo "You must build and install libsparse." \
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
	@wget -q -N -P $(SCRIPTS) $(SCRIPTLIB)/cfgmgr.source
	@wget -q -N -P $(SCRIPTS) $(SCRIPTLIB)/ui.source

make_tar = \
	tar $(TARFLAGS) $(SOURCES)/kabitools-$(VERSION).tar.gz -C $(KABISRC) .

clean:
	make -C $(KABISRC) clean
	rm -rf $(BUILD)/*
	# find $(RPMS)/ -type f -name "*.rpm" -exec rm -vf '{}' \;
