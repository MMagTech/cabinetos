#!/usr/bin/env python3
"""A RomM stand-in for the frontend's --romm-race-test (#398, batch i).

It answers, from memory, exactly the requests the test makes (see the head
of frontend/src/romm_racetest.cpp), never forwards anything (RomM is never
asked), and refuses any request whose token or host it did not hand out:
401 for a token (the client then says "not authorised"), 400 for a host. So
a request that went out with a torn or stale address or token is seen.

On SIGTERM it prints one line: the requests it answered and the ones it
refused, by reason, then exits.
Usage: racemock.py <port>"""
import json
import os
import signal
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT = int(sys.argv[1])
HOSTS = {'127.0.0.1:%d' % PORT, 'localhost:%d' % PORT}
TOKENS = {None, 'Bearer race-a', 'Bearer race-token-b-longer'}
CONTENT = bytes((i * 7) & 0xFF for i in range(4096))
COVER = b'\x89PNG\r\n\x1a\n' + bytes(120)
GAME = {'id': 1, 'platform_id': 1, 'platform_slug': 'snes', 'platform_fs_slug': 'snes',
        'platform_display_name': 'Race', 'name': 'Race', 'fs_name': 'race.sfc',
        'name_sort_key': 'race', 'fs_size_bytes': len(CONTENT), 'has_multiple_files': False,
        'path_cover_small': '/assets/race/cover/small.png',
        'path_cover_large': '/assets/race/cover/big.png',
        'files': [{'id': 1, 'file_name': 'race.sfc', 'file_size_bytes': len(CONTENT),
                   'category': 'game', 'is_top_level': True}]}
FAVOURITES = {'id': 7, 'is_favorite': True, 'user_id': 1, 'name': 'Favorites', 'rom_count': 1}
COUNT = {'answered': 0, 'refused, token': 0, 'refused, host': 0, 'unknown': 0}
LOCK = threading.Lock()
TURN = [0]


def count(what):
    with LOCK:
        COUNT[what] += 1


class Handler(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def log_message(self, *args):
        pass

    def send(self, code, body, kind='application/json', extra=()):
        data = body if isinstance(body, bytes) else json.dumps(body).encode()
        self.send_response(code)
        self.send_header('Content-Type', kind)
        self.send_header('Content-Length', str(len(data)))
        for k, v in extra:
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(data)

    def allowed(self):
        length = int(self.headers.get('Content-Length') or 0)
        if length:
            self.rfile.read(length)
        if self.headers.get('Host') not in HOSTS:
            count('refused, host')
            self.send(400, {'detail': 'host'})
            return False
        if self.headers.get('Authorization') not in TOKENS:
            count('refused, token')
            self.send(401, {'detail': 'token'})
            return False
        return True

    def answer(self, body, code=200, kind='application/json', extra=()):
        count('answered')
        self.send(code, body, kind, extra)

    def unknown(self):
        count('unknown')
        self.send(404, {'detail': 'not here'})

    def do_GET(self):
        if not self.allowed():
            return
        path = self.path.split('?')[0]
        if path == '/api/heartbeat':
            self.answer({'SYSTEM': {'VERSION': 'race'}})
        elif path == '/api/roms':
            self.answer({'items': [GAME], 'total': 1})
        elif path == '/api/roms/1':
            self.answer(GAME)
        elif path.startswith('/assets/race/cover/'):
            self.answer(COVER, kind='image/png')
        elif path == '/api/roms/1/content/race.sfc':
            r = self.headers.get('Range')
            if r and r.startswith('bytes='):
                a, b = (int(x) for x in r[6:].split('-'))
                b = min(b, len(CONTENT) - 1)
                self.answer(CONTENT[a:b + 1], 206, 'application/octet-stream',
                            [('Content-Range', 'bytes %d-%d/%d' % (a, b, len(CONTENT)))])
            else:
                self.answer(CONTENT, kind='application/octet-stream')
        elif path == '/api/collections':
            with LOCK:
                TURN[0] += 1
                turn = TURN[0]
            self.answer([FAVOURITES] if turn % 2 else [])
        elif path == '/api/users/me':
            self.answer({'id': 1, 'username': 'race'})
        else:
            self.unknown()

    def do_POST(self):
        if not self.allowed():
            return
        path = self.path.split('?')[0]
        if path == '/api/saves':
            self.answer({'id': 1})
        elif path == '/api/states/delete':
            self.answer({})
        elif path == '/api/collections':
            self.answer({'id': 7})
        elif path == '/api/collections/7/roms':
            self.answer({})
        elif path == '/api/auth/device/token':
            self.answer({'access_token': 'race-token-b-longer'})
        else:
            self.unknown()

    def do_DELETE(self):
        if not self.allowed():
            return
        if self.path.split('?')[0] == '/api/collections/7/roms':
            self.answer({})
        else:
            self.unknown()


class Server(ThreadingHTTPServer):
    daemon_threads = True
    request_queue_size = 128


def stop(*_):
    with LOCK:
        print('racemock %d: %s' % (PORT, ', '.join('%s %d' % kv for kv in COUNT.items())), flush=True)
    os._exit(0)


signal.signal(signal.SIGTERM, stop)
Server(('127.0.0.1', PORT), Handler).serve_forever()
