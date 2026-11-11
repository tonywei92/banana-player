#!/usr/bin/env python3
"""A stand-in for the player's web server, to develop web/ against: fake folder tree on disk (a temp dir), one
playlist set, a now-playing state with SSE, uploads written to the temp dir, PIN K7M4.
    python3 tools/web-mock.py [port]        then open http://127.0.0.1:8099/
Switch states with /__set?scene=idle|audio|video|photo|offline|expired|online&go=/%23/files  (also logout=1, lock=1)."""
import http.server, json, os, sys, tempfile, threading, time, struct, wave, math, zlib, shutil, urllib.parse, random, re, mimetypes

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8099
WEB = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "web")
FONTS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "fonts")
ROOT = tempfile.mkdtemp(prefix="bp-mock-")
PIN = "K7M4"
lock = threading.RLock()
cond = threading.Condition(lock)

# ---- the fake card ----
def tone(path, hz, secs=40):
	with wave.open(path, "wb") as w:
		w.setnchannels(1); w.setsampwidth(2); w.setframerate(8000)
		w.writeframes(b"".join(struct.pack("<h", int(6000 * math.sin(2 * math.pi * hz * i / 8000) * (0.6 + 0.4 * math.sin(i / 4000)))) for i in range(8000 * secs)))

def png(path, w=640, h=360, hue=0):
	rows = []
	for y in range(h):
		row = bytearray([0])
		for x in range(w):
			t = (x / w + y / h) / 2
			row += bytes([int(240 * t + 15) & 255, int(180 - 90 * t + hue) & 255, int(60 + 150 * (1 - t)) & 255])
		rows.append(bytes(row))
	raw = zlib.compress(b"".join(rows))
	def chunk(k, d): return struct.pack(">I", len(d)) + k + d + struct.pack(">I", zlib.crc32(k + d) & 0xffffffff)
	open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + chunk(b"IDAT", raw) + chunk(b"IEND", b""))

FAKE = {}   # relative path -> (size, duration, kind, playable)
def mk(rel, size=None, dur=None, kind="audio", play="yes", hz=None):
	p = os.path.join(ROOT, rel)
	os.makedirs(os.path.dirname(p), exist_ok=True)
	if rel.endswith(".wav"): tone(p, hz or 440, dur or 40)
	elif rel.endswith(".jpg") or rel.endswith(".png"): png(p, hue=len(rel) * 9)
	else: open(p, "wb").write(os.urandom(64))
	FAKE[rel] = (size, dur, kind, play)

SONGS = [("Sunny Peel", 330), ("Banana Bread Blues", 262), ("Highway Slow", 392), ("Moonlight Drive", 294), ("Dust and Gold", 349), ("Last Exit", 220)]
for i, (n, hz) in enumerate(SONGS):
	mk(f"Music/Road trip/{i+1:02d} {n}.wav", size=(3 + i) * 1024 * 1024 + 123456, dur=185 + i * 23, hz=hz)
mk("Music/Road trip/cover.jpg", kind="photo")
mk("Music/Gym/Run Faster.mp3", size=5 << 20, dur=214, play="yes")
mk("Music/Gym/Lungs Burning.flac", size=31 << 20, dur=262, play="yes")
mk("Music/Gym/Old Tape.ape", size=40 << 20, dur=300, play="no")
mk("Music/Chill/Rain.ogg", size=4 << 20, dur=240)
mk("Videos/Sintel (2010).mp4", size=312 << 20, dur=888, kind="video", play="yes")
mk("Videos/Tears of Steel.mkv", size=520 << 20, dur=734, kind="video", play="maybe")
mk("Videos/Old Holiday.avi", size=900 << 20, dur=1800, kind="video", play="no")
mk("Videos/Subs/Sintel.srt", kind="sub")
mk("Photos/Beach.jpg", kind="photo"); mk("Photos/Sunset.jpg", kind="photo"); mk("Photos/Dog.png", kind="photo")
os.makedirs(os.path.join(ROOT, "Albums/Empty"), exist_ok=True)
os.makedirs(os.path.join(ROOT, "Podcasts"), exist_ok=True)
FAKE_FREE = [12_400_000_000]

