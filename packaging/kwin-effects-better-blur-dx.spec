# SPDX-License-Identifier: GPL-3.0-or-later
#
# Fedora/RPM spec for kwin-effects-better-blur-dx
#
# Build (Wayland, default):
#   rpmbuild -tb kwin-effects-better-blur-dx-2.5.1.tar.gz
# or:
#   rpmbuild -bs kwin-effects-better-blur-dx.spec   # source RPM only
#
# The upstream project defaults to the KWin Wayland plugin, matching the
# Fedora Kinoite / Plasma Wayland setup. Pass -DBBDX_X11=ON via RPM_OPT_FLAGS
# style extra defines if you need the X11 plugin paths instead.

Name:           kwin-effects-better-blur-dx
Version:        2.5.1
Release:        1%{?dist}
Summary:        Fork of the KWin Blur effect for KDE Plasma 6 with force blur and more

License:        GPL-3.0-or-later
URL:            https://github.com/xarblu/kwin-effects-better-blur-dx
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  cmake
BuildRequires:  extra-cmake-modules
BuildRequires:  gcc-c++
BuildRequires:  gettext
BuildRequires:  kf6-ki18n-devel
BuildRequires:  kf6-kcmutils-devel
BuildRequires:  kf6-kconfigwidgets-devel
BuildRequires:  kf6-kcoreaddons-devel
BuildRequires:  kf6-kguiaddons-devel
BuildRequires:  kf6-kwindowsystem-devel
BuildRequires:  kwin-devel
BuildRequires:  kdecoration-devel
BuildRequires:  libepoxy-devel
BuildRequires:  libplasma-devel
BuildRequires:  plasma-workspace-devel
BuildRequires:  qt6-qtbase-devel
BuildRequires:  qt6-qtbase-private-devel
BuildRequires:  wayland-devel
BuildRequires:  libdrm-devel
BuildRequires:  pkgconfig(xcb)

Requires:       kwin
Requires:       kf6-ki18n

# Plugin ABI follows the KWin/Plasma major series.
Requires:       kwin%{?_isa} >= 6.4

%description
Better Blur DX is a fork of kwin-better-blur, itself a fork of KDE's default
KWin blur effect. It adds force-blur for arbitrary window classes, optional
window decoration blurring, menu and dock blurring, refraction effects,
rounded-corner caching and more performance tuning options for KDE Plasma 6
on Wayland (and X11).

The effect and its configuration module are translated through the KDE
ki18n/gettext framework. This release ships complete translations for
Simplified Chinese (zh_CN), Traditional Chinese (zh_TW), Japanese (ja),
German (de), French (fr) and Korean (ko).

%prep
%autosetup -n %{name}-%{version}

%build
%cmake
%cmake_build

%install
%cmake_install
# Collect the kwin_better_blur_dx.mo catalogs installed by ki18n_install(po)
%find_lang kwin_better_blur_dx

%files -f kwin_better_blur_dx.lang
%license LICENSE
%doc README.md
%{_qt6_plugindir}/kwin/effects/plugins/better_blur_dx.so
%{_qt6_plugindir}/kwin/effects/configs/kwin_better_blur_dx_config.so

%changelog
* Sat Aug 29 2026 Better Blur DX Packaging <i18n@example.invalid> - 2.5.1-1
- Initial RPM release
- Add ki18n/gettext based multi-language support
- Ship translations for zh_CN, zh_TW, ja, de, fr and ko
- Localize effect metadata and the KCM user interface
