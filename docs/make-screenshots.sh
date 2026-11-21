#!/bin/bash
# The README's pictures: scripted scenes of the desktop build on a demo library, both panels captured (--press's
# shot:NAME) and stacked like the handheld's, in docs/img/. Each scene has its own state folder and theme (the
# README shows dark and light ones side by side: a scene's theme is the `theme =` line of its settings).
#   docs/make-screenshots.sh DEMO_DIR
# DEMO_DIR (made by hand, generated or CC BY media only; see docs/workflow.md):
#   Movies/Big Buck Bunny (trailer).mkv  the trailer (download.blender.org/peach/trailer/trailer_480p.mov, © Blender
#                                        Foundation, CC BY 3.0) remuxed with 6 chapters (Opening 0:00, The meadow 0:04,
#                                        Big Buck 0:09, Three rodents 0:13, Payback 0:17, Credits 0:24) and an English
#                                        SRT track (written for it, lines at 0:09, 0:13, 0:16, 0:19, 0:24)
#   Movies/Meadow morning.mp4, Movies/Apples and acorns.mp4   two cuts of the trailer
#   Music/Night Drive/, Music/Paper Boats/   generated tones with cover.jpg (generated) and made-up tags
#   Photos/Big Buck Bunny/*.jpg          stills of the trailer
#   Manga/Temple Adventure Together.cbz  one single-page chapter (a PNG page zipped; made for Banana Player)
# The Network scene needs a Samba share "Media" (user tony, password banana) of DEMO_DIR on 127.0.0.1:1445, and an
# FTP server (same user/password) on 127.0.0.1:2121; the build needs libsmb2 (-DBP_NET=ON). Without them the scene is
# skipped and the old picture stays. Samba: docker run -d -p 1445:445 -v DEMO_DIR:/mnt/media:ro dperson/samba \
#   -u "tony;banana" -s "Media;/mnt/media;yes;yes;no;tony"
set -euo pipefail
R="$(cd "$(dirname "$0")/.." && pwd)"
DEMO="$(cd "$1" && pwd)"
BIN="$R/build/banana-player"
OUT="$R/docs/img"
WORK="$(mktemp -d)"
export BP_SHOTS="$WORK" BP_BROWSE="$DEMO"
MOVIE="$DEMO/Movies/Big Buck Bunny (trailer).mkv"
SONG="$DEMO/Music/Night Drive/01 Neon Highway.mp3"
PHOTO="$DEMO/Photos/Big Buck Bunny/Big Buck Bunny 16.jpg"

# [AT=SECONDS] scene NAME "settings lines (\n separated)" STEPS [FILE]: a fresh state folder with those settings, one
# run; AT: the trailer is resumed there (a frame worth showing: the film is mostly title cards), subtitles on
scene() {
	local name="$1" conf="$2" steps="$3"
	shift 3
	local st="$WORK/state-$name"
	rm -rf "$st"
	mkdir -p "$st"
	printf "$conf\nlid-close = pause\n" > "$st/settings.conf"
	[ -z "${AT:-}" ] || printf '%s\t1\t1\t%s\n' "$AT" "$MOVIE" > "$st/resume.tsv"
	"$BIN" --state-dir "$st" --press "$steps" "$@" > "$WORK/log-$name.txt" 2>&1 || true
	[ -f "$WORK/$name-top.png" ] || echo "no picture for $name (see $WORK/log-$name.txt)" >&2
}

# 1. Home, Dark: Recent cards from a few runs first (a film left half way, a song)
HIST="$WORK/state-home"
mkdir -p "$HIST"
printf 'lid-close = pause\ntheme = dark\n' > "$HIST/settings.conf"
"$BIN" --state-dir "$HIST" --press Right,Right,B,B "$DEMO/Movies/Meadow morning.mp4" > "$WORK/log.txt" 2>&1 || true
"$BIN" --state-dir "$HIST" --press Right,B,B "$SONG" > "$WORK/log.txt" 2>&1 || true
"$BIN" --state-dir "$HIST" --press prop:pause,prop:pause,Right,Right,Right,B,B "$DEMO/Movies/Apples and acorns.mp4" > "$WORK/log.txt" 2>&1 || true
"$BIN" --state-dir "$HIST" --press prop:pause,prop:pause,shot:home > "$WORK/log-home.txt" 2>&1 || true
# 2. a film, Light, in the Three rodents chapter with its subtitle
AT=13.2 scene playing 'theme = light' prop:pause,shot:playing "$MOVIE"
# 3. a song, Dark: Match album art on, Art + visualizer, Ribbon
scene music 'theme = dark\nmusic-show = both\nvisualizer = ribbon\nmatch-album-art = yes' prop:pause,prop:pause,prop:pause,shot:music "$SONG"
# 4. Manga, Light: Extensions, the Manga reader, the series, its page (the engine download offer is dismissed with B)
scene manga 'theme = light\nmanga-reader = yes\nmanga-intro-shown = yes' R2,R2,R2,R2,R2,A,B,A,A,prop:pause,prop:pause,shot:manga
# 5. Files, Dark: the storage cards (the first two folders stand in for the cards)
BP_BROWSE_CARDS=2 BP_BROWSE="$DEMO:$DEMO/Music" scene files 'theme = dark' R2,prop:pause,shot:files
# 6. Music, Light: the Albums grid
scene library 'theme = light' R2,R2,R2,prop:pause,prop:pause,prop:pause,shot:library

# the visualizer gallery: the top screen only, a style each (VIS_SHOWN in src/settings.h), docs/img/viz/ID.jpg
mkdir -p "$OUT/viz"
find "$OUT/viz" -name '*.jpg' -delete
for tv in peaks-yellow:yellow ribbon:sakura mirror-bars:midnight-blue faders:dark horizon:sunset grid:dark-purple starfield:dark \
	strings:light helix:mint sparks:coffee bouncy-keys:16-bit rocket:midnight-blue lanterns:32-bit sleepy-moon:dark-red dance-line:pink; do
	v="${tv%%:*}"
	scene "viz-$v" "theme = ${tv##*:}\nmusic-show = visualizer\nvisualizer = $v" prop:pause,prop:pause,prop:pause,shot:viz-$v "$SONG"
	[ -f "$WORK/viz-$v-top.png" ] && ffmpeg -loglevel error -y -i "$WORK/viz-$v-top.png" -vf scale=480:360:flags=lanczos -q:v 3 "$OUT/viz/$v.jpg" || true
done

# each pair stacked: top over bottom with a hinge between, on a dark frame
for s in home playing music manga files library; do
	[ -f "$WORK/$s-top.png" ] || continue
	ffmpeg -loglevel error -y -i "$WORK/$s-top.png" -i "$WORK/$s-bottom.png" -filter_complex \
		"[0]scale=640:480:flags=lanczos[t];[1]scale=640:480:flags=lanczos[b];color=c=0x2A2931:s=640x20[h];[t][h][b]vstack=3,pad=iw+40:ih+40:20:20:color=0x2A2931" \
		-frames:v 1 "$OUT/screen-$s.png"
	echo "docs/img/screen-$s.png"
done
[ -z "${KEEP_WORK:-}" ] && rm -rf "$WORK" || echo "kept $WORK"
