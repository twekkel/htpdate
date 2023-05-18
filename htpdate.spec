%global debug_package %{nil}

Name:           htpdate
Version:        1.3.7
Release:        1
Summary:        Htpdate will synchronize your computer's time by extracting timestamps from HTTP headers.

License:       GPL 
URL:           http://www.vervest.org/htp
Source0:       htpdate-%{version}.tar.gz

BuildRequires:  make gcc openssl-devel systemd-rpm-macros
Requires:      openssl-libs 

%description
The HTTP Time Protocol (HTP) is used to synchronize a computer's time
with web servers as reference time source. Htpdate will synchronize your
computer's time by extracting timestamps from HTTP headers found
in web server responses. Htpdate can be used as a daemon, to keep your
computer synchronized.

%prep
%autosetup


%build
%make_build https


%install
%make_install
install -D -m 644 scripts/htpdate.service %{buildroot}/%{_unitdir}/htpdate.service
mkdir -p %{buildroot}/%{_sysconfdir}/default
sed -e "s/Environment=\(.*\)/#\1/p" -e d scripts/htpdate.service > %{buildroot}/%{_sysconfdir}/default/htpdate

%clean
rm -rf %{buildroot}

%post
systemctl daemon-reload

%preun
systemctl --no-reload disable htpdate.service
systemctl stop htpdate.service

%files
%{_sbindir}/htpdate
%{_mandir}/man8/htpdate.8.gz
%{_unitdir}/htpdate.service
%{_sysconfdir}/default/htpdate
%license LICENSE
%doc Changelog README.md



%changelog
* Tue May 16 2023 Shiro Hara <white@vx-xv.com>
- htpdate-1.3.7
