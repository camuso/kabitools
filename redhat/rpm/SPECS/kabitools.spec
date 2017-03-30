###########################################################################
#
# kabitools.spec
#
# This spec file is intended to be invoked only from a Makefile that has
# correctly defined the directory tree in which this will be built.
# The make defines the following make variables and redefines the rpmbuild
# directory macros to correspond to the build environment.
#
#	RPMBUILD	:= /usr/bin/rpmbuild
#	KABIDIR		:= /work/kabi
#	KABILIB		:= $(KABIDIR)/lib
#	KABISRC		:= $(KABIDIR)/src
#	REDHAT		:= $(KABISRC)/redhat
#	RPM		:= $(REDHAT)/rpm
#	SOURCES 	:= $(RPM)/SOURCES
#	BUILD		:= $(RPM)/BUILD
#	RPMS		:= $(RPM)/RPMS
#	SRPMS		:= $(RPM)/SRPMS
#	SPECS		:= $(RPM)/SPECS
#
#	RPMFLAGS = $(RPMBUILD)
#		--define "_topdir	$(RPM)" \
#		--define "_sourcedir	$(SOURCES)" \
#		--define "_builddir	$(BUILD)" \
#		--define "_srcrpmdir	$(SRPMS)" \
#		--define "_rpmdir	$(RPMS)" \
#		--define "_specdir	$(SPECS)" \
#
# The Makefile also creates the libsparse.tar.gz archive from whatever is
# in the $(KABISRC) directory.
#
###########################################################################

Name:		kabitools
Version:	3.5.4
Release:	3%{?dist}
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
cp %{_topdir}/BUILD/%{name}/kabi-parser  $RPM_BUILD_ROOT%{_sbindir}
cp %{_topdir}/BUILD/%{name}/kabi-dump    $RPM_BUILD_ROOT%{_sbindir}
cp %{_topdir}/BUILD/%{name}/kabi-lookup  $RPM_BUILD_ROOT%{_sbindir}
cp %{_topdir}/BUILD/%{name}/kabi-graph   $RPM_BUILD_ROOT%{_sbindir}
cp %{_topdir}/BUILD/%{name}/kabiscan     $RPM_BUILD_ROOT%{_sbindir}
cp %{_topdir}/BUILD/%{name}/kabi-data.sh $RPM_BUILD_ROOT%{_sbindir}
cp %{_topdir}/BUILD/%{name}/makei.sh     $RPM_BUILD_ROOT%{_sbindir}
cp %{_topdir}/BUILD/%{name}/kabitools-rhel-kernel-make.patch $RPM_BUILD_ROOT%{_datadir}
cp %{_topdir}/BUILD/%{name}/kabitools-fedora-kernel-make.patch $RPM_BUILD_ROOT%{_datadir}

%files
%defattr(-,root,root)
%{_sbindir}/kabi-parser
%{_sbindir}/kabi-dump
%{_sbindir}/kabi-lookup
%{_sbindir}/kabi-graph
%{_sbindir}/kabiscan
%{_sbindir}/kabi-data.sh
%{_sbindir}/makei.sh
%{_datadir}/kabitools-rhel-kernel-make.patch
%{_datadir}/kabitools-fedora-kernel-make.patch
%doc README

%changelog
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
