#!/bin/sh
# Build txthighlight. One translation unit, no build system.
#
#   ./build.sh              optimised build -> ./txthighlight
#   ./build.sh --install    build, then install to $PREFIX/bin (Termux) or
#                           ~/.local/bin, whichever is on PATH
#   ./build.sh --debug      -O0 -g build with ASan/UBSan
#   ./build.sh --clean      remove the binary
#
# Uses $CXX, defaulting to `c++`: g++ on Linux distros, clang++ on Termux and macOS.
# Requires a C++20 compiler and the raster-image development packages:
#   Debian/Ubuntu  sudo apt install libpng-dev libjpeg-dev libwebp-dev libgif-dev
#   macOS          brew install libpng jpeg-turbo webp giflib
#   Fedora         sudo dnf install libpng-devel libjpeg-turbo-devel libwebp-devel giflib-devel
#   Arch           sudo pacman -S libpng libjpeg-turbo libwebp giflib
#   Termux         pkg install libpng libjpeg-turbo libwebp giflib
# Every C++ library used here is header-only and vendored under vendor/.

set -eu

cd "$(dirname "$0")"

BINARY=txthighlight
SOURCE=main.cpp
CXX=${CXX:-c++}

# Unified flags: no -I/-L beyond the vendored headers, no toolchain probing.
CXXFLAGS="-std=c++20 -Ivendor -Wall -Wextra"
LINK_LIBS="-pthread -lpng -ljpeg -lwebp -lgif"

case "${1:-}" in
    --debug)
        OPT_FLAGS="-O0 -g"
        EXTRA_FLAGS="-fsanitize=address,undefined"
        ;;
    --install)
        INSTALL=1
        OPT_FLAGS="-O2 -DNDEBUG"
        EXTRA_FLAGS=""
        ;;
    --clean)
        rm -f "$BINARY" "$BINARY".o
        echo "cleaned"
        exit 0
        ;;
    "" | --release)
        OPT_FLAGS="-O2 -DNDEBUG"
        EXTRA_FLAGS=""
        ;;
    -h | --help)
        # Print the leading comment block (line 2 up to the first blank line).
        sed -n '2,/^$/p' "$0" | sed 's/^# \{0,1\}//' | sed -e :a -e '/^\n*$/{$d;N;ba' -e '}'
        exit 0
        ;;
    *)
        echo "unknown option: $1" >&2
        echo "usage: $0 [--install|--debug|--release|--clean]" >&2
        exit 2
        ;;
esac

set -x
"$CXX" $CXXFLAGS $OPT_FLAGS $EXTRA_FLAGS "$SOURCE" -o "$BINARY" $LINK_LIBS

{ set +x; } 2>/dev/null   # stop tracing before the summary lines

if [ "${INSTALL:-0}" = "1" ]; then
    BIN_DIR="${PREFIX:-$HOME/.local}/bin"
    mkdir -p "$BIN_DIR"
    install -m 755 "$BINARY" "$BIN_DIR/txthighlight"
    echo "installed $BIN_DIR/txthighlight"
    case ":$PATH:" in
        *":$BIN_DIR:"*) ;;
        *) echo "note: $BIN_DIR is not in PATH" >&2 ;;
    esac
fi
