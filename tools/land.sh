#!/bin/sh
# Land an agent's branch on redesign-impl and put it on the handheld: tools/land.sh BRANCH [--no-push]
# Commits what the agent left (never opencode.json), merges into rd-base, builds, runs core-test, bundles, quits the
# player and pushes. Stops at the first failure and says where. Prints one line per step.
set -e
B=$1; [ -n "$B" ] || { echo "usage: $0 BRANCH [--no-push]"; exit 2; }
ROOT=$(git rev-parse --path-format=absolute --git-common-dir)/..; ROOT=$(cd "$ROOT" && pwd)
W=$ROOT/.claude/worktrees/$B; R=$ROOT/.claude/worktrees/rd-base
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.sdk
if [ -d "$W" ] && [ -n "$(git -C "$W" status --short -- . ':!opencode.json' ':!docs/redesign/__pycache__')" ]; then
	git -C "$W" add -A -- . ':!opencode.json' ':!docs/redesign/__pycache__'
	git -C "$W" commit -q -m "$B: the agent's work (committed by land.sh)"; echo "committed leftovers in $B"
fi
cd "$R"
git merge -q --no-edit "$B" || { echo "CONFLICT: resolve in $R, then rerun"; git status --short | grep '^UU'; exit 1; }
echo "merged $(git log --oneline -1)"
cmake --build build -j 2>&1 | grep -E " error" | head -5
./build/core-test 2>&1 | grep -qE "all passed" && echo "core-test passed" || { echo "core-test FAILED"; ./build/core-test 2>&1 | grep -E "core_test.cpp:[0-9]+" | head -5; exit 1; }
git branch -f redesign-2026-11-20 redesign-impl 2>/dev/null || true
[ "$2" = "--no-push" ] && exit 0
packaging/build-bundle.sh --quick 2>&1 | tail -1
ssh -o ConnectTimeout=8 root@192.168.1.6 'killall -TERM banana-player 2>/dev/null; sleep 6; true' && tools/push-handheld.sh root@192.168.1.6 2>&1 | tail -1 || echo "handheld offline: bundle ready, push later"
