Summary: A static library made in motor IDE
Name: connwrap
Version: 0.1
Release: 1
Copyright: GPL
Group: Development/Libraries
URL: http://konst.org.ua/motor/
Packager: Konstantin Klyagin
Source: %{name}-%{version}.tar.gz
BuildRoot: /var/tmp/%{name}-buildroot/

%description
Motor is a text mode based programming environment for Linux. It
consists of a powerful editor with syntax highlight feature, project
manager, makefile generator, gdb front-end, etc. Deep CVS integration is
also provided.

%prep
%setup

%build
./configure --prefix=/usr
make

%install
rm -rf $RPM_BUILD_ROOT
make prefix=$RPM_BUILD_ROOT/usr sysconfdir=$RPM_BUILD_ROOT/etc install

find $RPM_BUILD_ROOT/usr/ -type f -print | \
    grep -v '\/(README|COPYING|INSTALL|TODO|ChangeLog)$' | \
    sed "s@^$RPM_BUILD_ROOT@@g" | \
    sed 's/^\(.\+\/man.\+\)$/\1*/g' \
    > %{name}-%{version}-filelist

%clean
rm -rf $RPM_BUILD_ROOT

%files -f %{name}-%{version}-filelist
%defattr(-, root, root)

%doc README COPYING INSTALL TODO ChangeLog

%changelog
