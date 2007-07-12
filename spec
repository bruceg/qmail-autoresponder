Name: @PACKAGE@
Summary: Autoresponder for qmail
Version: @VERSION@
Release: 1
License: GPL
Group: Utilities/System
Source: http://untroubled.org/@PACKAGE@/archive/@PACKAGE@-%{version}.tar.gz
BuildRequires: bglibs >= 1.022
BuildRoot: %{_tmppath}/@PACKAGE@-root
URL: http://untroubled.org/@PACKAGE@/
Packager: Bruce Guenter <bruce@untroubled.org>

%description
This package contains a program to provide automatic rate limited email
responses from qmail.

%package mysql
Summary: MySQL-based Autoresponder for qmail
Group: Utilities/System
%description mysql
This package contains a program to provide automatic rate limited email
responses from qmail, based entirely on a MySQL database.

%prep
%setup

%build
echo %{_bindir} >conf-bin
echo %{_mandir} >conf-man
echo gcc "%{optflags}" >conf-cc
echo gcc -s "%{optflags}" >conf-ld

make all mysql

%install
rm -fr %{buildroot}
mkdir -p %{buildroot}%{_bindir}
mkdir -p %{buildroot}%{_mandir}/man1
make install install_prefix=%{buildroot}

%clean
rm -rf %{buildroot}

%files
%defattr(-,root,root)
%doc COPYING README procedure.txt
%{_bindir}/qmail-autoresponder
%{_mandir}/man*/*

%files mysql
%{_bindir}/qmail-autoresponder-mysql
