Name: @PACKAGE@
Summary: Autoresponder for qmail
Version: @VERSION@
Release: 1
Copyright: GPL
Group: Utilities/System
Source: http://em.ca/~bruceg/@PACKAGE@/%{version}/@PACKAGE@-%{version}.tar.gz
BuildRoot: %{_tmppath}/@PACKAGE@-root
URL: http://em.ca/~bruceg/@PACKAGE@/
Packager: Bruce Guenter <bruceg@em.ca>

%description
This package contains a program to provide automatic rate limited email
responses from qmail.

%prep
%setup

%build
echo gcc "%{optflags}" -I/usr/local/bglibs/include >conf-cc
echo gcc -s "%{optflags}" -L/usr/local/bglibs/lib >conf-ld
make

%install
rm -fr %{buildroot}
mkdir -p %{buildroot}%{_bindir}
mkdir -p %{buildroot}%{_mandir}/man1
echo %{buildroot}%{_bindir} >conf-bin
echo %{buildroot}%{_mandir} >conf-man
rm -f conf_bin.c conf_man.c insthier.o installer instcheck
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