KINDS = {"mp3": "audio", "wav": "audio", "flac": "audio", "ogg": "audio", "m4a": "audio", "ape": "audio", "mp4": "video", "mkv": "video", "avi": "video", "jpg": "photo", "png": "photo", "srt": "sub"}
def kind_of(name): return KINDS.get(name.rsplit(".", 1)[-1].lower(), "other")
def real(vp):
	parts = [x for x in vp.split("/") if x]
	if not parts or parts[0] != "0" or ".." in parts: return None
	return os.path.join(ROOT, *parts[1:])

# ---- player state ----
S = {}
Q = {"items": [], "index": 0}
def now_ms(): return int(time.time() * 1000)
def reset_state(scene="audio"):
	S.clear()
	S.update(path="", title="", artist="", album="", kind="", source={"kind": "none"}, loading=False, paused=True, position=0, positionAt=now_ms(),
		duration=0, speed=1, volume=62, eq={"on": True, "preset": "Rock", "gains": [4, 3, 2, 0, -1, -1, 1, 2, 3, 4]}, fx="none", loop="no",
		shuffled=False, chapters=[], chapter=-1, clients=1, playlistSerial=1)
	Q["items"] = []
	if scene == "audio": play_list(playlist_paths("road-trip"), 1, {"kind": "playlist", "title": "Road trip", "id": "road-trip"}, 61)
	if scene == "video": play_list(["0/Videos/Sintel (2010).mp4"], 0, {"kind": "folder", "title": "Videos", "id": "0/Videos"}, 120)
	if scene == "photo": pass

# the Session (docs/design/session.md): only where it plays and where picks go, as the page reads them
NOPICK = {"set": False, "ord": -1, "of": 0, "lang": "", "label": "", "external": False}
SS = {"boot": 1, "v": 1, "out": {"kind": "handheld", "tab": ""}, "pref": {"kind": "handheld", "tab": ""}}
def set_out(kind, tab="", pref=True):
	SS["out"] = {"kind": kind, "tab": tab if kind == "browser" else ""}
	if pref: SS["pref"] = dict(SS["out"])
	SS["v"] += 1
def snap():
	i = 0 if S["path"] else -1
	S["session"] = dict(boot=SS["boot"], v=SS["v"], out=SS["out"], pref=SS["pref"], load=SS["v"], loaded=True, index=i, file=S["path"],
		queue=[S["path"]] if S["path"] else [], qindex=i, playing=bool(S["path"]) and not S["paused"], pos=cur_pos(), dur=S["duration"],
		speed=S["speed"], audio=NOPICK, sub=NOPICK, parked=False, error="", shuffle=bool(S.get("shuffleMode")),
		source=dict(kind=(S.get("source") or {}).get("kind", "folder"), title=(S.get("source") or {}).get("title", ""), id=(S.get("source") or {}).get("id", "")))
	return S

def cur_pos():
	if S["paused"] or not S["path"]: return S["position"]
	return min(S["duration"], S["position"] + (now_ms() - S["positionAt"]) / 1000 * S["speed"])

def set_pos(t):
	S["position"] = max(0, t); S["positionAt"] = now_ms()

def load_file(vp, pos=0):
	rp = real(vp); rel = os.path.relpath(rp, ROOT); size, dur, kind, play = FAKE.get(rel, (None, 200, "audio", "yes"))
	name = os.path.basename(rp); title = re.sub(r"^\d+ ", "", name.rsplit(".", 1)[0])
	S.update(path=vp, title=title, artist="The Peel Brothers" if kind == "audio" else "", album="Highway Slow" if kind == "audio" else "", kind=kind, duration=dur or 200,
		paused=False, loading=False, chapters=chapters_of(vp))
	set_pos(pos); update_chapter()

def chapters_of(vp):
	if "Sintel" in vp: return [{"title": t, "start": s} for t, s in [("Opening", 0), ("The Dragon", 95), ("Search", 260), ("The Cave", 480), ("Reunion", 700)]]
	if "Tears" in vp: return []
	return []

