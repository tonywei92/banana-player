#!/bin/bash
# Banana Player's launcher, in its own folder (BananaPlayer/launch.sh). It's started by:
#   - the Store's tile, as the package's menu.launch: BananaOS's run-store-app gives it its own session,
#     HOME=/data/home, SDL_KMSDRM_FIRST (the top panel is SDL's first display) and both panels on;
#   - "Banana Player.sh" on the Ports shelf: run-port does the same, but turns the bottom panel's backlight off.
D="$(cd "$(dirname "$0")" && pwd)"
P="$(dirname "$D")"

# An update the player downloaded (Settings › Updates; Ports installs only), put in place before it starts: the
# folder swapped whole, the old one kept beside it as BananaPlayer.old; on the Ports shelf its entry and picture too.
# Whole or not there: the player renames it to BananaPlayer.update only once it's checked and unpacked
U="$P/BananaPlayer.update"
if [ -x "$U/BananaPlayer/bin/banana-player" ] && [ -f "$U/BananaPlayer/VERSION" ]; then
	rm -rf "$P/BananaPlayer.old"
	if mv "$D" "$P/BananaPlayer.old"; then
		if mv "$U/BananaPlayer" "$D"; then
			[ -f "$U/Banana Player.sh" ] && [ -f "$P/Banana Player.sh" ] && cp -f "$U/Banana Player.sh" "$P/Banana Player.sh"
			[ -f "$U/media/box/Banana Player.png" ] && [ -d "$P/media/box" ] && cp -f "$U/media/box/Banana Player.png" "$P/media/box/"
			rm -rf "$U"
			# the new launcher, from the top
			exec "$D/launch.sh" "$@"
		fi
		# it couldn't go in: the old one back
		mv "$P/BananaPlayer.old" "$D"
	fi
fi

# run-port turns the bottom panel's backlight off, for ports that draw on the top one only. Banana Player has its
# controls there: back on (run-port puts it as it was when the player quits)
if [ -f /run/banana/panel-off ]; then
	BL=$(cat /run/banana/panel-off)
	rm -f /run/banana/panel-off
	case "$BL" in /sys/class/backlight/*) [ -w "$BL/bl_power" ] && echo 0 > "$BL/bl_power" ;; esac
fi

LOG_DIR="${XDG_CONFIG_HOME:-/data/home/.config}/banana-player"
mkdir -p "$LOG_DIR"
export LD_LIBRARY_PATH="$D/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
# the player's fonts for subtitles and titles in any script (BananaOS has none of its own)
export FONTCONFIG_FILE="$D/share/banana-player/fonts.conf"
# the last run's log, for a report: home/.config/banana-player/log.txt on the card, and the run before it beside it
# (log.prev.txt: a relaunch doesn't lose what happened)
[ -f "$LOG_DIR/log.txt" ] && mv -f "$LOG_DIR/log.txt" "$LOG_DIR/log.prev.txt"

# When the lid closes: Keep playing (Settings › Controls & screen). While something plays, the player turns
# BananaOS's lid_sleep line in /data/banana/bananad.conf to "no" and puts it back after, keeping a note of what it
# changed in its state folder (lid-restore). A player that ended without putting it back (a crash, a kill) left the
# note: undone here by the player's own rule (src/lidsleep.cpp, keep the two in step): only if the file's last
# lid_sleep line still says what the player wrote (a change in BananaOS's Settings meanwhile wins), the line put
# back as it was, or taken out if it was added, the file removed if the player made it and nothing else went in.
# Then bananad reads its settings again (with the lid shut, it sleeps as the lid would have had it)
lid_restore() {
	local N="$1/lid-restore"
	[ -f "$N" ] || return 0
	local mode conf was wrote nl n endnl tmp
	mode=$(sed -n 's/^mode=//p' "$N"); conf=$(sed -n 's/^conf=//p' "$N")
	was=$(sed -n 's/^was=//p' "$N"); wrote=$(sed -n 's/^wrote=//p' "$N"); nl=$(sed -n 's/^nl=//p' "$N")
	# bananad's lid_sleep line: not starting with '#', a '=' in it, the key before it trimmed
	local LIDKEY='function lidkey(s,  k) { if (substr(s, 1, 1) == "#" || index(s, "=") == 0) return 0; k = substr(s, 1, index(s, "=") - 1); gsub(/^[[:space:]]+|[[:space:]]+$/, "", k); return k == "lid_sleep" }'
	# the last such line's number, if it's still the player's (awk's ENVIRON: as given, where -v would read backslashes)
	n=""
	[ -f "$conf" ] && n=$(WROTE="$wrote" awk "$LIDKEY"' lidkey($0) { n = NR; l = $0 } END { if (n && l == ENVIRON["WROTE"]) print n }' "$conf" 2>/dev/null)
	if [ -z "$n" ]; then
		echo "lid: restore skipped, changed in BananaOS Settings (launcher)" >&2
		rm -f "$N"
		return 0
	fi
	endnl=1
	[ -n "$(tail -c 1 "$conf")" ] && endnl=0
	tmp="$conf.bp-tmp"
	cp -p "$conf" "$tmp" || return 1
	WAS="$was" awk -v n="$n" -v mode="$mode" -v nl="$nl" -v endnl="$endnl" '
		NR == n { last = NR; if (mode != "changed") next; $0 = ENVIRON["WAS"] }
		{ if (c++) printf "\n"; printf "%s", $0 }
		END { if (nl == 1 && mode != "changed" && n == NR) endnl = 0; if (c && endnl) printf "\n" }' "$conf" > "$tmp" || { rm -f "$tmp"; return 1; }
	if [ "$mode" = created ] && [ ! -s "$tmp" ]; then
		rm -f "$tmp" "$conf"
	else
		sync
		mv -f "$tmp" "$conf" || { rm -f "$tmp"; return 1; }
	fi
	sync
	rm -f "$N"
	echo "lid: sleep restored (launcher)" >&2
	local p
	for p in /proc/[0-9]*; do
		[ "$(cat "$p/comm" 2>/dev/null)" = bananad ] && kill -HUP "${p#/proc/}" 2>/dev/null
	done
	return 0
}
# tests: BP_LID_RESTORE_ONLY=DIR runs just the undo against DIR (a state folder) and quits
if [ -n "$BP_LID_RESTORE_ONLY" ]; then
	lid_restore "$BP_LID_RESTORE_ONLY"
	exit $?
fi

# The player in the background and waited for, so a TERM to the launcher (a group kill) is seen while it runs: passed
# on, the player waited for, and the lid put back after
"$D/bin/banana-player" "$@" <&0 > "$LOG_DIR/log.txt" 2>&1 &
PID=$!
trap 'kill -TERM $PID 2>/dev/null' TERM INT HUP
wait $PID
RC=$?
# (a trap ends the first wait early: the player's own end waited for)
while kill -0 $PID 2>/dev/null; do wait $PID; RC=$?; done
trap - TERM INT HUP
# how it ended, for a report (a signal is 128 + its number: 137 a kill, 139 a segfault)
echo "launcher: the player ended with $RC" >> "$LOG_DIR/log.txt"
lid_restore "$LOG_DIR" 2>> "$LOG_DIR/log.txt"
# 75: "Restart now" with an update ready: again from the top, which puts it in place
[ "$RC" = 75 ] && exec "$D/launch.sh" "$@"
exit $RC
