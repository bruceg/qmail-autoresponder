Name: @PACKAGE@
Summary: Autoresponder for qmail
Version: @VERSION@
Release: 1
Copyright: GPL
Group: Utilities/System
Source: http://em.ca/~bruceg/@PACKAGE@/%{version}/@PACKAGE@-%{version}.tar.gz
BuildRoot: /tmp/@PACKAGE@-root
URL: http://em.ca/~bruceg/@PACKAGE@/
Packager: Bruce Guenter <bruceg@em.ca>

%description
This package contains a program to provide automatic rate limited email
responses from qmail.

%prep
%setup

%build
make CFLAGS="$RPM_OPT_FLAGS" LDFLAGS="$RPM_OPT_FLAGS -s"

%install
rm -fr $RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT
make install_prefix=$RPM_BUILD_ROOT install

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root)
%doc COPYING README procedure.txt
/usr/bin/*
