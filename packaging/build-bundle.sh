#!/bin/bash
# Builds Banana Player for the RG DS / RG DS Plus (aarch64) as a folder to copy onto the card, in an arm64 Debian 12
# container: Debian 12's glibc (2.36) is older than BananaOS's, so what's built here loads there.
#
#   packaging/build-bundle.sh            (on the host: runs itself in Docker)
#   packaging/build-bundle.sh --quick    (for the handheld: no zip, no shelf picture)
#   packaging/build-bundle.sh --clean    (everything from scratch: the libraries and the player)
#
# Fast again and again: the build tools are baked into a Docker image made once (bp-builder, tagged by the package
# list below), and the libraries are installed into .cache/bundle/prefix, kept between runs (built again only when a
# version below changes, or with --clean); the player builds incrementally in .cache/bundle/bp-build.
#
# Out: dist/ports/BananaPlayer/ (bin, lib, share) and dist/ports/Banana Player.sh, ready for roms/ports on the card;
# dist/BananaPlayer-<version>-aarch64.zip with both; dist/symbols/banana-player-<version>, the player unstripped (for crash.log).
#
# Bundled, built from source with only what playing needs: FFmpeg (decoders and demuxers), libplacebo, libass and
# libmpv, and the few libraries they pull in; libsmb2 (network folders over SMB). Taken from BananaOS instead (not
# bundled): glibc, libstdc++, SDL2, SDL2_ttf, Mesa (EGL/GLES), libdrm, ALSA, FreeType, zlib, libpng, OpenSSL's
# libcrypto (saved passwords) and libcurl (FTP): its SDL carries BananaOS's own patches (SDL_KMSDRM_FIRST picks the
# top panel), its Mesa drives the Mali GPU, and its libcurl and OpenSSL get its security updates.
set -euo pipefail

FFMPEG_VERSION=7.1.1
LIBPLACEBO_VERSION=v7.349.0
LIBASS_VERSION=0.17.3
MPV_VERSION=v0.40.0
LIBSMB2_VERSION=6.2
# FFmpeg's configure beyond the version: the mp4 muxer is the web dashboard's Direct Stream (src/web/remux.cpp)
FFMPEG_CONFIG=mp4mux

HERE="$(cd "$(dirname "$0")/.." && pwd)"

