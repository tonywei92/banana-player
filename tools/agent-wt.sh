#!/bin/sh
# A worktree for one agent, ready to build in seconds: tools/agent-wt.sh NAME [BASE]
# Branch NAME from BASE (redesign-impl), the parent's build/ copied in and repointed, the deps cache linked, the
# permission file copied, the spec refreshed from redesign-preview. Prints the path to cd into before launching.
set -e
NAME=$1; BASE=${2:-redesign-impl}
[ -n "$NAME" ] || { echo "usage: $0 NAME [BASE]"; exit 2; }
ROOT=$(git rev-parse --path-format=absolute --git-common-dir)/..; ROOT=$(cd "$ROOT" && pwd)
PARENT=$ROOT/.claude/worktrees/rd-base; W=$ROOT/.claude/worktrees/$NAME
git -C "$ROOT" worktree add -q -b "$NAME" "$W" "$BASE"
# (all at low priority on 4 cores: a full-speed copy and build froze the Mac)
nice -n 19 rsync -a "$PARENT/build/" "$W/build/"
nice -n 19 grep -rlI --include=*.cmake --include=Makefile --include=*.make --include=CMakeCache.txt --include=*.txt --include=*.d --include=*.ninja "worktrees/rd-base" "$W/build" | while read -r f; do sed -i '' "s#worktrees/rd-base#worktrees/$NAME#g" "$f"; done
mkdir -p "$W/.cache" && ln -sfn "$ROOT/.cache/mac" "$W/.cache/mac"
cp "$ROOT/opencode.json" "$W/opencode.json"
if git -C "$W" checkout redesign-preview -- docs/redesign 2>/dev/null && ! git -C "$W" diff --cached --quiet; then
	git -C "$W" rm -rq --cached docs/redesign/__pycache__ 2>/dev/null || true
	git -C "$W" diff --cached --quiet || git -C "$W" commit -q -m "docs/redesign: the spec from redesign-preview"
fi
(cd "$W" && SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.sdk nice -n 19 cmake --build build -j4 >/dev/null 2>&1 && echo "built")
echo "$W"
