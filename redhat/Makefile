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

VERSION		:= 3.5.3

RPMBUILD	:= /usr/bin/rpmbuild
KABIDIR		:= /work/kabi
KABILIB		:= $(KABIDIR)/lib
KABISRC		:= $(KABIDIR)/src
REDHAT		:= $(KABISRC)/redhat
RPM		:= $(REDHAT)/rpm
SOURCES 	:= $(RPM)/SOURCES
BUILD		:= $(RPM)/BUILD
RPMS		:= $(RPM)/RPMS
SRPMS		:= $(RPM)/SRPMS
SPECS		:= $(RPM)/SPECS
ARCH		:= $(shell uname -i)

RPM2CPIO	:= /usr/bin/rpm2cpio
CPIO		:= /usr/bin/cpio
CPIOFLAGS	:= "cpio -iv --to-stdout"
SPARSEROOT	:= /work/sparse
SPARSERPMS	:= $(SPARSEROOT)/redhat/rpm/RPMS
SPARSESPECS	:= $(SPARSEROOT)/redhat/rpm/SPECS

RPMFLAGS = $(RPMBUILD) \
	--define "_topdir	$(RPM)" \
	--define "_sourcedir	$(SOURCES)" \
	--define "_builddir	$(BUILD)" \
	--define "_srcrpmdir	$(SRPMS)" \
	--define "_rpmdir	$(RPMS)" \
	--define "_specdir	$(SPECS)"

ARCH_LIST := \
	s390x \
	ppc64 \
	ppc64le \
	aarch64 \
	x86_64

OS_MEME = $(shell uname -r | rev | cut -d'.' -f2 | rev)
SPARSE_VER = $(shell grep -m1 Version $(SPARSESPECS)/libsparse.spec | cut -d '	' -f2)

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

wrong_arch = 	\
	echo -e "\nThis arch is $(uname -m), but this make must be executed on x86_64.";

make_tar = \
	tar --exclude-vcs -czf $(SOURCES)/kabitools-$(VERSION).tar.gz -C $(KABISRC) .

prepare_lib = \
	set -x; \
	mkdir -p $(KABILIB)/$(1); \
	$(RPM2CPIO) $(SPARSERPMS)/$(1)/libsparse-$(SPARSE_VER)-1.$(OS_MEME).$(1).rpm \
	| $(CPIO) cpio -iv --to-stdout ./usr/lib64/libsparse.a > $(KABILIB)/libsparse.a

# These are the cross-compile targets
# The final one will also build the src.rpm
#
all:
	$(shell [[ $(ARCH) == "x86_64" ]] || $(call wrong_arch))
	echo "OS $(OS_MEME)"
	echo "SPARSE_VER $(SPARSE_VER)"
	$(call make_tar)

	$(shell for arch in s390x ppc64 ppc64le aarch64; do \
		echo arch = $$arch; \
		$(call prepare_lib,$$arch); \
		$(RPMFLAGS) --target $$arch --with cross -bb $(SPECS)/kabitools.spec; \
	done)

	$(call prepare_lib,x86_64)
	$(RPMFLAGS) -ba $(SPECS)/kabitools.spec

clean:
	make -C $(KABISRC) clean
	rm -rf $(BUILD)
