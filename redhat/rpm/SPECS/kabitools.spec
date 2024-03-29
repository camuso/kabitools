###########################################################################
#
# kabitools.spec
#
# This spec file is intended to be invoked only from a Makefile that has
# correctly defined the directory tree in which this will be built.
# The make defines the following make variables and redefines the rpmbuild
# directory macros to correspond to the build environment.
#
#	RPMBUILD	:= $(shell which rpmbuild)
#	REDHAT		:= $(PWD)
#	KABISRC		:= $(REDHAT)/..
#	RPM		:= $(REDHAT)/rpm
#	SOURCES 	:= $(RPM)/SOURCES
#	BUILD		:= $(RPM)/BUILD
#	RPMS		:= $(RPM)/RPMS
#	SRPMS		:= $(RPM)/SRPMS
#	SPECS		:= $(RPM)/SPECS
#	SCRIPTS		:= $(PWD)/scripts
#
#	RPMFLAGS = $(RPMBUILD)
#		--define "_topdir	$(RPM)" \
#		--define "_sourcedir	$(SOURCES)" \
#		--define "_builddir	$(BUILD)" \
#		--define "_srcrpmdir	$(SRPMS)" \
#		--define "_rpmdir	$(RPMS)" \
#		--define "_specdir	$(SPECS)" \
#		--define "_kbversion	$(VERSION)" \
#		--define "_kbrelease	$(RELEASE)"
#
# The Makefile also creates the libsparse.tar.gz archive from whatever is
# in the $(KABISRC) directory.
#
###########################################################################

Name:		kabitools
Version:	%{_kbversion}
Release:	%{_kbrelease}%{?dist}
Summary:	A toolkit for KABI navigation
BuildRoot:	%{_topdir}/BUILDROOT/

License:        GPLv2
URL:		https://github.com/camuso/kabiparser
Source0:	{_topdir}/%{name}-%{version}.tar.gz

BuildRequires:	gcc >= 4.8
BuildRequires:	gcc-c++
BuildRequires:	boost
Requires:       boost
Requires:	gcc >= 4.8

%description
kabitools
=========

This kit provides utilities for navigating the kABI

kabi-graph 	- builds the kernel graph and the kernel and kmods, but
		  does not install the kmods or kernel.

kabiscan	- provides a menu-driven wrapper around kabi-lookup,
		  making the tool much easier to use.

kabi-lookup 	- given the symbol name of an exported symbol, determines
		  all the dependencies for that symbol and prints them to
		  the screen.

makei.sh 	- preprocesses kernel c files containing exported symbols
kabi-data.sh	- converts the preprocessed .i files into .kb_dat graphs
kabi-dump 	- utility for examining the contents of a kb_dat graph.

kabitools-rhel-kernel-make.patch
kabitools-fedora-kernel-make.patch
	These are patches for the rhel and fedora kernel make files in
	order to make the kernel graph files while building the kernel.

# The following line disables building of the debug package.
#
%global debug_package %{nil}

%prep
%setup -q -c %{name} -n %{name}

%build
echo $PWD
make %{?_smp_mflags}

%install
mkdir -p $RPM_BUILD_ROOT%{_sbindir}
mkdir -p $RPM_BUILD_ROOT%{_datadir}
cp %{_topdir}/BUILD/%{name}/kabi-parser   $RPM_BUILD_ROOT%{_sbindir}
cp %{_topdir}/BUILD/%{name}/kabi-dump     $RPM_BUILD_ROOT%{_sbindir}
cp %{_topdir}/BUILD/%{name}/kabi-lookup   $RPM_BUILD_ROOT%{_sbindir}
cp %{_topdir}/BUILD/%{name}/kabi-graph    $RPM_BUILD_ROOT%{_sbindir}
cp %{_topdir}/BUILD/%{name}/kabiscan      $RPM_BUILD_ROOT%{_sbindir}
cp %{_topdir}/BUILD/%{name}/kabiscan.help $RPM_BUILD_ROOT%{_sbindir}
cp %{_topdir}/BUILD/%{name}/kabi-data.sh  $RPM_BUILD_ROOT%{_sbindir}
cp %{_topdir}/BUILD/%{name}/makei.sh      $RPM_BUILD_ROOT%{_sbindir}
cp %{_topdir}/BUILD/%{name}/kabitools-rhel7-kernel-make.patch $RPM_BUILD_ROOT%{_datadir}
cp %{_topdir}/BUILD/%{name}/kabitools-rhel8-kernel-make.patch $RPM_BUILD_ROOT%{_datadir}

%files
%defattr(-,root,root)
%{_sbindir}/kabi-parser
%{_sbindir}/kabi-dump
%{_sbindir}/kabi-lookup
%{_sbindir}/kabi-graph
%{_sbindir}/kabiscan
%{_sbindir}/kabiscan.help
%{_sbindir}/kabi-data.sh
%{_sbindir}/makei.sh
%{_datadir}/kabitools-rhel7-kernel-make.patch
%{_datadir}/kabitools-rhel8-kernel-make.patch
%doc README

%changelog
* Fri Aug 11 2023 Tony Camuso <tcamuso@redhat.com> - 3.6.4-3
- Removed multi-arch build, because it's impractical. Too many
  arch-specific libraries and headers to install in the tool
  chain, as well as the cros-compilers.
  Easier to just build on the platform for the platform.
* Thu Aug 03 2023 Tony Camuso <tcamuso@redhat.com> - 3.6.4-2
- Extensive changes to the build to minimize user intervention
  and to liberate it from hard-coded directories.
