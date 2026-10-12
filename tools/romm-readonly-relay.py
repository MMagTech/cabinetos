#!/usr/bin/env python3
# Test relay for #310: 127.0.0.1:<port> -> RomM, READS ONLY.
#   * GET and HEAD are forwarded; anything else gets 403 and is logged, so a
#     test can never write to the server (saves, play sessions, last played).
#   * A platform can be shown under another folder name and display name:
#       romm-readonly-relay.py 16006 45=arcade:Arcade 22=FB%20Neo
#     rewrites fs_slug (and display_name, when given) in /api/platforms and
#     platform_fs_slug / platform_display_name in every ROM payload.
#   * A game's files can be shown under other names, or not at all (#162,
#     the disc rig): file:<fileId>=<name> renames one in every `files` list,
#     hide:<fileId> leaves it out. RomM still serves a file by its id.
#   * `people` adds four made-up people with public collections (#352, the
#     Collections tab's "From <name>" groups), so a household of five can be
#     tried with two real accounts. Each one opens onto a real platform's
#     games; Sam's public Favorites must never show.
import json, sys, urllib.parse, urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

UP = "http://192.168.1.10:6005"
PORT = int(sys.argv[1])
MAP = {}
# file:<fileId>=<name> shows a game's file under another name (#162, odd names).
FILES = {}
PEOPLE = False
# (owner id, owner name, collection id, name, smart, favourite, platform id)
MOCK = [
    (9101, "Alex", 9001, "Saturday co-op", False, False, 8),
    (9101, "Alex", 9002, "Hard mode", True, False, 10),
    (9101, "Alex", 9003, "Handhelds", False, False, 2),
    (9102, "Jordan", 9004, "Puzzle corner", True, False, 1),
    (9103, "Riley", 9005, "Arcade nights", False, False, 22),
    (9103, "Riley", 9006, "Sega only", True, False, 11),
    (9103, "Riley", 9007, "Dreamcast", True, False, 14),
    (9103, "Riley", 9008, "PlayStation", False, False, 9),
    (9103, "Riley", 9009, "Master System", False, False, 30),
    (9104, "Sam", 9010, "Favorites", False, True, 1),
    (9104, "Sam", 9011, "Game Boy Color", False, False, 3),
]
for a in sys.argv[2:]:
    if a == "people":
        PEOPLE = True
        continue
    if a.startswith("hide:"):
        FILES[int(a[5:])] = None
        continue
    if a.startswith("file:"):
        fid, name = a[5:].split("=", 1)
        FILES[int(fid)] = urllib.parse.unquote(name)
        continue
    pid, val = a.split("=", 1)
    fs, _, name = urllib.parse.unquote(val).partition(":")
    MAP[int(pid)] = (fs, name)

def rewrite(obj):
    if isinstance(obj, list):
        return [rewrite(x) for x in obj
                if not (isinstance(x, dict) and "file_size_bytes" in x and x.get("id") in FILES
                        and FILES[x["id"]] is None)]
    if isinstance(obj, dict):
        obj = {k: rewrite(v) for k, v in obj.items()}
        if "file_name" in obj and obj.get("id") in FILES and "file_size_bytes" in obj:
            obj["file_name"] = FILES[obj["id"]]
        if "fs_slug" in obj and obj.get("id") in MAP and "rom_count" in obj:
            fs, name = MAP[obj["id"]]
            obj["fs_slug"] = fs
            if name: obj["display_name"] = name; obj["custom_name"] = name
        if obj.get("platform_id") in MAP and "platform_fs_slug" in obj:
            fs, name = MAP[obj["platform_id"]]
            obj["platform_fs_slug"] = fs
            if name: obj["platform_display_name"] = name; obj["platform_custom_name"] = name
        return obj
    return obj

FIRST = {}   # platform id -> (rom ids, first cover), asked once with the caller's token

