#!/bin/bash

# Specific variables for X11 and Fedora Kinoite
X11_FLAG=""
KINOITE_MODE=0

# Parsing arguments
for arg in "$@"; do
    if [[ "$arg" == "--x11" ]]; then
        X11_FLAG="-DBBDX_X11=ON"
    elif [[ "$arg" == "--kinoite" ]]; then
        KINOITE_MODE=1
    elif [[ "$arg" == "-h" || "$arg" == "--help" ]]; then
        echo "Usage: $0 [OPTIONS]"
        echo "Options:"
        echo "  --x11        Build for KWin X11 instead of the Wayland default"
        echo "  --kinoite    Build an RPM package for Fedora Kinoite"
        echo "  -h | --help  Displays this message"
        exit 0
    else
        echo "Unknown parameter passed: $arg"
        exit 1
    fi
done

# Common building part of both regular and fedora kinoite versions
rm -fr build
mkdir -p build
cd build
cmake .. -DCMAKE_INSTALL_PREFIX=/usr $X11_FLAG
make -j$(nproc)

# Handle installation or packaging based on the mode
if [[ $KINOITE_MODE -eq 1 ]]; then
    cpack -V -G RPM

    echo "To finish installation on Fedora Kinoite, exit your container and run:"
    echo "sudo rpm-ostree install kwin-effects-better-blur-dx/build/kwin-better-blur-dx.rpm"
else
    sudo make install
fi