def update_chapter():
	c = -1
	for i, ch in enumerate(S["chapters"]):
		if ch["start"] <= cur_pos(): c = i
	S["chapter"] = c

def play_list(items, index, source, pos=0):
	Q["items"] = items; Q["index"] = index
	src = dict(source); src.update(index=index, count=len(items))
	S["source"] = src; load_file(items[index], pos)

def folder_media(vp):
	rp = real(vp); out = []
	for n in sorted(os.listdir(rp)):
		if os.path.isfile(os.path.join(rp, n)) and kind_of(n) in ("audio", "video"): out.append(vp.rstrip("/") + "/" + n)
	return out

# ---- playlists ----
PL = {"road-trip": ("Road trip", [f"0/Music/Road trip/{i+1:02d} {n}.wav" for i, (n, _) in enumerate(SONGS)]),
	"gym": ("Gym", ["0/Music/Gym/Run Faster.mp3", "0/Music/Gym/Lungs Burning.flac", "0/Music/Gym/Old Tape.ape"]), "chill": ("Chill", [])}
PL = {k: [v[0], list(v[1])] for k, v in PL.items()}
# the built-in Liked: first, id ".liked", never renamed, deleted or added to; the Like button owns it (newest first)
LIKED = ".liked"
PL = {LIKED: ["Liked", ["0/Music/Gym/Run Faster.mp3"]], **PL}
def sync_liked(): S["liked"] = S.get("path", "") in PL[LIKED][1]
def likable(vp): return kind_of(vp) in ("audio", "video")
def playlist_paths(i): return list(PL[i][1])
def rev_of(i): return format(abs(hash(tuple(PL[i][1]))) % (1 << 32), "x")
def bump(): S["playlistSerial"] += 1
def pl_items(i):
	out = []
	for p in PL[i][1]:
		rp = real(p); rel = os.path.relpath(rp, ROOT) if rp else ""
		d = FAKE.get(rel, (0, 200))[1] or 200
		out.append({"p": p, "title": os.path.basename(p).rsplit(".", 1)[0], "duration": d, "missing": not os.path.exists(rp)})
	return out

SCENE = {"v": "online", "authed": False, "fails": {}, "lockUntil": 0, "expireOnce": False, "tokens": set()}
reset_state()
EVQ = []   # (event, data)

def push(ev="state"):
	sync_liked()
	with cond:
		EVQ.append((ev, json.dumps(snap()) if ev == "state" else "{}")); del EVQ[:-20]; cond.notify_all()

def ticker():
	while True:
		time.sleep(5)
		with cond:
			if S["path"] and not S["paused"]:
				update_chapter()
				if cur_pos() >= S["duration"] - 0.5: advance(1, auto=True)
		push()
threading.Thread(target=ticker, daemon=True).start()

def advance(d, auto=False):
	n = len(Q["items"])
	if not n: return
	i = Q["index"] + d
	if S["loop"] == "one" and auto: i = Q["index"]
	elif i >= n:
		if S["loop"] == "all": i = 0
		else: S["paused"] = True; set_pos(S["duration"]); return
	elif i < 0: i = 0
	Q["index"] = i; S["source"]["index"] = i; load_file(Q["items"][i])

