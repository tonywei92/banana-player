#!/usr/bin/env python3
# The player <-> manga reader loop, N times, with the process measured at the end of each cycle
# (docs/notes/2026-11-11-reader-memory.md). Growth that scales with N is a leak; a step that stops is a cache filling.
#
#   tools/leak-loop/loop.py BIN N OUT [KIND] [--work DIR]
#
# BIN: build/banana-player. OUT: a path stem (OUT.txt the samples, OUT.log the player's log). --work: where the test
# media and the state folders go (default: beside OUT); the media is made with ffmpeg the first time (quiet pink
# noise songs with a cover, test-pattern CBZ chapters, a test-pattern video).
# KIND:
#   full     a song with Art + visualizer; Manga > the series > a chapter, steps, the pages grid, fullscreen off and
#            on, the next chapter, zoom, close; the player, the next song, back (the default)
#   reader   a chapter opened and closed only
#   song     the player, another song, back to the Manga library (no reader)
#   strip    chapters of long pages (bands), the reading mode cycled each time, a turn, the grid
#   engine   a video outside Manga stops the manga engine, entering Manga stops the video and starts it again
#   engidle  the Manga library left open (10 steps a cycle): the engine's idle stop after five minutes
# The engine kinds run tools/leak-loop/fake-java in the engine's place (BP_JAVA): it answers 200 on the port asked.
# macOS: vmmap's footprint, the malloc zones' bytes and blocks, the GPU's regions. Linux: /proc's RssAnon (a
# computer's GL is llvmpipe there, so textures are anonymous memory too). Under Xvfb with no sound device, set
# BP_AO_PCM=/dev/null (the songs then run untimed; "loop = all" keeps them going).
# MSL=1 (macOS): MallocStackLogging, and `malloc_history -allBySize` at cycle N into OUT.mhN (the reader closed).
import os, pty, re, shutil, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
args = [a for a in sys.argv[1:] if not a.startswith("--")]
if len(args) < 3:
    sys.exit("loop.py BIN N OUT [full|reader|song|strip|engine|engidle] [--work DIR]")
binp, N, out = args[0], int(args[1]), os.path.abspath(args[2])
kind = args[3] if len(args) > 3 else "full"
work = os.path.dirname(out)
if "--work" in sys.argv:
    work = os.path.abspath(sys.argv[sys.argv.index("--work") + 1])
engine = kind in ("engine", "engidle")


def ff(*a):
    subprocess.run(["ffmpeg", "-loglevel", "error", "-y", *a], check=True)


def media():
    # three libraries (BP_BROWSE): songs + a series of ordinary pages; the same songs + a series of long pages (the
    # Manga library lists the series read last first, so each kind has one series only); a video
    lib = work + "/leak-media"
    if os.path.exists(lib + "/made"):
        return lib
    shutil.rmtree(lib, ignore_errors=True)
    tmp = lib + "/tmp"
    for d in ("/songs/Manga/Alpha", "/songs/Music/Album", "/long/Manga/Beta", "/long/Music/Album", "/video/A Video", "/tmp"):
        os.makedirs(lib + d)
    ff("-f", "lavfi", "-i", "testsrc2=s=640x480:d=1", "-frames:v", "1", tmp + "/cover.jpg")
    for i in (1, 2, 3):
        # near silence: the loop runs on a desk for minutes
        name = "/Music/Album/0%d Quiet %d.mp3" % (i, i)
        ff("-f", "lavfi", "-i", "anoisesrc=d=600:c=pink:a=0.002:seed=%d" % i, "-i", tmp + "/cover.jpg", "-map", "0",
            "-map", "1", "-c:a", "libmp3lame", "-b:a", "128k", "-c:v", "copy", "-disposition:v", "attached_pic",
            "-id3v2_version", "3", "-metadata", "title=Quiet %d" % i, lib + "/songs" + name)
        shutil.copy(lib + "/songs" + name, lib + "/long" + name)
    for where, series, size, pages in (("songs", "Alpha", "1400x2000", 8), ("long", "Beta", "800x9000", 6)):
        names = []
        for i in range(1, pages + 1):
            names.append("%s-%02d.jpg" % (series, i))
            ff("-f", "lavfi", "-i", "testsrc2=s=%s:d=1" % size, "-frames:v", "1", "-q:v", "3", tmp + "/" + names[-1])
        for c in (1, 2):
            subprocess.run(["zip", "-q", "-0", "%s/%s/Manga/%s/Chapter %d.cbz" % (lib, where, series, c)] + names, cwd=tmp, check=True)
    ff("-f", "lavfi", "-i", "testsrc2=s=640x360:r=24:d=600", "-f", "lavfi", "-i", "anoisesrc=d=600:c=pink:a=0.002",
        "-c:v", "libx264", "-preset", "ultrafast", "-crf", "32", "-c:a", "aac", "-b:a", "64k", "-shortest",
        lib + "/video/A Video/v.mp4")
    shutil.rmtree(tmp)
    open(lib + "/made", "w").close()
    return lib