# the build tools, once: an image with them installed (its tag changes with the list, so a new package makes a new one)
read -r -d '' BUILDER <<'DOCKERFILE' || true
FROM debian:bookworm
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update -qq && apt-get install -y -qq --no-install-recommends \
	build-essential pkg-config cmake meson ninja-build nasm git ca-certificates curl xz-utils zip patchelf file \
	python3 python3-jinja2 python3-pip \
	libdav1d-dev libfreetype-dev libfribidi-dev libharfbuzz-dev libfontconfig-dev zlib1g-dev libasound2-dev \
	libsdl2-dev libsdl2-ttf-dev libegl-dev libgles-dev libdrm-dev libssl-dev libcurl4-openssl-dev >/dev/null \
	&& rm -rf /var/lib/apt/lists/*
# mpv wants Meson 1.3 or newer; Debian 12 has 1.0
RUN pip3 install -q --break-system-packages 'meson>=1.3' 2>&1 | grep -v "^WARNING" || true
DOCKERFILE

if [ "${1:-}" != "--inside" ]; then
	mkdir -p "$HERE/dist" "$HERE/.cache/bundle"
	TAG="bp-builder:$(printf '%s' "$BUILDER" | shasum -a 256 | cut -c1-12)"
	if ! docker image inspect "$TAG" >/dev/null 2>&1; then
		echo "== the build tools image ($TAG), once"
		printf '%s\n' "$BUILDER" | docker build --platform linux/arm64 -q -t "$TAG" - >/dev/null
	fi
	exec docker run --rm --platform linux/arm64 \
		-v "$HERE":/src -v "$HERE/.cache/bundle":/cache \
		"$TAG" bash /src/packaging/build-bundle.sh --inside "$@"
fi
shift
QUICK=0; CLEAN=0
for a in "$@"; do case "$a" in --quick) QUICK=1 ;; --clean) CLEAN=1 ;; esac; done

# ---- inside the container ----------------------------------------------------------------------------------------
# the libraries' install, kept in the cache: built again only when a version changes (or --clean)
PREFIX=/cache/prefix
export PKG_CONFIG_PATH=$PREFIX/lib/pkgconfig
export LD_LIBRARY_PATH=$PREFIX/lib
SRC=/cache/src
STAMP="$FFMPEG_VERSION+$FFMPEG_CONFIG $LIBPLACEBO_VERSION $LIBASS_VERSION $MPV_VERSION $LIBSMB2_VERSION"
if [ "$CLEAN" = 1 ] || [ "$(cat $PREFIX/.versions 2>/dev/null)" != "$STAMP" ]; then
	echo "== the libraries from scratch"
	# (libplacebo and mpv are git checkouts under unversioned names: a new version needs a new clone; FFmpeg's and
	# libass's trees were configured for the old prefix)
	rm -rf $PREFIX $SRC/libplacebo $SRC/mpv /cache/bp-build
	(cd $SRC/ffmpeg-$FFMPEG_VERSION 2>/dev/null && make distclean >/dev/null 2>&1) || true
fi
mkdir -p $SRC $PREFIX
JOBS=$(nproc)

fetch() { # url dir
	[ -d "$SRC/$2" ] || { mkdir -p "$SRC/$2"; curl -sL "$1" | tar -x --strip-components=1 -C "$SRC/$2" -f - ${3:-}; }
}

echo "== FFmpeg $FFMPEG_VERSION"
fetch https://ffmpeg.org/releases/ffmpeg-$FFMPEG_VERSION.tar.xz ffmpeg-$FFMPEG_VERSION -J
cd $SRC/ffmpeg-$FFMPEG_VERSION
if [ ! -f $PREFIX/lib/libavcodec.so ]; then
	./configure --prefix=$PREFIX --enable-shared --disable-static --disable-programs --disable-doc \
		--disable-encoders --disable-muxers --enable-muxer=mp4 --disable-indevs --disable-outdevs --disable-network \
		--disable-debug --enable-libdav1d --enable-zlib --enable-pic >/dev/null
	make -j$JOBS >/dev/null && make install >/dev/null
fi

echo "== libplacebo $LIBPLACEBO_VERSION"
[ -d $SRC/libplacebo ] || git -c advice.detachedHead=false clone -q --depth 1 --branch $LIBPLACEBO_VERSION --recurse-submodules --shallow-submodules \
	https://code.videolan.org/videolan/libplacebo.git $SRC/libplacebo
if [ ! -f $PREFIX/lib/pkgconfig/libplacebo.pc ]; then
	cd $SRC/libplacebo
	rm -rf build
	meson setup build --prefix=$PREFIX --libdir=lib --buildtype=release -Dvulkan=disabled -Dopengl=enabled \
		-Dglslang=disabled -Dshaderc=disabled -Dlcms=disabled -Ddovi=disabled -Dlibdovi=disabled -Ddemos=false \
		-Dtests=false >/dev/null
	ninja -C build >/dev/null && ninja -C build install >/dev/null
fi

echo "== libass $LIBASS_VERSION"
fetch https://github.com/libass/libass/releases/download/$LIBASS_VERSION/libass-$LIBASS_VERSION.tar.xz libass-$LIBASS_VERSION -J
# with fontconfig: the one way libass falls back, character by character, to the font that has it (Korean, Thai,
# Arabic... in subtitles and titles). The bundled fonts.conf lists only the player's own fonts. (A libass from before
# that, without it, is built again, and mpv on it.)
if [ ! -f $PREFIX/lib/.libass-fontconfig ]; then
	rm -f $PREFIX/lib/libass.* $PREFIX/lib/libmpv.*
	cd $SRC/libass-$LIBASS_VERSION
	make distclean >/dev/null 2>&1 || true
	./configure --prefix=$PREFIX --disable-static --enable-fontconfig >/dev/null
	make -j$JOBS >/dev/null && make install >/dev/null && touch $PREFIX/lib/.libass-fontconfig
fi

echo "== mpv $MPV_VERSION"
[ -d $SRC/mpv ] || git -c advice.detachedHead=false clone -q --depth 1 --branch $MPV_VERSION https://github.com/mpv-player/mpv.git $SRC/mpv
if [ ! -f $PREFIX/lib/libmpv.so ]; then
	cd $SRC/mpv
	rm -rf build
	# libmpv only, drawn through its render API (plain-gl: no window system), sound to ALSA
	meson setup build --prefix=$PREFIX --libdir=lib --buildtype=release -Dlibmpv=true -Dcplayer=false \
		-Dauto_features=disabled -Dgl=enabled -Dplain-gl=enabled -Dalsa=enabled -Dzlib=enabled -Diconv=enabled \
		-Dlibavdevice=enabled 2>&1 | tail -40
	ninja -C build >/dev/null && ninja -C build install >/dev/null
fi

echo "== libsmb2 $LIBSMB2_VERSION"
fetch https://github.com/sahlberg/libsmb2/archive/refs/tags/libsmb2-$LIBSMB2_VERSION.tar.gz libsmb2-$LIBSMB2_VERSION -z
if [ ! -f $PREFIX/lib/libsmb2.so ]; then
	cd $SRC/libsmb2-$LIBSMB2_VERSION
	rm -rf build
	# its examples don't build with -Werror on this compiler; no Kerberos (NTLM is what home servers use)
	cmake -S . -B build -DCMAKE_INSTALL_PREFIX=$PREFIX -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_BUILD_TYPE=Release \
		-DBUILD_SHARED_LIBS=ON -DENABLE_EXAMPLES=OFF -DENABLE_LIBKRB5=OFF -DENABLE_GSSAPI=OFF >/dev/null
	cmake --build build -j$JOBS >/dev/null && cmake --install build >/dev/null
fi
echo "$STAMP" > $PREFIX/.versions

echo "== banana-player"
# incremental, in the cache: a one-line change rebuilds one file
BPB=/cache/bp-build
# BP_WEB: libavformat from the FFmpeg above (its .pc files are in $PREFIX/lib/pkgconfig, on PKG_CONFIG_PATH)
# -g: the copy kept in dist/symbols/ maps crash.log's frames to lines (the bundle's own is stripped)
cmake -S /src -B $BPB -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS=-g -DBP_GLES=ON -DBP_NET=ON -DBP_WEB=ON -DBP_TOOLS=OFF \
	-DCMAKE_INSTALL_RPATH='$ORIGIN/../lib' -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON >/dev/null
# a failed compile stops here: before, the grep hid it and the last good binary went into the bundle unnoticed
if ! cmake --build $BPB -j$JOBS > $BPB/build.log 2>&1; then
	grep -E "error|FAILED" $BPB/build.log | head -40
	echo "build failed: the bundle is not made" >&2
	exit 1
fi
grep -E "warning" $BPB/build.log || true
$BPB/core-test

echo "== bundle"
VERSION=$(grep -oP 'project\(banana-player VERSION \K[0-9.]+' /src/CMakeLists.txt)
OUT=/src/dist/ports
B="$OUT/BananaPlayer"
rm -rf "$OUT" && mkdir -p "$B/bin" "$B/lib" "$B/share/banana-player"
cp $BPB/banana-player "$B/bin/"
cp -r /src/assets/fonts "$B/share/banana-player/"
# fontconfig's view of the fonts: the player's folder only (the launcher sets FONTCONFIG_FILE), its cache in
# $XDG_CACHE_HOME (run-port: /data/home/.cache)
cat > "$B/share/banana-player/fonts.conf" <<'CONF'
<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "urn:fontconfig:fonts.dtd">
<fontconfig>
	<dir prefix="relative">fonts</dir>
	<cachedir prefix="xdg">fontconfig</cachedir>
</fontconfig>
CONF
cp /src/LICENSE /src/README.md "$B/"
# licences of what's compiled in: the web dashboard's libraries (cpp-httplib, qrcodegen, htm, Sortable) and the fonts it serves
L="$B/share/banana-player/licenses"
mkdir -p "$L"
cp /src/third_party/httplib/LICENSE "$L/cpp-httplib-LICENSE.txt"
cp /src/third_party/qrcodegen/LICENSE "$L/qrcodegen-LICENSE.txt"
cp /src/web/vendor/LICENSE-htm.txt "$L/htm-LICENSE.txt"
cp /src/web/vendor/LICENSE-Sortable.txt "$L/Sortable-LICENSE.txt"
# JASSUB (the browser's ASS subtitles): its wrapper's licence, and its WebAssembly build's (libass, FreeType, HarfBuzz,
# FriBidi (LGPL-2.1+), ...); served as separate files, so they can be replaced
cp /src/web/vendor/jassub/LICENSE.txt "$L/jassub-LICENSE.txt"
cp /src/web/vendor/jassub/COPYRIGHT.txt "$L/jassub-COPYRIGHT.txt"
cp /src/assets/fonts/licenses/PixelifySans-OFL.txt "$L/Pixelify-Sans-OFL.txt"
cp /src/assets/fonts/licenses/Inter-LICENSE.txt "$L/Inter-LICENSE.txt"
# the launcher (the Store's menu.launch, and what the Ports shelf's script runs), the version the Store reads
# (package.version_file) and the tile's picture (menu.media.image)
cp /src/packaging/launch.sh "$B/launch.sh"
# Secure DNS for the Java extensions (java/bp-dns, built from source by its build.sh, small): the add-on loads it
mkdir -p "$B/java" && cp /src/packaging/java/bp-dns.jar "$B/java/"
echo "$VERSION" > "$B/VERSION"
# the tile's picture: an experiment's PNG when there is one (packaging/icon/app-icon-experiment.png: delete it to go
# back), else the generated pixel art
if [ -f /src/packaging/icon/app-icon-experiment.png ]; then cp /src/packaging/icon/app-icon-experiment.png "$B/share/banana-player/icon.png"
else python3 /src/packaging/icon/make-icon.py "$B/share/banana-player/icon.png" 16; fi

# what the player loads that BananaOS doesn't have: libmpv and everything it needs, minus the system's own
SYSTEM='^(linux-vdso|ld-linux|libc\.|libm\.|libdl\.|libpthread\.|librt\.|libresolv\.|libstdc\+\+|libgcc_s|libSDL2|libasound|libfreetype|libz\.|libpng|libdrm|libEGL|libGLES|libGL\.|libgbm|libGLdispatch|libGLX|libX|libxcb|libwayland|libbz2|libbrotli|libcrypto|libssl|libcurl)'
# a file's own NEEDED entries (not ldd's whole tree: a system library's needs are the system's), with where each is
needed() {
	local f=$1
	readelf -d "$f" | awk -F'[][]' '/NEEDED/ {print $2}' | while read -r name; do
		echo "$name $(ldd "$f" | awk -v n="$name" '$1 == n {print $3}')"
	done
}
declare -A seen
queue=("$B/bin/banana-player")
while [ ${#queue[@]} -gt 0 ]; do
	f=${queue[0]}; queue=("${queue[@]:1}")
	while read -r name path; do
		[[ "$name" =~ $SYSTEM ]] && continue
		[ -n "${seen[$name]:-}" ] && continue
		seen[$name]=1
		cp -L "$path" "$B/lib/$name"
		queue+=("$B/lib/$name")
	done < <(needed "$f")
done
# each bundled library finds its neighbours first
for l in "$B"/lib/*.so*; do patchelf --set-rpath '$ORIGIN' "$l"; done
# the unstripped player, outside the zip: for addr2line on a crash.log (DEVELOPMENT.md, "Crash log")
mkdir -p /src/dist/symbols && cp "$B/bin/banana-player" "/src/dist/symbols/banana-player-$VERSION"
strip --strip-unneeded "$B/bin/banana-player" "$B"/lib/*.so* 2>/dev/null || true

cp /src/packaging/banana-player-port.sh "$OUT/Banana Player.sh"
chmod +x "$OUT/Banana Player.sh" "$B/launch.sh" "$B/bin/banana-player"
if [ "$QUICK" = 1 ]; then du -sh "$B"; echo "done (quick): dist/ports/BananaPlayer"; exit 0; fi
# the shelf's picture: BananaOS looks for media/box/<entry's name>.png beside the entries
mkdir -p "$OUT/media/box"
if [ -f /src/packaging/icon/app-icon-experiment.png ]; then cp /src/packaging/icon/app-icon-experiment.png "$OUT/media/box/Banana Player.png"
else python3 /src/packaging/icon/make-icon.py "$OUT/media/box/Banana Player.png" 16; fi

echo "bundled libraries:"; ls "$B/lib" | sed 's/^/  /'
du -sh "$B"
cd "$OUT" && rm -f /src/dist/BananaPlayer-$VERSION-aarch64.zip && zip -qr /src/dist/BananaPlayer-$VERSION-aarch64.zip "Banana Player.sh" BananaPlayer media
echo "done: dist/ports/ and dist/BananaPlayer-$VERSION-aarch64.zip"