* Fri Jan 24 2020 Tony Camuso <tcamuso@redhat.com> - 3.6.2-4
- Using latest sparse version 0.6.1-3
* Thu Jan 23 2020 Tony Camuso <tcamuso@redhat.com> - 3.6.2-3
- kabitools-rhel8-kernel-make.patch: sync with latest rhel8
* Mon May 20 2019 Tony Camuso <tcamuso@redhat.com> - 3.6.2-2
- kabi.c: change add_ptr_list_notag to add_ptr_list
- kabitools-rhel-kernel-make.patch: sync with drift in rhel7 make
- rename kabitools-rhel-kernel-make.patch to
  kabitools-rhel7-kernel-make.patch
- remove kabitools-pegas-kernel-make.patch
- add kabitools-rhel8-kernel-make.patch
* Mon May 20 2019 Tony Camuso <tcamuso@redhat.com> - 3.6.2-1
- Allow kabi-graph to include all RHEL
- In kabi.c, MOD_TYPEDEF and MOD_ACCESSED are no longer defined
  in the sparse library, so delete them.
- In kabi.h, typedef for bool emits an error, so it was removed.
  The problem is that kabi.c includes sparse/symbol.h, which
  includes sparse/lib.h, which now includes stdbool.h(C99),
  which defines the bool type. Pull from upstream in sparse
  made this change.
- In the kabitools Makefile, a change was made to specifically
  extract libsparse.a. The rpm build of libsparse.a now includes
  all the header files, so without specifically calling out the
  libsparse static library, all the headers get glommed onto
  the binary lib.
- Version bumps all around.
* Fri Oct 05 2018 Tony Camuso <tcamuso@redhat.com> - 3.6.1-1
- kabi-map.cpp requires explicit include iostream
- Makefile enhancements and fixes
- Bump version numbers
* Mon Jun 19 2017 Tony Camuso <tcamuso@redhat.com> - 3.6.0-2
- aac6b47 README: Update list of files and fix a spelling error
- c171352 kabiscan.help: fix a minor bug
- 5ffe81f README: necessary updates
* Sat Apr 01 2017 Tony Camuso <tcamuso@redhat.com> - 3.6.0-1
- When whitelist flag is set, check the whitelist for the sybmol
  before proceeding.
- Added pegas kernel patch, removed fedora kernal patch.
- Added kabiscan.help script to provide formatted help text
* Fri Mar 31 2017 Tony Camuso <tcamuso@redhat.com> - 3.5.4-6
- Improve usage and error messages
- Correct the script name in the usage string.
- Make use of bold and underline.
- Use an errexit function and define error exit codes.
- Add test for boost.
* Fri Mar 31 2017 Tony Camuso <tcamuso@redhat.com> - 3.5.4-5
- Changes to make kabi-graph more useful and to obscure the
  graph files from git.
* Thu Mar 30 2017 Tony Camuso <tcamuso@redhat.com> - 3.5.4-4
- Disabled building of debug packages.
* Wed Mar 29 2017 Tony Camuso <tcamuso@redhat.com> - 3.5.4-3
- Add build of kabiscan menu-driven wrapper for kabi-lookup.
- Change graph filename type from kb_dat to kbg
* Fri Mar 24 2017 Tony Camuso <tcamuso@redhat.com> - 3.5.4-2
- Fix bug in lookup.cpp::lookup::run() improper use of mask
  m_flags with KB_JUSTONE.
* Tue Mar 21 2017 Tony Camuso <tcamuso@redhat.com> - 3.5.4-1
- Added the -1 option to exit after finding just one symbol.
- Changed the NOTFOUND message to say that the symbol is not
  in the database, so is kABI safe.
* Fri Jan 13 2017 Tony Camuso <tcamuso@redhat.com> - 3.5.3-7
- Modified kabi-graph script to fix bug and test for fedora
  or redhat branch.
* Fri Nov 18 2016 Tony Camuso <tcamuso@redhat.com> - 3.5.3-6
- Adapt for cross-compiling into other arches.
  This means it must be invoked by a make file.
* Mon Nov 14 2016 Tony Camuso <tcamuso@redhat.com> - 3.5.3-5
- Return to build/install
- Updated the description section
- Renamed kabitools.sh -> kabi-graph
* Sat Nov 12 2016 Tony Camuso <tcamuso@redhat.com> - 3.5.3-4
- Changed to install only, instead of build.
- Finished update of README and added README to the %docs directory
- Added the patchfiles to %{_datadir}
- Added kabitools.sh script to build the kernel graph files
* Thu Nov 10 2016 Tony Camuso <tcamuso@redhat.com> - 3.5.3-3
- Refresh kabitools-rhel-kernel-make.patch
- Update the README
* Wed Nov 09 2016 Tony Camuso <tcamuso@redhat.com> - 3.5.3-2
- Update this spec changelog.
- Rename kernel-make.patch kabitools-rhel-kernel-make.patch.
- Add kabitools-fedora-kernel-make.patch
- Moved the Makefile patches to %{datadir} (/usr/share)
* Tue Nov 08 2016 Tony Camuso <tcamuso@redhat.com> - 3.5.3-1
- Update to help text and coment-out calls to cerr.flush
  cerr.flush is unbuffered, so no flush necessary.
* Sun Nov 06 2016 Tony Camuso <tcamuso@redhat.com> - 3.5.2-1
- Add kernel-make.patch for fedora-24
* Thu May 26 2016 Tony Camuso <tcamuso@redhat.com> - 3.5.1-1
- Bump to 3.5.1-1 as first major nvr*
