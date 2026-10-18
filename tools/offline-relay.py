#!/usr/bin/env python3
# A TCP relay for testing offline play: 127.0.0.1:16005 -> the RomM server.
# Off = nothing answers at the console's address; on = the server is back.
import asyncio, os, sys, urllib.parse
# The server, from the environment as for romm-readonly-relay.py:
# CABINETOS_RELAY_UP=http://<host>:<port>.
UP = os.environ.get("CABINETOS_RELAY_UP") or sys.exit(
    "CABINETOS_RELAY_UP: set it to http://<your RomM server>:<port>")
UP = urllib.parse.urlsplit(UP if "://" in UP else "http://" + UP)
UP = (UP.hostname, UP.port or 80)
async def pipe(r, w):
    try:
        while (d := await r.read(65536)):
            w.write(d); await w.drain()
    except Exception: pass
    finally:
        try: w.close()
        except Exception: pass
async def handle(cr, cw):
    try: ur, uw = await asyncio.open_connection(*UP)
    except Exception: cw.close(); return
    await asyncio.gather(pipe(cr, uw), pipe(ur, cw))
async def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 16005
    srv = await asyncio.start_server(handle, "127.0.0.1", port)
    async with srv: await srv.serve_forever()
asyncio.run(main())
