Name: @PACKAGE@
Summary: Autoresponder for qmail
Version: @VERSION@
Release: 1
Copyright: GPL
Group: Utilities/System
Source: http://em.ca/~bruceg/@PACKAGE@/%{version}/@PACKAGE@-%{version}.tar.gz
BuildRoot: %{_tmpdir}/@PACKAGE@-root
URL: http://em.ca/~bruceg/@PACKAGE@/
Packager: Bruce Guenter <bruceg@em.ca>

%description
This package contains a program to provide automatic rate limited email
responses from qmail.

%prep
%setup

%build
echo gcc "%{optflags}" >conf-cc
echo gcc -s "%{optflags}" >conf-ld
make

%install
rm -fr %{buildroot}
mkdir -p %{buildroot}%{_bindir}
mkdir -p %{buildroot}%{_mandir}/man1
echo %{buildroot}%{_bindir} >conf-bin
echo %{buildroot}%{_mandir} >conf-man
rm -f installer.o installer instcheck
make installer instcheck
./installer
./instcheck

%clean
rm -rf %{buildroot}

%files
%defattr(-,root,root)
%doc COPYING README procedure.txt
%{_bindir}/*
%{_mandir}/man*/*

