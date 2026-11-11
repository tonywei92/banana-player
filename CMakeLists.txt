cmake_minimum_required(VERSION 3.16)
project(banana-player VERSION 0.7.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

option(BP_SANITIZE "Build every target with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if(BP_SANITIZE)
	add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
	add_link_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
endif()

# The handheld has OpenGL ES 2 (Mesa Panfrost) through SDL2's KMSDRM; a desktop has desktop GL.
if(APPLE)
	set(BP_GLES_DEFAULT OFF)
else()
	set(BP_GLES_DEFAULT ON)
endif()
option(BP_GLES "Ask SDL for an OpenGL ES 2 context" ${BP_GLES_DEFAULT})

find_package(PkgConfig REQUIRED)
pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
pkg_check_modules(SDL2_TTF REQUIRED IMPORTED_TARGET SDL2_ttf)
pkg_check_modules(ZLIB REQUIRED IMPORTED_TARGET zlib)

# libcrypto seals the saved passwords; Homebrew's openssl is keg-only, so look where it keeps it
if(APPLE AND NOT OPENSSL_ROOT_DIR)
	foreach(d /opt/homebrew/opt/openssl@3 /usr/local/opt/openssl@3)
		if(EXISTS ${d})
			set(OPENSSL_ROOT_DIR ${d})
			break()
		endif()
	endforeach()
endif()
find_package(OpenSSL REQUIRED)

# lexbor: the vendored HTML parser and CSS selectors (third_party/README.md) behind bp.html. Built as its own CMake
# builds it statically: only the vendored modules, C99, LEXBOR_STATIC, position independent, its warnings not ours.
enable_language(C)
file(GLOB_RECURSE LEXBOR_SOURCES CONFIGURE_DEPENDS
	third_party/lexbor/source/lexbor/core/*.c
	third_party/lexbor/source/lexbor/css/*.c
	third_party/lexbor/source/lexbor/dom/*.c
	third_party/lexbor/source/lexbor/html/*.c
	third_party/lexbor/source/lexbor/ns/*.c
	third_party/lexbor/source/lexbor/selectors/*.c
	third_party/lexbor/source/lexbor/tag/*.c)
# lexbor picks one port per platform; only posix is ever built here (windows_nt kept for completeness, never tested)
if(WIN32)
	file(GLOB_RECURSE LEXBOR_PORT_SOURCES CONFIGURE_DEPENDS third_party/lexbor/source/lexbor/ports/windows_nt/*.c)
else()
	file(GLOB_RECURSE LEXBOR_PORT_SOURCES CONFIGURE_DEPENDS third_party/lexbor/source/lexbor/ports/posix/*.c)
endif()
list(APPEND LEXBOR_SOURCES ${LEXBOR_PORT_SOURCES})
add_library(lexbor_static STATIC ${LEXBOR_SOURCES})
target_include_directories(lexbor_static PUBLIC third_party/lexbor/source)
set_target_properties(lexbor_static PROPERTIES C_STANDARD 99 C_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
target_compile_definitions(lexbor_static PRIVATE LEXBOR_STATIC)	# core/def.h: this makes LXB_API plain, no dll machinery
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	target_compile_definitions(lexbor_static PRIVATE _POSIX_C_SOURCE=199309L)	# from the vendored ports/posix/config.cmake
endif()
if(NOT MSVC)
	target_compile_options(lexbor_static PRIVATE -w)
endif()
if(UNIX)
	target_link_libraries(lexbor_static PRIVATE m)	# ceil() in core/dtoa.c; its config.cmake looks for libm
endif()

# what doesn't need a window: unit-tested on its own
add_library(bpcore STATIC src/aspect.cpp src/tracks.cpp src/challenge.cpp src/format.cpp src/substyle.cpp src/settings.cpp src/resume.cpp src/playlist.cpp src/browser.cpp src/history.cpp src/png.cpp src/battery.cpp src/photo.cpp src/update.cpp src/backlight.cpp src/netstore.cpp src/netpath.cpp src/servers.cpp src/secret.cpp src/sidecar.cpp src/ftplist.cpp src/keyboard.cpp src/eq.cpp src/soundfx.cpp src/playlists.cpp
	src/webauth.cpp src/webpath.cpp src/json.cpp src/webmedia.cpp src/webstate.cpp src/crashlog.cpp src/session.cpp src/durations.cpp src/picsubs.cpp src/manga_names.cpp src/mangafilters.cpp src/mangastore.cpp src/pagecache.cpp src/strip.cpp src/comicinfo.cpp src/cbz.cpp src/mangalocal.cpp src/manganetfolders.cpp src/manga_ui_logic.cpp src/paged.cpp src/dlqueue.cpp src/mangarepo.cpp src/opds.cpp src/cbzwrite.cpp src/mangaservers.cpp src/htmldoc.cpp src/mangaremote.cpp src/readerfetchplan.cpp src/suwayomi.cpp src/mangajava.cpp src/doh.cpp src/tarx.cpp src/mangaseriescache.cpp src/imagesize.cpp src/bands.cpp)
target_include_directories(bpcore PUBLIC src)
target_link_libraries(bpcore PUBLIC PkgConfig::MPV PkgConfig::ZLIB OpenSSL::Crypto ${CMAKE_DL_LIBS})
target_link_libraries(bpcore PRIVATE lexbor_static)	# src/htmldoc.h hides lexbor, so nothing downstream includes it
if(APPLE)
	target_link_libraries(bpcore PUBLIC iconv)	# glibc has iconv in libc; macOS has it apart
endif()

# Lua 5.4 for the manga reader's user-added source scripts (docs/design/manga.md 5.2). The vendored tree is the
# library only (no lua.c/luac.c), warning-free as it came, with the platform Lua's makefile would pick.
file(GLOB LUA_SRC CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/third_party/lua/src/*.c)
add_library(lua_static STATIC ${LUA_SRC})
set_target_properties(lua_static PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)
target_include_directories(lua_static PUBLIC third_party/lua/src)
target_compile_options(lua_static PRIVATE -w)
if(APPLE)
	target_compile_definitions(lua_static PUBLIC LUA_USE_MACOSX)
else()
	target_compile_definitions(lua_static PUBLIC LUA_USE_POSIX)
endif()

# the source-script host: no window, no networking of its own
add_library(bpmanga STATIC src/mangasource.cpp src/mangasource.h)
target_include_directories(bpmanga PUBLIC src)
target_link_libraries(bpmanga PUBLIC bpcore PRIVATE lua_static)

# libavformat (FFmpeg: libmpv's own dependency) for the file lists' durations; the web dashboard's probe uses it too
pkg_check_modules(AV REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil libswscale)
add_library(bpprobe STATIC src/durprobe.cpp src/pagedecode.cpp)
target_link_libraries(bpprobe PUBLIC bpcore PkgConfig::AV)
# libjpeg (libjpeg-turbo's jpeg_skip_scanlines) and libpng: a long strip page's bands, decoded without the whole
# picture in memory (pagedecode.cpp's decodeBand); FFmpeg's decoders only give whole frames
pkg_check_modules(JPEG REQUIRED IMPORTED_TARGET libjpeg)
pkg_check_modules(PNG REQUIRED IMPORTED_TARGET libpng)
target_link_libraries(bpprobe PRIVATE PkgConfig::JPEG PkgConfig::PNG)
# (included as <libpng16/png.h>: src/png.h, this project's own PNG writer, would answer <png.h>)
target_include_directories(bpprobe PRIVATE ${PNG_INCLUDEDIR}/..)

add_executable(banana-player src/main.cpp src/player.cpp src/ui.cpp src/ui_draw.cpp src/ui_menus.cpp src/ui_browse.cpp src/ui_forms.cpp src/ui_playlists.cpp src/ui_settings.cpp src/ui_main.cpp src/ui_sound.cpp src/ui_web.cpp src/ui_home.cpp src/ui_ext.cpp src/input.cpp src/sleepwatch.cpp src/touch.cpp src/panel.cpp src/levelwatch.cpp src/updater.cpp src/volume.cpp src/networker.cpp src/netstream.cpp src/netresolve.cpp src/discovery.cpp src/session_host.cpp src/durworker.cpp src/coverworker.cpp src/pagerender.cpp src/reader_proto.cpp src/reader.cpp src/readerfetch.cpp src/manganet.cpp src/javaext.cpp src/ui_manga.cpp src/mangarepos.cpp src/mangadownloader.cpp src/ui_manga_settings.cpp src/ui_manga_sources.cpp src/ui_manga_browse.cpp src/ui_manga_filters.cpp src/ui_manga_java.cpp src/ui_manga_servers.cpp src/ui_manga_downloads.cpp src/ui_manga_netfolders.cpp src/ui_manga_web.cpp)
# libcurl: the Java add-on's downloads (BananaOS's own, with OpenSSL: not bundled), and the network folders' FTP and
# the manga reader's images when BP_NET is on
pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl)
find_package(Threads REQUIRED)	# the Java host's install and process threads
target_link_libraries(banana-player PRIVATE bpcore bpprobe PkgConfig::SDL2 PkgConfig::SDL2_TTF PkgConfig::CURL Threads::Threads)
# SMB through libsmb2 (bundled on the handheld; on a Mac, built into .cache/mac/libsmb2). Without it the browser's
# Network tab says no server can be reached
if(APPLE AND EXISTS "${CMAKE_SOURCE_DIR}/.cache/mac/libsmb2/lib/pkgconfig")
	set(ENV{PKG_CONFIG_PATH} "${CMAKE_SOURCE_DIR}/.cache/mac/libsmb2/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
endif()
pkg_check_modules(SMB2 IMPORTED_TARGET libsmb2)
option(BP_NET "SMB/FTP network folders (libsmb2)" ${SMB2_FOUND})
if(BP_NET)
	pkg_check_modules(SMB2 REQUIRED IMPORTED_TARGET libsmb2)
	# FTP and the manga reader's images through libcurl (BananaOS's own, with OpenSSL: not bundled)
	target_sources(banana-player PRIVATE src/net.cpp src/net_smb.cpp src/net_ftp.cpp src/mangafetch.cpp
		src/mangaworker.cpp)
	# bpmanga: the Lua source host the worker runs calls in (docs/design/manga.md 5.2, 7)
	target_link_libraries(banana-player PRIVATE PkgConfig::SMB2 PkgConfig::CURL bpmanga)
else()
	target_sources(banana-player PRIVATE src/net_none.cpp src/mangafetch_none.cpp src/mangaworker_none.cpp)
endif()
# the commit in crash.log's records (empty without git, as in a source tarball)
execute_process(COMMAND git -c safe.directory=* describe --always --dirty --exclude=*
	WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} OUTPUT_VARIABLE BP_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
target_compile_definitions(banana-player PRIVATE
	BP_VERSION="${PROJECT_VERSION}"
	BP_COMMIT="${BP_COMMIT}"
	BP_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
	BP_MANGA_FETCH=1)	# mangarepos.cpp's adapter for MangaFetch; repos-test has no fetcher to link
if(BP_GLES)
	target_compile_definitions(banana-player PRIVATE BP_GLES=1)
endif()

# the web dashboard: a server in the player (cpp-httplib, split into httplib.h/.cc), QR codes (nayuki), libavformat
# for the file probe, and the web app compiled in (cmake/EmbedWeb.cmake writes web_assets.cpp from web/)
option(BP_WEB "web dashboard" ON)
if(BP_WEB)
	find_package(Threads REQUIRED)
	file(GLOB_RECURSE BP_WEB_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/web/*)
	add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/web_assets.cpp
		COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR} -DOUT=${CMAKE_BINARY_DIR}/web_assets.cpp
			-P ${CMAKE_SOURCE_DIR}/cmake/EmbedWeb.cmake
		DEPENDS ${BP_WEB_FILES} ${CMAKE_SOURCE_DIR}/cmake/EmbedWeb.cmake
			${CMAKE_SOURCE_DIR}/assets/fonts/PixelifySans-Regular.ttf ${CMAKE_SOURCE_DIR}/assets/fonts/Inter-Regular.ttf ${CMAKE_SOURCE_DIR}/assets/fonts/Inter-Bold.ttf
			${CMAKE_SOURCE_DIR}/assets/fonts/licenses/PixelifySans-OFL.txt ${CMAKE_SOURCE_DIR}/assets/fonts/licenses/Inter-LICENSE.txt
		COMMENT "Embedding the web dashboard"
		VERBATIM)
	target_sources(banana-player PRIVATE
		third_party/httplib/httplib.cc third_party/qrcodegen/qrcodegen.cpp
		src/web/server.cpp src/web/netif.cpp src/web/api_files.cpp src/web/api_player.cpp src/web/api_playlists.cpp src/web/api_manga.cpp
		src/web/probe.cpp src/web/subs.cpp src/web/picsubs.cpp src/web/remux.cpp ${CMAKE_BINARY_DIR}/web_assets.cpp)
	target_include_directories(banana-player PRIVATE third_party/httplib third_party/qrcodegen)
	# both split files of httplib read these: the keep-alive check every second, and form bodies up to 256 KB
	target_compile_definitions(banana-player PRIVATE BP_WEB=1
		CPPHTTPLIB_KEEPALIVE_TIMEOUT_CHECK_INTERVAL_USECOND=1000000
		CPPHTTPLIB_FORM_URL_ENCODED_PAYLOAD_MAX_LENGTH=262144)
	target_link_libraries(banana-player PRIVATE PkgConfig::AV Threads::Threads)
endif()

option(BP_TOOLS "Build the command-line tools (bp-discover)" ON)
if(BP_TOOLS)
	find_package(Threads REQUIRED)
	add_executable(bp-discover tools/bp-discover.cpp src/discovery.cpp)
	target_link_libraries(bp-discover PRIVATE Threads::Threads)
	add_executable(bp-servers tools/bp-servers.cpp)
	target_link_libraries(bp-servers PRIVATE bpcore)
	if(BP_NET)
		add_executable(bp-net tools/bp-net.cpp src/net.cpp src/net_smb.cpp src/net_ftp.cpp)
		target_link_libraries(bp-net PRIVATE bpcore Threads::Threads PkgConfig::SMB2 PkgConfig::CURL)
	endif()
endif()

enable_testing()
add_executable(core-test tests/core_test.cpp)
target_link_libraries(core-test PRIVATE bpcore bpprobe)
target_compile_definitions(core-test PRIVATE BP_TESTS_DIR="${CMAKE_SOURCE_DIR}/tests")
add_test(NAME core COMMAND core-test)

add_executable(manga-test tests/manga_test.cpp)
target_link_libraries(manga-test PRIVATE bpmanga)
add_test(NAME manga COMMAND manga-test)
# The harness's shell doesn't run ./build/manga-test directly; this target runs it through cmake.
add_custom_target(manga-check COMMAND $<TARGET_FILE:manga-test> DEPENDS manga-test)
add_executable(fuzz-test tests/fuzz_test.cpp)
target_link_libraries(fuzz-test PRIVATE bpcore bpprobe)
target_compile_definitions(fuzz-test PRIVATE BP_FUZZ_DIR="${CMAKE_SOURCE_DIR}/tests/pages")
# the repository manager, driven by a fake fetcher: no thread, no socket, no Lua host (design 5.3)
add_executable(repos-test tests/repos_test.cpp src/mangarepos.cpp)
target_link_libraries(repos-test PRIVATE bpcore PkgConfig::SDL2)
add_test(NAME repos COMMAND repos-test)
# The download engine with a fake fetcher (canned pages, no thread and no server); SDL is only for mangafetch.h
add_executable(downloader-test tests/downloader_test.cpp src/mangadownloader.cpp)
target_link_libraries(downloader-test PRIVATE bpcore PkgConfig::SDL2)
add_test(NAME downloader COMMAND downloader-test)

# The manga worker (design 7): a real thread over the Lua host, with fixture scripts that need no network. Only built
# when libcurl is there (the source's HttpFn is a real easy handle); the no-network build's stub has nothing to test.
pkg_check_modules(WORKER_CURL QUIET IMPORTED_TARGET libcurl)
if(WORKER_CURL_FOUND)
	find_package(Threads REQUIRED)
	add_executable(worker-test tests/worker_test.cpp src/mangaworker.cpp src/net_none.cpp)
	target_link_libraries(worker-test PRIVATE bpmanga PkgConfig::SDL2 PkgConfig::WORKER_CURL Threads::Threads)
	add_test(NAME worker COMMAND worker-test)
	# The harness's shell doesn't run ./build/worker-test directly; this target runs it through cmake.
	add_custom_target(worker-check COMMAND $<TARGET_FILE:worker-test> DEPENDS worker-test)
endif()
