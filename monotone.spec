Summary: monotone is a distributed version control tool
Name: monotone
Version: 0.37
Release: 0.mtn.1%{?dist}
License: GPL
Group: Development/Tools
URL: http://www.monotone.ca/
Source0: %{name}-%{version}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-buildroot
BuildRequires: boost-devel >= 1.33.0, texinfo, zlib-devel
Requires: boost >= 1.33.0

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
%configure
make %{?_smp_mflags}

%install
rm -rf %{buildroot}
make install DESTDIR=%{buildroot}
rm -f $RPM_BUILD_ROOT%{_infodir}/dir
# remove x permission in contrib to avoid messing the dependencies
chmod -x contrib/*
# clean up contrib
rm -Rf contrib/.deps
# let RPM copy this file
rm -f %{buildroot}%{_datadir}/doc/monotone/monotone.html

%clean
rm -rf %{buildroot}

%post
if [ -x /sbin/install-info ]; then
  /sbin/install-info --info-dir=%{_infodir} %{_infodir}/monotone.info.gz || :
fi
 
%postun
if [ $1 -eq 0 -a -x /sbin/install-info ]; then
  /sbin/install-info --info-dir=%{_infodir} --remove monotone || :
fi

%files
%defattr(-,root,root,-)
%doc AUTHORS COPYING NEWS README README.changesets UPGRADE monotone.html contrib
%{_bindir}/mtn
%{_infodir}/*.info*.gz
%{_datadir}/locale/*/LC_MESSAGES/monotone.mo


%changelog
* Mon Nov  5 2007 Julio M. Merino Vidal <jmmv@NetBSD.org>
- Fixed build of RPM package in Fedora by discarding info/dir.

* Fri Oct 26 2007 Richard Levitte <richard@levitte.org>
- 0.37 release.

* Fri Aug  3 2007 Richard Levitte <richard@levitte.org>
- 0.36 release.

* Mon May  7 2007 Richard Levitte <richard@levitte.org>
- 0.35 release.

* Sun Apr  1 2007 Richard Levitte <richard@levitte.org>
- 0.34 release

* Thu Mar 01 2007 Thomas Keller <me@thomaskeller.biz>
- removed reference to no longer shipped manpage

* Wed Feb 28 2007 Richard Levitte <richard@levitte.org>
- 0.33 release

* Wed Dec 27 2006 Richard Levitte <richard@levitte.org>
- 0.32 release

* Fri Nov 10 2006 nathaniel smith <njs@pobox.com>
- 0.31 release

* Sun Sep 17 2006 nathaniel smith <njs@pobox.com>
- 0.30 release

* Sun Aug 20 2006 nathaniel smith <njs@pobox.com>
- 0.29 release

* Fri Jul 21 2006 nathaniel smith <njs@pobox.com>
- 0.28 release

* Sat Jun 17 2006 nathaniel smith <njs@pobox.com>
- 0.27 release

* Sat Apr 8 2006 nathaniel smith <njs@pobox.com>
- 0.26 release

* Wed Mar 29 2006 nathaniel smith <njs@pobox.com>
- 0.26pre3 release

* Sat Feb 11 2006 nathaniel smith <njs@pobox.com>
- 0.26pre2 release

* Thu Jan 8 2006 nathaniel smith <njs@pobox.com>
- 0.26pre1 release

* Thu Dec 29 2005 nathaniel smith <njs@pobox.com>
- 0.25 release

* Sat Nov 26 2005 nathaniel smith <njs@pobox.com>
- 0.24 release

* Thu Sep 29 2005 nathaniel smith <njs@pobox.com>
- 0.23 release

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