def cycle(i):
    song = "R" if i % 4 < 2 else "L"  # (three songs: forward twice, back twice)
    # L2 then R2: whichever chapter was open, the second is at the end, in fullscreen (so the Bs that follow are:
    # leave fullscreen, close, the library)
    if kind == "full":
        return "A,A,RIGHT,RIGHT,R,R,Y,B,SELECT,RIGHT,R,L2,R2,UP,B,B,B,SELECT,%s,B" % song
    if kind == "reader":
        return "A,A,RIGHT,R,B,B,B"
    if kind == "song":
        return "SELECT,%s,B" % song
    if kind == "strip":
        return "A,A,X,RIGHT,RIGHT,RIGHT,R,UP,L3,Y,B,RIGHT,L2,R2,RIGHT,B,B,B,SELECT,%s,B" % song
    if kind == "engine":
        return "B,L,L,L,A,A,A,page,page,B,B,B,R,R,R,A,page,page"
    if kind == "engidle":
        return ",".join(["page"] * 10)
    sys.exit("no such kind: " + kind)


lib = media()
# from the player: back to Home's sections, Extensions, Manga (and, without the engine, its offer answered Not now)
steps = "B,B,B,R,R,R,A,page,page".split(",") if engine else "B,B,B,B,R,R,R,A,B".split(",")
marks = {}
for i in range(N):
    steps += cycle(i).split(",")
    marks[len(steps)] = i + 1
steps += ["page"] * 6  # (the last sample is taken before the quit: the tools take a few seconds)
folder = "/video" if engine else "/long" if kind == "strip" else "/songs"
env = dict(os.environ, BP_BROWSE=lib + folder)
first = lib + ("/video/A Video/v.mp4" if engine else folder + "/Music/Album/01 Quiet 1.mp3")
state = work + "/leak-state-" + os.path.basename(out)
shutil.rmtree(state, ignore_errors=True)
os.makedirs(state)
open(state + "/settings.conf", "w").write(
    "manga-reader = yes\nmanga-intro-shown = yes\nmusic-show = both\nvisualizer = circle\nloop = all\n")
if engine:
    # "installed": the marker and a server.jar are all the host looks for before it runs BP_JAVA
    env["BP_JAVA"] = HERE + "/fake-java"
    os.makedirs(state + "/manga/java")
    open(state + "/manga/java/server.jar", "w").close()
    open(state + "/manga/java/installed", "w").write("bp-javaext 1\n")
if os.environ.get("MSL"):
    env["MallocStackLogging"] = "1"
master, slave = pty.openpty()  # a terminal: the player's stdout is line-buffered, so a press line comes as it happens
p = subprocess.Popen([binp, "--state-dir", state, "--press", ",".join(steps), first], stdout=slave,
    stderr=open(out + ".log", "w"), env=env)
os.close(slave)


def lines():
    buf = b""
    while True:
        try:
            d = os.read(master, 4096)
        except OSError:
            return
        if not d:
            return
        buf += d
        while b"\n" in buf:
            l, buf = buf.split(b"\n", 1)
            yield l.decode("utf-8", "replace").strip()


def run(*a):
    return subprocess.run(a, capture_output=True, text=True).stdout


def sample(c):
    pid = str(p.pid)
    kids = [k.split() for k in run("ps", "-ax", "-o", "ppid=,stat=,pid=").split("\n")]
    kids = [k for k in kids if k and k[0] == pid]
    extra = " children %d zombies %d" % (len(kids), sum(1 for k in kids if k[1].startswith("Z"))) if engine else ""
    if sys.platform.startswith("linux"):
        st = open("/proc/%s/status" % pid).read()
        g = lambda k: int(re.search(k + r":\s+(\d+)", st).group(1))
        line = "cycle %3d rss %7d KB anon %7d KB file %6d KB threads %3d fds %3d" % (
            c, g("VmRSS"), g("RssAnon"), g("RssFile"), g("Threads"), len(os.listdir("/proc/%s/fd" % pid)))
    else:
        vm = run("vmmap", "-summary", pid)
        fp = re.search(r"Physical footprint:\s+(\S+)", vm)
        tot = re.findall(r"^TOTAL\s+.*$", vm, re.M)  # (the last one is the malloc zones')
        t = tot[-1].split() if tot else ["?"] * 8
        gpu = re.search(r"^IOAccelerator \(graphics\)\s+(\S+)\s+.*?(\d+)\s*$", vm, re.M)
        line = "cycle %3d rss %7d KB footprint %8s threads %3d fds %3d gpu %s in %s regions malloc %s in %s blocks" % (
            c, int(run("ps", "-o", "rss=", "-p", pid).strip() or 0), fp.group(1) if fp else "?",
            len(run("ps", "-M", "-p", pid).strip().split("\n")) - 1, len(run("lsof", "-p", pid).strip().split("\n")) - 1,
            gpu.group(1) if gpu else "-", gpu.group(2) if gpu else "-", t[6], t[5])
        if os.environ.get("MSL") and c == N:
            open(out + ".mh%d" % c, "w").write(run("malloc_history", pid, "-allBySize"))
    print(line + extra, flush=True)
    open(out + ".txt", "a").write(line + extra + "\n")


open(out + ".txt", "w").close()
for line in lines():
    m = re.match(r"press (\d+):", line)
    if m and int(m.group(1)) in marks:
        time.sleep(0.45)  # (the step's own work done: a close frees on the next pass)
        sample(marks[int(m.group(1))])
p.wait()