class H(http.server.BaseHTTPRequestHandler):
	protocol_version = "HTTP/1.1"
	def log_message(self, *a): pass
	def send_json(self, obj, code=200, headers=None):
		b = json.dumps(obj).encode()
		self.send_response(code); self.send_header("Content-Type", "application/json"); self.send_header("Content-Length", str(len(b)))
		for k, v in (headers or {}).items(): self.send_header(k, v)
		self.end_headers(); self.wfile.write(b)
	def err(self, code, msg, **kw): self.send_json(dict(error=msg, **kw), code)
	def authed(self):
		m = re.search(r"bp_s=(\w+)", self.headers.get("Cookie", ""))
		return bool(m and m.group(1) in SCENE["tokens"])
	def do_GET(self): self.route("GET")
	def do_POST(self): self.route("POST")
	def do_PUT(self): self.route("PUT")
	def route(self, method):
		u = urllib.parse.urlparse(self.path); q = urllib.parse.parse_qs(u.query, keep_blank_values=True); path = u.path
		def one(k, d=""): return q.get(k, [d])[0]
		if path == "/__set":
			for k, v in q.items():
				if k == "scene":
					if v[0] in ("audio", "idle", "video", "photo"): reset_state(v[0] if v[0] != "idle" else "x"); SCENE["v"] = "online"
					else: SCENE["v"] = v[0]
				if k == "login": SCENE["tokens"].add("mocktoken")
				if k == "logout": SCENE["tokens"].clear()
				if k == "lock": SCENE["lockUntil"] = time.time() + 47
				if k == "free": FAKE_FREE[0] = int(v[0])
				if k == "browser": set_out("browser", "mock-other", False)
				if k == "handheld": set_out("handheld", "", False)
			go = one("go", "/")
			self.send_response(302); self.send_header("Location", go); self.send_header("Content-Length", "0")
			if "login" in q: self.send_header("Set-Cookie", "bp_s=mocktoken; Path=/")
			self.end_headers(); return
		if not path.startswith("/api/"):
			return self.static(path)
		if SCENE["v"] == "offline": return self.err(503, "offline")
		if path == "/api/login":
			body = self.body_form()
			if time.time() < SCENE["lockUntil"]: return self.send_json({"left": 0, "retryAfter": int(SCENE["lockUntil"] - time.time())}, 401)
			ok = body.get("pin", [""])[0].upper() == PIN
			if ok:
				SCENE["tokens"].add("mocktoken"); SCENE["fails"].clear()
				return self.send_json({"ok": True}, 200, {"Set-Cookie": "bp_s=mocktoken; Path=/; HttpOnly; SameSite=Strict"})
			f = SCENE["fails"].get("x", 0) + 1; SCENE["fails"]["x"] = f
			if f >= 5: SCENE["lockUntil"] = time.time() + 60; SCENE["fails"]["x"] = 0
			return self.send_json({"left": max(0, 5 - f), "retryAfter": int(SCENE["lockUntil"] - time.time()) if f >= 5 else 0}, 401)
		if not self.authed() or SCENE["v"] == "expired":
			return self.err(401, "signin")
		if method != "GET" and self.headers.get("X-BP") != "1": return self.err(403, "csrf")
		try: self.api(method, path, q, one)
		except (BrokenPipeError, ConnectionResetError): pass
		except Exception as e:
			import traceback; traceback.print_exc(); self.err(500, str(e))
	def body_form(self):
		n = int(self.headers.get("Content-Length", 0))
		return urllib.parse.parse_qs(self.rfile.read(n).decode(), keep_blank_values=True) if n else {}
	def static(self, path):
		if path == "/": path = "/index.html"
		if path.startswith("/fonts/"): fp = os.path.join(FONTS, path[7:])
		else: fp = os.path.join(WEB, path.lstrip("/"))
		fp = os.path.normpath(fp)
		if not os.path.isfile(fp): self.send_response(404); self.send_header("Content-Length", "0"); self.end_headers(); return
		b = open(fp, "rb").read()
		ct = {"js": "text/javascript", "css": "text/css", "svg": "image/svg+xml", "html": "text/html", "ttf": "font/ttf"}.get(fp.rsplit(".", 1)[-1], "application/octet-stream")
		self.send_response(200); self.send_header("Content-Type", ct); self.send_header("Content-Length", str(len(b))); self.send_header("Cache-Control", "no-cache"); self.end_headers(); self.wfile.write(b)

	def api(self, method, path, q, one):
		if path == "/api/state": sync_liked(); S["position"] = cur_pos() if not S["paused"] else S["position"]; S["positionAt"] = now_ms() if not S["paused"] else S["positionAt"]; return self.send_json(snap())
		if path == "/api/events": return self.sse()
		if path == "/api/logout": SCENE["tokens"].clear(); return self.send_json({"ok": True})
		if path == "/api/roots": return self.send_json({"roots": [{"name": "This card", "path": "0", "free": FAKE_FREE[0], "total": 64_000_000_000}]})
		if path == "/api/eq":
			return self.send_json({"presets": [{"name": n, "gains": g, "builtin": True} for n, g in [("Flat", [0] * 10), ("Bass Boost", [3, 5, 5, 3, 1, 0, 0, 0, 0, 0]), ("Treble Boost", [0, 0, 0, 0, 0, 1, 2, 4, 5, 5]), ("Vocal", [-3, -2, -1, 1, 3, 4, 3, 2, 0, -1]), ("Rock", [4, 3, 2, 0, -1, -1, 1, 2, 3, 4]), ("Pop", [-1, 1, 3, 4, 2, 0, -1, -1, 0, 1]), ("Jazz", [3, 2, 1, 2, -1, -1, 0, 1, 2, 3])]] + [{"name": "My mix", "gains": [2, 2, 1, 0, 0, 0, 1, 1, 2, 2], "builtin": False}]})
		if path == "/api/source":
			items = []
			for p in Q["items"]: items.append({"p": p, "title": re.sub(r"^\d+ ", "", os.path.basename(p).rsplit(".", 1)[0])})
			return self.send_json({"items": items, "index": Q["index"]})
		if path == "/api/ls":
			p = one("p"); rp = real(p)
			if not rp or not os.path.isdir(rp): return self.err(404, "No such folder")
			items = []
			for n in sorted(os.listdir(rp), key=str.lower):
				if n.startswith("."): continue
				fp = os.path.join(rp, n); rel = os.path.relpath(fp, ROOT)
				if os.path.isdir(fp): items.append({"name": n, "dir": True, "kind": "folder", "mtime": int(os.path.getmtime(fp)), "items": len(os.listdir(fp))})
				else:
					size, dur, kind, play = FAKE.get(rel, (None, None, kind_of(n), "yes"))
					items.append({"name": n, "dir": False, "size": size or os.path.getsize(fp), "mtime": int(os.path.getmtime(fp)), "kind": kind_of(n), "playable": play})
			return self.send_json({"path": p, "free": FAKE_FREE[0], "total": 64_000_000_000, "items": items})
		if path == "/api/media":
			p = one("p"); rp = real(p); rel = os.path.relpath(rp, ROOT) if rp else ""
			if not rp or not os.path.isfile(rp): return self.err(404, "No such file")
			size, dur, kind, play = FAKE.get(rel, (None, 200, kind_of(rp), "yes"))
			return self.send_json({"container": rp.rsplit(".", 1)[-1], "vcodec": "h264" if kind == "video" else "", "acodec": "aac", "duration": dur or 200, "mime": mimetypes.guess_type(rp)[0] or "audio/wav",
				"playable": play, "chapters": chapters_of(p), "title": os.path.basename(rp), "artist": "The Peel Brothers" if kind == "audio" else "", "album": "Highway Slow" if kind == "audio" else ""})
		if path == "/api/file":
			rp = real(one("p"))
			if not rp or not os.path.isfile(rp): return self.err(404, "No such file")
			return self.file(rp, one("dl") == "1")
		if path == "/api/cover":
			rp = real(one("p"))
			if not rp: return self.err(404, "none")
			cp = os.path.join(ROOT, "Music/Road trip/cover.jpg")
			if "Road trip" not in rp and not rp.endswith(".png") and not rp.endswith(".jpg"): return self.err(404, "none")
			return self.file(cp if "Road trip" in rp else rp, False)
		if path == "/api/download": return self.file(real(one("p")), True)
		if path == "/api/upload": return self.upload(q, one)
		if path == "/api/playlists":
			return self.send_json({"serial": S["playlistSerial"], "playlists": [dict({"id": k, "name": v[0], "count": len(v[1])}, **({"builtin": True} if k == LIKED else {})) for k, v in PL.items()]})
		if path == "/api/likes": return self.send_json({"serial": S["playlistSerial"], "count": len(PL[LIKED][1]), "paths": list(PL[LIKED][1])})
		if path == "/api/playlist":
			i = one("id")
			if i not in PL: return self.err(404, "No such playlist")
			return self.send_json(dict({"id": i, "name": PL[i][0], "rev": rev_of(i), "items": pl_items(i)}, **({"builtin": True} if i == LIKED else {})))
		# --- writes (form bodies)
		f = self.body_form()
		def g(k, d=""): return f.get(k, [d])[0]
		def gl(k): return f.get(k + "[]", f.get(k, []))
		time.sleep(0.05)
		with cond:
			if path == "/api/mkdir":
				rp = real(g("p")); nm = g("name")
				if "/" in nm or not nm.strip(): return self.err(400, "Names can't contain /", code="name")
				t = os.path.join(rp, nm)
				if os.path.exists(t): return self.err(409, "A folder with that name is already here", code="exists")
				os.makedirs(t); return self.send_json({"ok": True})
			if path == "/api/rename":
				rp = real(g("p")); nm = g("name"); t = os.path.join(os.path.dirname(rp), nm)
				if "/" in nm or not nm.strip(): return self.err(400, "Names can't contain /", code="name")
				if os.path.exists(t): return self.err(409, "A file with that name is already here", code="exists")
				os.rename(rp, t); return self.send_json({"ok": True})
			if path == "/api/move":
				to = real(g("to"))
				for p in gl("p"):
					t = os.path.join(to, os.path.basename(p))
					if os.path.exists(t): return self.err(409, "Something named %s is already there" % os.path.basename(p), code="exists")
					shutil.move(real(p), t)
				return self.send_json({"ok": True})
			if path == "/api/delete":
				for p in gl("p"):
					if p == S["path"]: return self.err(409, "That file is playing on the handheld", code="playing")
					rp = real(p); shutil.rmtree(rp) if os.path.isdir(rp) else os.remove(rp)
				return self.send_json({"ok": True})
			if path == "/api/play":
				ps = gl("p")
				if g("to") == "handheld": set_out("handheld")
				if g("playlist"):
					i = g("playlist"); idx = int(g("index", "0")); items = playlist_paths(i)
					if g("shuffle") == "1": random.shuffle(items); idx = 0; S["shuffled"] = True
					else: S["shuffled"] = False
					play_list(items, idx, {"kind": "playlist", "title": PL[i][0], "id": i}, float(g("t", "0")))
				elif len(ps) > 1:
					items = []
					for p in ps:
						rp = real(p)
						items += folder_media(p) if os.path.isdir(rp) else ([p] if kind_of(p) in ("audio", "video") else [])
					if g("shuffle") == "1": random.shuffle(items)
					play_list(items, 0, {"kind": "playlist", "title": "Selection", "id": ""})
				elif ps:
					p = ps[0]; rp = real(p)
					if os.path.isdir(rp):
						items = folder_media(p); play_list(items, 0, {"kind": "folder", "title": os.path.basename(rp), "id": p})
					else:
						d = p.rsplit("/", 1)[0]; items = folder_media(d); play_list(items, items.index(p), {"kind": "folder", "title": os.path.basename(real(d)), "id": d}, float(g("t", "0")))
				push(); return self.send_json({"ok": True})
			if path == "/api/intent":
				if g("type") == "SetOutput":
					if g("to") == "handheld" and SS["out"]["kind"] == "browser" and S["path"]: set_pos(float(g("at", cur_pos()))); S["paused"] = False
					elif g("to") == "browser": set_pos(cur_pos()); S["paused"] = True
					set_out(g("to"), g("tab")); push()
				if g("type") == "Open":
					qs = q.get("q", []) or f.get("q", [])
					play_list(qs, int(g("index", "0")), {"kind": g("skind", "folder"), "title": g("stitle"), "id": g("sid")}, float(g("t", "0")))
					S["paused"] = True; set_out("browser", g("tab")); push()
				if g("type") == "Stop": reset_state("x"); push()
				if g("type") == "SetShuffle": S["shuffleMode"] = S["shuffled"] = g("on") == "1"; SS["v"] += 1; push()
				return self.send_json({"ok": True, "v": SS["v"], "session": snap()["session"]})
			if path == "/api/cmd":
				c = g("c")
				if c == "toggle": 
					set_pos(cur_pos()); S["paused"] = not S["paused"]
				elif c == "pause": set_pos(cur_pos()); S["paused"] = True
				elif c == "play": set_pos(cur_pos()); S["paused"] = False
				elif c == "next": advance(1)
				elif c == "prev": advance(-1) if cur_pos() < 3 else set_pos(0)
				elif c == "stop": reset_state("x")
				elif c == "seek": set_pos(float(g("t", "0")))
				elif c == "seekrel": set_pos(cur_pos() + float(g("d", "0")))
				elif c == "chapter":
					cs = S["chapters"]; i = max(0, min(len(cs) - 1, S["chapter"] + int(g("dir", "1")))); set_pos(cs[i]["start"]) if cs else None
				elif c == "chapterto": set_pos(S["chapters"][int(g("i"))]["start"])
				elif c == "loop": S["loop"] = g("v", "no")
				elif c == "shuffle": 
					random.shuffle(Q["items"]); Q["index"] = 0; S["source"]["index"] = 0; S["shuffled"] = True; load_file(Q["items"][0])
				update_chapter(); push(); return self.send_json({"ok": True})
			if path == "/api/volume": S["volume"] = int(g("v")); push(); return self.send_json({"ok": True})
			if path == "/api/eq":
				if g("on"): S["eq"]["on"] = g("on") == "1"
				if g("preset"): S["eq"]["preset"] = g("preset")
				if g("gains"): S["eq"]["gains"] = [float(x) for x in g("gains").split(",")]; S["eq"]["preset"] = g("preset", "")
				push(); return self.send_json({"ok": True})
			if path == "/api/fx": S["fx"] = g("key"); push(); return self.send_json({"ok": True})
			if path == "/api/like":
				vp = g("p"); vp = S["path"] if vp == "current" else vp
				if not vp: return self.err(409, "Nothing is playing", code="idle")
				if not real(vp) or not likable(vp): return self.err(400, "Only songs and videos can be liked", code="type")
				L = PL[LIKED][1]; want = g("on") in ("1", "true") if g("on") != "" else vp not in L
				if want and vp not in L: L.insert(0, vp)
				if not want: L[:] = [x for x in L if x != vp]
				bump(); push("playlists"); push(); return self.send_json({"liked": vp in L, "path": vp, "serial": S["playlistSerial"]})
			if path in ("/api/playlists/rename", "/api/playlists/delete") and g("id") == LIKED or \
					path in ("/api/playlist/add", "/api/playlist/move", "/api/playlist/dedupe") and g("id") == LIKED:
				return self.err(403, "Liked is managed with the Like button", code="builtin")
			if path == "/api/playlists/create":
				nm = g("name").strip()
				if not nm or len(nm) > 40 or "/" in nm: return self.err(400, "Names can't contain /", code="name")
				if any(v[0].lower() == nm.lower() for v in PL.values()): return self.err(409, "A playlist with that name already exists", code="exists")
				i = re.sub(r"\W+", "-", nm.lower()); PL[i] = [nm, []]; bump(); return self.send_json({"id": i})
			if path == "/api/playlists/rename":
				nm = g("name").strip()
				if any(v[0].lower() == nm.lower() and k != g("id") for k, v in PL.items()): return self.err(409, "A playlist with that name already exists", code="exists")
				PL[g("id")][0] = nm; bump(); return self.send_json({"id": g("id")})
			if path == "/api/playlists/delete": PL.pop(g("id"), None); bump(); return self.send_json({"ok": True})
			if path.startswith("/api/playlist/"):
				i = g("id")
				if i not in PL: return self.err(404, "No such playlist")
				if g("rev") != rev_of(i): return self.err(409, "Changed elsewhere", code="rev")
				L = PL[i][1]; act = path.rsplit("/", 1)[1]
				if act == "add":
					at = int(g("at", str(len(L)))); new = []
					for p in gl("p"):
						new += folder_media(p) if os.path.isdir(real(p)) else [p]
					L[at:at] = new
				elif act == "remove":
					for k in sorted((int(x) for x in gl("i")), reverse=True): del L[k]
				elif act == "move":
					sel = sorted(int(x) for x in gl("i")); at = int(g("to")); moved = [L[k] for k in sel]
					at -= len([k for k in sel if k < at])
					for k in reversed(sel): del L[k]
					L[at:at] = moved
				elif act == "dedupe":
					seen = []; [seen.append(x) for x in L if x not in seen]; L[:] = seen
				bump(); push("playlists"); return self.send_json({"rev": rev_of(i), "count": len(L)})
		self.err(404, "no such endpoint " + path)

	def sse(self):
		self.send_response(200); self.send_header("Content-Type", "text/event-stream"); self.send_header("Cache-Control", "no-cache"); self.send_header("Connection", "close"); self.end_headers()
		sync_liked(); self.wfile.write(("event: state\ndata: %s\n\n" % json.dumps(snap())).encode()); self.wfile.flush()
		last = len(EVQ) and id(EVQ[-1]); seen = id(EVQ[-1]) if EVQ else None
		while True:
			with cond:
				cond.wait(25)
				new = [e for e in EVQ]
				fresh = []
				if seen is None: fresh = new
				else:
					ids = [id(e) for e in new]
					fresh = new[ids.index(seen) + 1:] if seen in ids else new
				if new: seen = id(new[-1])
			try:
				if not fresh: self.wfile.write(b": ka\n\n")
				for ev, data in fresh: self.wfile.write(("event: %s\ndata: %s\n\n" % (ev, data)).encode())
				self.wfile.flush()
			except Exception: return

	def file(self, fp, dl):
		size = os.path.getsize(fp); rng = self.headers.get("Range"); start, end = 0, size - 1; code = 200
		if rng:
			m = re.match(r"bytes=(\d*)-(\d*)", rng)
			if m:
				if m.group(1): start = int(m.group(1))
				if m.group(2): end = int(m.group(2))
				if not m.group(1) and m.group(2): start = size - int(m.group(2)); end = size - 1
				code = 206
		end = min(end, size - 1)
		self.send_response(code); self.send_header("Content-Type", mimetypes.guess_type(fp)[0] or "application/octet-stream"); self.send_header("Accept-Ranges", "bytes")
		self.send_header("Content-Length", str(end - start + 1))
		if code == 206: self.send_header("Content-Range", "bytes %d-%d/%d" % (start, end, size))
		if dl: self.send_header("Content-Disposition", 'attachment; filename="%s"' % os.path.basename(fp))
		self.end_headers()
		with open(fp, "rb") as f:
			f.seek(start); left = end - start + 1
			while left > 0:
				b = f.read(min(65536, left))
				if not b: break
				self.wfile.write(b); left -= len(b)

	def upload(self, q, one):
		n = self.headers.get("Content-Length")
		if n is None: return self.err(411, "Length required")
		n = int(n); d = real(one("dir")); name = one("name"); rel = one("rel")
		if not d: return self.err(400, "Bad folder")
		td = os.path.join(d, rel) if rel else d; os.makedirs(td, exist_ok=True); t = os.path.join(td, name)
		if os.path.exists(t) and one("overwrite") != "1": self.rfile.read(n); return self.err(409, "Already there", code="exists")
		left = n
		with open(t + ".part", "wb") as f:
			while left > 0:
				b = self.rfile.read(min(65536, left))
				if not b: break
				f.write(b); left -= len(b); time.sleep(0.02 if n > 500000 else 0)
		os.replace(t + ".part", t); self.send_json({"ok": True})

if __name__ == "__main__":
	srv = http.server.ThreadingHTTPServer(("127.0.0.1", PORT), H); srv.daemon_threads = True
	print("mock on http://127.0.0.1:%d/  (PIN %s, card %s)" % (PORT, PIN, ROOT), flush=True)
	srv.serve_forever()
