# Better Blur DX
Better Blur DX is a loose continuation of [Better Blur](https://github.com/taj-ny/kwin-effects-forceblur/) - a fork of the Plasma 6 blur effect with additional features and bug fixes.

![image](https://github.com/user-attachments/assets/f8a7c618-89b4-485a-b0f8-29dd5f77e3ca)

### Features
- X11 and Wayland support
- Force blur
- Adjustable blur brightness, contrast and saturation
- Adjustable corner radius

You may notice these are *less* features than the original Better Blur.
*This is intentional* - this project focusses on bringing the KWin blur to arbitrary windows
and nothing else.

If you expect to see more features you should check out other projects - or feel free to fork
and create your own.

### Bug fixes
Fixes for blur-related Plasma bugs that haven't been patched yet.

- Blur may sometimes disappear during animations
- [Transparent color schemes don't work properly with the Breeze application style](https://github.com/taj-ny/kwin-effects-better_blur_dx/pull/38)

### Support for previous Plasma releases
Better Blur DX should always work on the current stable version of Plasma.
Older versions aren't tested much and may or may not work.

Currently supported versions: **6.5**

# Installation
> [!IMPORTANT]
> If the effect stops working after a system upgrade, you will need to rebuild it or reinstall the package.
> The effect only works for the *exact KWin* version it was built for.

## Packages
<details>
  <summary>Arch Linux AUR (maintained by https://github.com/D3SOX)</summary>
  <br>

  Tagged releases:

  ```sh
  yay -S kwin-effects-better-blur-dx
  ```
  
  `-git` package tracking the `main` branch:

  ```sh
  yay -S kwin-effects-better-blur-dx-git
  ```

  (Or use your AUR helper of choice instead of `yay`)
</details>

<details>
  <summary>Gentoo (maintained by me - https://github.com/xarblu)</summary>
  <br>

  ```sh
  eselect repository enable xarblu-overlay
  emerge --sync xarblu-overlay
  emerge --ask kde-misc/kwin-effects-better-blur-dx
  ```
</details>

<details>
  <summary>Fedora COPR (maintained by https://github.com/Infinality)</summary>
  <br>

  Details: https://copr.fedorainfracloud.org/coprs/infinality/kwin-effects-better-blur-dx/

  ```sh
  dnf copr enable infinality/kwin-effects-better-blur-dx
  dnf install kwin-effects-better-blur-dx
  dnf install kwin-effects-better-blur-dx-x11
  ```
</details>

<details>
  <summary>NixOS (flakes)</summary>
  <br>

  ``flake.nix``:
  ```nix
  {
    inputs = {
      nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";

      kwin-effects-better-blur-dx = {
        url = "github:xarblu/kwin-effects-better-blur-dx";
        inputs.nixpkgs.follows = "nixpkgs";
      };
    };
  }
  ```

  ```nix
  { inputs, pkgs, ... }:

  {
    environment.systemPackages = [
      inputs.kwin-effects-better-blur-dx.packages.${pkgs.system}.default # Wayland
      inputs.kwin-effects-better-blur-dx.packages.${pkgs.system}.x11 # X11
    ];
  }
  ```
</details>

## Manual
> [!NOTE]
> On Fedora Kinoite and other distributions based on it, the effect must be built in a container.

### Dependencies
- CMake
- Extra CMake Modules
- Plasma 6
- Qt 6
- KF6
- KWin development packages

<details>
  <summary>Arch Linux</summary>
  <br>

  Wayland:
  ```
  sudo pacman -S base-devel git extra-cmake-modules qt6-tools kwin
  ```
  
  X11:
  ```
  sudo pacman -S base-devel git extra-cmake-modules qt6-tools kwin-x11
  ```
</details>

<details>
  <summary>Debian-based (KDE Neon, Kubuntu, Ubuntu)</summary>
  <br>

  Wayland:
  ```
  sudo apt install -y git cmake g++ extra-cmake-modules qt6-tools-dev kwin-dev libkf6configwidgets-dev gettext libkf6crash-dev libkf6globalaccel-dev libkf6kio-dev libkf6service-dev libkf6notifications-dev libkf6kcmutils-dev libkdecorations3-dev libxcb-composite0-dev libxcb-randr0-dev libxcb-shm0-dev
  ```
  
  X11:
  ```
  sudo apt install -y git cmake g++ extra-cmake-modules qt6-tools-dev kwin-x11-dev libkf6configwidgets-dev gettext libkf6crash-dev libkf6globalaccel-dev libkf6kio-dev libkf6service-dev libkf6notifications-dev libkf6kcmutils-dev libkdecorations3-dev libxcb-composite0-dev libxcb-randr0-dev libxcb-shm0-dev
  ```
</details>

<details>
  <summary>Fedora 41, 42</summary>
  <br>

  Wayland:
  ```
  sudo dnf -y install git cmake extra-cmake-modules gcc-g++ kf6-kwindowsystem-devel plasma-workspace-devel libplasma-devel qt6-qtbase-private-devel qt6-qtbase-devel cmake kwin-devel extra-cmake-modules kwin-devel kf6-knotifications-devel kf6-kio-devel kf6-kcrash-devel kf6-ki18n-devel kf6-kguiaddons-devel libepoxy-devel kf6-kglobalaccel-devel kf6-kcmutils-devel kf6-kconfigwidgets-devel kf6-kdeclarative-devel kdecoration-devel kf6-kglobalaccel kf6-kdeclarative libplasma kf6-kio qt6-qtbase kf6-kguiaddons kf6-ki18n wayland-devel libdrm-devel
  ```
  
  X11:
  ```
  sudo dnf -y install git cmake extra-cmake-modules gcc-g++ kf6-kwindowsystem-devel plasma-workspace-devel libplasma-devel qt6-qtbase-private-devel qt6-qtbase-devel cmake extra-cmake-modules kf6-knotifications-devel kf6-kio-devel kf6-kcrash-devel kf6-ki18n-devel kf6-kguiaddons-devel libepoxy-devel kf6-kglobalaccel-devel kf6-kcmutils-devel kf6-kconfigwidgets-devel kf6-kdeclarative-devel kdecoration-devel kf6-kglobalaccel kf6-kdeclarative libplasma kf6-kio qt6-qtbase kf6-kguiaddons kf6-ki18n wayland-devel libdrm-devel kwin-x11-devel
  ```
</details>

<details>
  <summary>openSUSE</summary>
  <br>

  Wayland:
  ```
  sudo zypper in -y git cmake-full gcc-c++ kf6-extra-cmake-modules kcoreaddons-devel kguiaddons-devel kconfigwidgets-devel kwindowsystem-devel ki18n-devel kiconthemes-devel kpackage-devel frameworkintegration-devel kcmutils-devel kirigami2-devel "cmake(KF6Config)" "cmake(KF6CoreAddons)" "cmake(KF6FrameworkIntegration)" "cmake(KF6GuiAddons)" "cmake(KF6I18n)" "cmake(KF6KCMUtils)" "cmake(KF6KirigamiPlatform)" "cmake(KF6WindowSystem)" "cmake(Qt6Core)" "cmake(Qt6DBus)" "cmake(Qt6Quick)" "cmake(Qt6Svg)" "cmake(Qt6Widgets)" "cmake(Qt6Xml)" "cmake(Qt6UiTools)" "cmake(KF6Crash)" "cmake(KF6GlobalAccel)" "cmake(KF6KIO)" "cmake(KF6Service)" "cmake(KF6Notifications)" libepoxy-devel kwin6-devel
  ```
  
  X11:
  ```
  sudo zypper in -y git cmake-full gcc-c++ kf6-extra-cmake-modules kcoreaddons-devel kguiaddons-devel kconfigwidgets-devel kwindowsystem-devel ki18n-devel kiconthemes-devel kpackage-devel frameworkintegration-devel kcmutils-devel kirigami2-devel "cmake(KF6Config)" "cmake(KF6CoreAddons)" "cmake(KF6FrameworkIntegration)" "cmake(KF6GuiAddons)" "cmake(KF6I18n)" "cmake(KF6KCMUtils)" "cmake(KF6KirigamiPlatform)" "cmake(KF6WindowSystem)" "cmake(Qt6Core)" "cmake(Qt6DBus)" "cmake(Qt6Quick)" "cmake(Qt6Svg)" "cmake(Qt6Widgets)" "cmake(Qt6Xml)" "cmake(Qt6UiTools)" "cmake(KF6Crash)" "cmake(KF6GlobalAccel)" "cmake(KF6KIO)" "cmake(KF6Service)" "cmake(KF6Notifications)" libepoxy-devel kwin6-x11-devel
  ```
</details>

### Building
```sh
git clone https://github.com/xarblu/kwin-effects-better-blur-dx
cd kwin-effects-better-blur-dx
mkdir build
cd build
cmake .. -DCMAKE_INSTALL_PREFIX=/usr
make -j$(nproc)
sudo make install
```

By default this will build the effect for the regular (Wayland) KWin.
To build a version for KWin X11 add `-DBETTERBLUR_X11=ON` to the `cmake` invocation.

<details>
  <summary>Building on Fedora Kinoite</summary>
  <br>

  ```sh
  # enter container
  git clone https://github.com/xarblu/kwin-effects-better-blur-dx
  cd kwin-effects-better-blur-dx
  mkdir build
  cd build
  cmake .. -DCMAKE_INSTALL_PREFIX=/usr
  make -j$(nproc)
  cpack -V -G RPM
  exit # exit container
  sudo rpm-ostree install kwin-effects-better-blur-dx/build/kwin-better-blur-dx.rpm
  ```
</details>

**Remove the *build* directory when rebuilding the effect.**

# Usage
This effect conflicts with the default KWin blur effect (and other effects replacing it).

1. Install the plugin.
2. Open the *Desktop Effects* page in *System Settings*.
3. Disable any blur effects.
4. Enable the *Better Blur DX* effect.

### Window transparency
The window needs to be translucent in order for the blur to be visible. This can be done in multiple ways:
- Use a transparent theme for the program if it supports it
- Use a transparent color scheme, such as [Alpha](https://store.kde.org/p/1972214)
- Create a window rule that reduces the window opacity

### Obtaining window classes
The classes of windows to blur can be specified in the effect settings. You can obtain them in two ways:
  - Run ``qdbus org.kde.KWin /KWin org.kde.KWin.queryWindowInfo`` and click on the window. You can use either *resourceClass* or *resourceName*.
  - Right click on the titlebar, go to *More Options* and *Configure Special Window/Application Settings*. The class can be found at *Window class (application)*. If there is a space, for example *Navigator firefox*, you can use either *Navigator* or *firefox*.

# Known Issues
## Incompatibility with other effects
This effect has some compatibility issues with some other effects.

- "Blur" from KWin - Because we effectively replace the KWin blur you shouldn't use both in parallel. Some windows might get double blurred and "look off" if you do.
- "Wobbly Windows" from KWin - We're blurring a square region behind the window. Either that square will bleed out of the deformed window or KWin will skip the blur entirely while the "wobble effect" is active.

## High cursor latency or stuttering on Wayland
This effect can be very resource-intensive if you have a lot of windows open. On Wayland, high GPU load may result in higher cursor latency or even stuttering. If that bothers you, set the following environment variable: ``KWIN_DRM_NO_AMS=1``. If that's not enough, try enabling or disabling the software cursor by also setting ``KWIN_FORCE_SW_CURSOR=0`` or ``KWIN_FORCE_SW_CURSOR=1``.

Intel GPUs use software cursor by default due to [this bug](https://gitlab.freedesktop.org/drm/intel/-/issues/9571), however it doesn't seem to affect all GPUs.

# Credits
- [a-parhom/LightlyShaders](https://github.com/a-parhom/LightlyShaders) - CMakeLists.txt files
- [taj-ny/kwin-effects-forceblur](https://github.com/taj-ny/kwin-effects-forceblur) - The original Better Blur
