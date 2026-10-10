#!/usr/bin/env python3
# Test relay for #310: 127.0.0.1:<port> -> RomM, READS ONLY.
#   * GET and HEAD are forwarded; anything else gets 403 and is logged, so a
#     test can never write to the server (saves, play sessions, last played).
#   * A platform can be shown under another folder name and display name:
#       romm-readonly-relay.py 16006 45=arcade:Arcade 22=FB%20Neo
#     rewrites fs_slug (and display_name, when given) in /api/platforms and
#     platform_fs_slug / platform_display_name in every ROM payload.
import json, sys, urllib.parse, urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

UP = "http://192.168.1.10:6005"
PORT = int(sys.argv[1])
MAP = {}
for a in sys.argv[2:]:
    pid, val = a.split("=", 1)
    fs, _, name = urllib.parse.unquote(val).partition(":")
    MAP[int(pid)] = (fs, name)

def rewrite(obj):
    if isinstance(obj, list):
        return [rewrite(x) for x in obj]
    if isinstance(obj, dict):
        obj = {k: rewrite(v) for k, v in obj.items()}
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
        req = urllib.request.Request(UP + self.path, headers=hdrs, method=self.command)
        try:
            r = urllib.request.urlopen(req, timeout=120)
        except urllib.error.HTTPError as e:
            r = e
        body = r.read() if not head else b""
        ctype = r.headers.get("Content-Type", "")
        if MAP and "json" in ctype and body:
            try: body = json.dumps(rewrite(json.loads(body))).encode()
            except Exception: pass
        self.send_response(r.status if hasattr(r, "status") else r.code)
        for k, v in r.headers.items():
            if k.lower() in ("content-length", "transfer-encoding", "connection", "content-encoding"): continue
            self.send_header(k, v)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        if not head: self.wfile.write(body)

ThreadingHTTPServer(("127.0.0.1", PORT), H).serve_forever()
