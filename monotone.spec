Summary: monotone is a distributed version control tool
Name: monotone
Version: 0.5
Release: 1
License: GPL
Group: Development/Version Control
URL: http://www.venge.net/monotone
Source0: %{name}-%{version}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-buildroot

%description
monotone is a free, distributed version control system. it provides
fully disconnected operation, manages complete tree versions, keeps
its state in a local transactional database, supports overlapping
branches and extensible metadata, exchanges work over plain network
protocols, performs history-sensitive merging, and delegates trust
functions to client-side RSA certificates.

%prep
%setup -q

%build
./configure --prefix=$RPM_BUILD_ROOT/usr \
            --infodir=$RPM_BUILD_ROOT%{_infodir} \
            --mandir=$RPM_BUILD_ROOT%{_mandir}
make

%install
rm -rf $RPM_BUILD_ROOT
make install

%clean
rm -rf $RPM_BUILD_ROOT

%post
if [ -x /sbin/install-info ] 
then
/sbin/install-info --info-dir=%{_infodir} \
             --entry="* monotone: (monotone).              Monotone version control system" \
             --section=Programming \
	     %{_infodir}/monotone.info.gz
fi

%preun
if [ -x /sbin/install-info ]
then
/sbin/install-info --info-dir=%{_infodir} --remove monotone
fi

%files
%defattr(-,root,root,-)
%doc AUTHORS COPYING NEWS README
%{_bindir}/monotone
%{_bindir}/depot.cgi
%{_mandir}/man1/monotone.1.gz
%{_infodir}/*


%changelog
* Wed Sep 27 2003 graydon hoare <graydon@pobox.com> 
- 0.5 release.

* Wed Sep 24 2003 graydon hoare <graydon@pobox.com> 
- Initial build.


