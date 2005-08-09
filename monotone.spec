Summary: monotone is a distributed version control tool
Name: monotone
Version: 0.22
Release: 1
License: GPL
Group: Development/Tools
URL: http://www.venge.net/monotone
Source0: %{name}-%{version}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-buildroot
BuildRequires: boost-devel >= 1.32.0
Requires: boost >= 1.32.0

%description
monotone is a free, distributed version control system. It provides
fully disconnected operation, manages complete tree versions, keeps
its state in a local transactional database, supports overlapping
branches and extensible metadata, exchanges work over plain network
protocols, performs history-sensitive merging, and delegates trust
functions to client-side RSA certificates.

%prep
%setup -q

%build
CFLAGS="$RPM_OPT_FLAGS" \
CXXFLAGS="$RPM_OPT_FLAGS" \
./configure --prefix=$RPM_BUILD_ROOT/usr \
            --infodir=$RPM_BUILD_ROOT%{_infodir} \
            --mandir=$RPM_BUILD_ROOT%{_mandir} \
            --with-bundled-sqlite \
            --with-bundled-lua
make

%install
rm -rf $RPM_BUILD_ROOT
make install
rm -f $RPM_BUILD_ROOT%{_infodir}/dir
# remove x permission in contrib to avoid messing the dependencies
chmod -x contrib/*

%clean
rm -rf $RPM_BUILD_ROOT

%post
if [ -x /sbin/install-info ] 
then
/sbin/install-info --info-dir=%{_infodir} \
	     %{_infodir}/monotone.info.gz
fi

%preun
if [ -x /sbin/install-info ]
then
/sbin/install-info --info-dir=%{_infodir} --remove monotone
fi

%files
%defattr(-,root,root,-)
%doc AUTHORS COPYING NEWS README README.changesets UPGRADE contrib
%{_bindir}/monotone
%{_mandir}/man1/monotone.1.gz
%{_infodir}/*.info*.gz


%changelog
* Mon Aug  8 2005 nathaniel smith <njs@pobox.com>
- 0.22 release

* Sun Jul 17 2005 nathaniel smith <njs@pobox.com>
- 0.21 release

* Tue Jul 5 2005 nathaniel smith <njs@pobox.com>
- 0.20 release

* Tue May 3 2005 nathaniel smith <njs@pobox.com>
- 0.19 release

* Sun Apr 10 2005 nathaniel smith <njs@pobox.com>
- 0.18 release

* Fri Mar 3 2005 nathaniel smith <njs@pobox.com>
- 0.17 release

* Thu Dec 30 2004 graydon hoare <graydon@pobox.com>
- 0.16 release

* Sun Nov 7 2004 graydon hoare <graydon@pobox.com>
- 0.15 release

* Sun Aug 2 2004 graydon hoare <graydon@pobox.com>
- 0.14 release

* Thu May 20 2004 graydon hoare <graydon@pobox.com>
- 0.13 release

* Sun May 2 2004 graydon hoare <graydon@pobox.com>
- 0.12 release

* Mon Mar 29 2004 graydon hoare <graydon@pobox.com>
- 0.11 release

* Mon Mar 1 2004 graydon hoare <graydon@pobox.com>
- 0.10 release

* Thu Jan 8 2004 graydon hoare <graydon@pobox.com>
- don't install /usr/share/info/dir

* Thu Jan 8 2004 graydon hoare <graydon@pobox.com>
- 0.9 release

* Fri Nov 21 2003 graydon hoare <graydon@pobox.com>
- 0.8 release

* Mon Nov 3 2003 graydon hoare <graydon@pobox.com>
- 0.7 release

* Sat Oct 18 2003 graydon hoare <graydon@pobox.com>
- 0.6 release
- set CFLAGS/CXXFLAGS since RH compiler can do optimization
- remove info details since texi has category / entry

* Wed Sep 27 2003 graydon hoare <graydon@pobox.com> 
- 0.5 release.

* Wed Sep 24 2003 graydon hoare <graydon@pobox.com> 
- Initial build.


