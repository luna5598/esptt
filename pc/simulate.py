"""Run a loopback-only temperature lab using Python, CMake and a native C compiler."""
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import math
from pathlib import Path
import re
import secrets
import sys
import time
from threading import Lock
from urllib.parse import parse_qs, urlsplit

from core import LABELS, PHASES, load

HERE = Path(__file__).resolve().parent
UINT32_MAX = (1 << 32) - 1


class Bench:
    def __init__(self):
        self.core = load()
        self.boot = secrets.randbits(64) or 1
        self.core.mo_init(self.boot)
        self.started = time.monotonic_ns()
        self.next_sample = 0
        self.core.mo_presence(0, 1)
        self.core.mo_presence(1, 1)
        self.tick()

    def now(self):
        return (time.monotonic_ns() - self.started) // 1_000_000

    def sample(self, index, slot, now):
        wave = math.sin(now / 6100 + index * 1.3)
        self.core.mo_sample(self.boot, index, slot.generation, now,
                            round(2210 + index * 42 + 95 * wave), round(4800 + 210 * wave))

    def tick(self):
        now = self.now()
        if now < self.next_sample:
            return
        self.next_sample = now + 1000
        for index in range(len(LABELS)):
            slot = self.core.mo_slot(index).contents
            if slot.present:
                self.sample(index, slot, now)

    def snapshot(self):
        now = self.now()
        slots = []
        for i, name in enumerate(LABELS):
            s = self.core.mo_slot(i).contents
            slots.append(dict(name=name, generation=s.generation, present=bool(s.present),
                              status="sample" if s.present else "empty",
                              fresh=self.core.mo_read(self.boot, i, s.generation, now) == 0,
                              temp_centi=s.temp if s.has_sample else None,
                              humidity_centi=s.humidity if s.has_sample else None))
        c = self.core.mo_course().contents
        samples = [dict(ms=c.times[i], temp_centi=c.temps[i]) for i in range(c.count)]
        return dict(boot=f"{self.boot:016x}", bus_ready=True, slots=slots,
                    course=dict(run=c.run, phase=PHASES[c.phase], slot=c.slot, goal=10, samples=samples))

    def action(self, path, query):
        def number(name, pattern, limit, base=10):
            values = query.get(name, [])
            if len(values) != 1 or not re.fullmatch(pattern, values[0]):
                raise ValueError
            value = int(values[0], base)
            if value > limit:
                raise ValueError
            return value

        boot = number("boot", r"[0-9a-fA-F]{16}", (1 << 64) - 1, 16)
        idx = number("slot", r"[0-9]", len(LABELS) - 1)
        gen = number("gen", r"[0-9]{1,10}", UINT32_MAX)
        now = self.now()
        if path == "/sim/presence":
            present = number("present", "[01]", 1)
            slot = self.core.mo_slot(idx).contents
            if boot != self.boot:
                return 2
            if gen != slot.generation:
                return 3
            changed = present != slot.present
            result = self.core.mo_presence(idx, present)
            if result == 0 and changed and present:
                self.sample(idx, slot, now)
            return result
        run = number("run", r"[0-9]{1,10}", UINT32_MAX)
        action = self.core.mo_start if path == "/api/start" else self.core.mo_resume
        return action(boot, idx, gen, now, run)


class BenchServer(ThreadingHTTPServer):
    def service_actions(self):
        with self.lock:
            self.bench.tick()


class Handler(BaseHTTPRequestHandler):
    def reply(self, status, data, mime="application/json"):
        if mime == "application/json":
            data = json.dumps(data, ensure_ascii=False)
        data = data.encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", mime + "; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        path = urlsplit(self.path).path
        if path == "/":
            self.reply(200, self.server.page, "text/html")
        elif path == "/api/state":
            with self.server.lock:
                snapshot = self.server.bench.snapshot()
            self.reply(200, snapshot)
        else:
            self.send_error(404)

    def do_POST(self):
        req = urlsplit(self.path)
        if req.path not in ("/api/start", "/api/resume", "/sim/presence"):
            return self.send_error(404)
        host = self.headers.get("Host")
        if (self.headers.get("X-MOBan-UI") != "1" or
                host not in {f"127.0.0.1:{self.server.server_port}", f"localhost:{self.server.server_port}"} or
                self.headers.get("Origin") not in (None, "http://" + host)):
            return self.reply(403, {"error": "Local UI required"})
        try:
            with self.server.lock:
                rc = self.server.bench.action(req.path, parse_qs(req.query, max_num_fields=5))
        except ValueError:
            return self.reply(400, {"error": "Invalid target"})
        if rc:
            self.reply(409, {"error": rc})
        else:
            self.reply(200, {"ok": True})

    def log_message(self, fmt, *args):
        if self.path != "/api/state":
            super().log_message(fmt, *args)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8765)
    args = parser.parse_args()
    bench = Bench()
    page = (HERE.parent / "main/index.html").read_text(encoding="utf-8")
    panel = (HERE / "panel.html").read_text(encoding="utf-8")
    with BenchServer(("127.0.0.1", args.port), Handler) as server:
        server.lock = Lock()
        server.bench = bench
        server.page = page.replace("</main>", panel + "</main>")
        print(f"MO companion simulator: http://127.0.0.1:{server.server_port}", flush=True)
        server.serve_forever(poll_interval=0.1)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
    except FileNotFoundError as error:
        sys.exit(f"Install CMake and a native C compiler, then run again. {error}")