def platform_sample(pid, hdrs):
    if pid not in FIRST:
        req = urllib.request.Request(UP + "/api/roms?platform_ids=%d&limit=500&with_filter_values=false"
                                     "&with_rom_id_index=false&with_char_index=false" % pid, headers=hdrs)
        items = json.loads(urllib.request.urlopen(req, timeout=60).read()).get("items", [])
        FIRST[pid] = ([r["id"] for r in items],
                      next((r.get("path_cover_small") for r in items if r.get("path_cover_small")), None))
    return FIRST[pid]

def mock_collections(smart, hdrs):
    out = []
    for uid, uname, cid, name, is_smart, fav, pid in MOCK:
        if is_smart != smart:
            continue
        ids, cover = platform_sample(pid, hdrs)
        c = {"id": cid, "name": name, "description": "made up by the relay (#352)",
             "user_id": uid, "owner_username": uname, "is_public": True, "is_favorite": fav,
             "is_smart": smart, "is_virtual": False, "rom_ids": ids, "rom_count": len(ids),
             "path_cover_small": None, "path_covers_small": [cover] if cover else [],
             "path_cover_large": None, "path_covers_large": [],
             "created_at": "2026-10-12T00:00:00+00:00", "updated_at": "2026-10-12T00:00:00+00:00"}
        if smart:
            c["filter_criteria"] = {"platform_ids": [pid]}
            c["filter_summary"] = ""
        out.append(c)
    return out

def mock_path(path):
    # A made-up collection opens onto its platform.
    u = urllib.parse.urlsplit(path)
    if u.path != "/api/roms":
        return path
    q = urllib.parse.parse_qsl(u.query, keep_blank_values=True)
    for i, (k, v) in enumerate(q):
        if k in ("collection_id", "smart_collection_id") and v.isdigit():
            for m in MOCK:
                if m[2] == int(v):
                    q[i] = ("platform_ids", str(m[6]))
    return urllib.parse.urlunsplit(u._replace(query=urllib.parse.urlencode(q)))

class H(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    def log_message(self, *a): pass
    def refuse(self):
        sys.stderr.write(f"REFUSED {self.command} {self.path}\n"); sys.stderr.flush()
        n = int(self.headers.get("Content-Length") or 0)
        if n: self.rfile.read(n)
        self.send_response(403); self.send_header("Content-Length", "0"); self.end_headers()
    do_POST = do_PUT = do_PATCH = do_DELETE = refuse
    def do_HEAD(self): self.fwd(head=True)
    def do_GET(self): self.fwd()
    def fwd(self, head=False):
        hdrs = {k: v for k, v in self.headers.items() if k.lower() not in ("host", "connection", "accept-encoding")}
        path = mock_path(self.path) if PEOPLE else self.path
        req = urllib.request.Request(UP + path, headers=hdrs, method=self.command)
        try:
            r = urllib.request.urlopen(req, timeout=120)
        except urllib.error.HTTPError as e:
            r = e
        body = r.read() if not head else b""
        ctype = r.headers.get("Content-Type", "")
        if (MAP or FILES) and "json" in ctype and body:
            try: body = json.dumps(rewrite(json.loads(body))).encode()
            except Exception: pass
        listing = urllib.parse.urlsplit(self.path).path
        if PEOPLE and "json" in ctype and body and listing in ("/api/collections", "/api/collections/smart"):
            try:
                body = json.dumps(json.loads(body) + mock_collections(listing.endswith("smart"), hdrs)).encode()
            except Exception as e:
                sys.stderr.write(f"people: {e}\n")
        self.send_response(r.status if hasattr(r, "status") else r.code)
        for k, v in r.headers.items():
            if k.lower() in ("content-length", "transfer-encoding", "connection", "content-encoding"): continue
            self.send_header(k, v)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        if not head: self.wfile.write(body)

ThreadingHTTPServer(("127.0.0.1", PORT), H).serve_forever()
