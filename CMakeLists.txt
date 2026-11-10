cmake_minimum_required(VERSION 3.16)
project(banana-player VERSION 0.6.4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE RelWithDebInfo)
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

# what doesn't need a window: unit-tested on its own
add_library(bpcore STATIC src/aspect.cpp src/tracks.cpp src/format.cpp src/substyle.cpp src/settings.cpp src/resume.cpp src/playlist.cpp src/browser.cpp src/history.cpp src/png.cpp src/battery.cpp src/photo.cpp src/update.cpp src/backlight.cpp src/netstore.cpp src/netpath.cpp src/servers.cpp src/secret.cpp src/sidecar.cpp src/ftplist.cpp src/keyboard.cpp src/eq.cpp src/soundfx.cpp src/playlists.cpp
	src/webauth.cpp src/webpath.cpp src/json.cpp src/webmedia.cpp src/webstate.cpp src/crashlog.cpp src/session.cpp src/durations.cpp src/picsubs.cpp src/manga_names.cpp src/mangastore.cpp src/pagecache.cpp src/strip.cpp src/comicinfo.cpp src/cbz.cpp src/mangalocal.cpp src/manga_ui_logic.cpp src/paged.cpp src/dlqueue.cpp src/mangarepo.cpp src/opds.cpp src/cbzwrite.cpp src/mangaservers.cpp)
target_include_directories(bpcore PUBLIC src)
target_link_libraries(bpcore PUBLIC PkgConfig::MPV PkgConfig::ZLIB OpenSSL::Crypto ${CMAKE_DL_LIBS})
if(APPLE)
	target_link_libraries(bpcore PUBLIC iconv)	# glibc has iconv in libc; macOS has it apart
endif()

# libavformat (FFmpeg: libmpv's own dependency) for the file lists' durations; the web dashboard's probe uses it too
pkg_check_modules(AV REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil libswscale)
add_library(bpprobe STATIC src/durprobe.cpp src/pagedecode.cpp)
target_link_libraries(bpprobe PUBLIC bpcore PkgConfig::AV)

add_executable(banana-player src/main.cpp src/player.cpp src/ui.cpp src/ui_draw.cpp src/ui_menus.cpp src/ui_browse.cpp src/ui_forms.cpp src/ui_playlists.cpp src/ui_settings.cpp src/ui_main.cpp src/ui_sound.cpp src/ui_web.cpp src/ui_home.cpp src/ui_ext.cpp src/input.cpp src/sleepwatch.cpp src/touch.cpp src/panel.cpp src/levelwatch.cpp src/updater.cpp src/volume.cpp src/networker.cpp src/netstream.cpp src/netresolve.cpp src/discovery.cpp src/session_host.cpp src/durworker.cpp src/coverworker.cpp src/pagerender.cpp src/reader_proto.cpp src/reader.cpp src/ui_manga.cpp)
target_link_libraries(banana-player PRIVATE bpcore bpprobe PkgConfig::SDL2 PkgConfig::SDL2_TTF)
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
	pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl)
	target_sources(banana-player PRIVATE src/net.cpp src/net_smb.cpp src/net_ftp.cpp src/mangafetch.cpp)
	target_link_libraries(banana-player PRIVATE PkgConfig::SMB2 PkgConfig::CURL)
else()
	target_sources(banana-player PRIVATE src/net_none.cpp src/mangafetch_none.cpp)
endif()
# the commit in crash.log's records (empty without git, as in a source tarball)
execute_process(COMMAND git -c safe.directory=* describe --always --dirty --exclude=*
	WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} OUTPUT_VARIABLE BP_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
target_compile_definitions(banana-player PRIVATE
	BP_VERSION="${PROJECT_VERSION}"
	BP_COMMIT="${BP_COMMIT}"
	BP_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets")
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
		src/web/server.cpp src/web/netif.cpp src/web/api_files.cpp src/web/api_player.cpp src/web/api_playlists.cpp
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
