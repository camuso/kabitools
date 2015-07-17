Name:           kabitools
Version:        3.0
Release:        1%{?dist}
Summary:        A toolkit for KABI navigation
BuildRoot:	%{_topdir}/BUILDROOT/

License:        GPLv2
URL:            https://github.com/camuso/kabiparser
Source0:        %{_topdir}/%{name}-%{version}.tar.gz

BuildArch:	x86_64
BuildRequires:  gcc >= 4.8
BuildRequires:	gcc-c++
BuildRequires:	boost
Requires:       boost

%description
kabitools provides utilities for navigating the KABI
makei.sh - preprocesses kernel c files containing exported symbols
kabi-data.sh - converts the preprocessed .i files into .kb_dat graphs
kabi-dump - utility for examining the contents of a kb_dat graph.
kabi-lookup - given the symbol name of an exported symbol, determines
              all the dependencies for that symbol and prints them to
              the screen.

%prep
%autosetup

%build
make %{?_smp_mflags}

%install
cp %{_topdir}/BUILD/%{name}-%{version}/kabi-parser  $RPM_BUILD_ROOT/.
cp %{_topdir}/BUILD/%{name}-%{version}/kabi-dump    $RPM_BUILD_ROOT/.
cp %{_topdir}/BUILD/%{name}-%{version}/kabi-lookup  $RPM_BUILD_ROOT/.
cp %{_topdir}/BUILD/%{name}-%{version}/kabi-data.sh $RPM_BUILD_ROOT/.
cp %{_topdir}/BUILD/%{name}-%{version}/makei.sh     $RPM_BUILD_ROOT/.

%files
%defattr(-,root,root)
/kabi-parser
/kabi-dump
/kabi-lookup
/kabi-data.sh
/makei.sh
%doc

%changelog

