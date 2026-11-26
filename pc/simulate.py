"""Run a loopback-only sensor lab using Python, CMake and a native C compiler."""
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
        if not self.core.bench_init(self.boot):
            raise RuntimeError("Invalid bench ADC profile")
        self.started = time.monotonic_ns()
        self.tick()

    def now(self):
        return (time.monotonic_ns() - self.started) // 1_000_000

    def tick(self):
        now = self.now()
        for index in range(len(LABELS)):
            wave = math.sin(now / 6100 + index * 1.3)
            self.core.bench_tick(index, now, round(2210 + index * 42 + 95 * wave),
                                 round(4800 + 210 * wave))

    def snapshot(self):
        now = self.now()
        slots = []
        for i, name in enumerate(LABELS):
            s = self.core.mo_slot(i).contents
            slots.append(dict(name=name, generation=s.generation, present=bool(s.present),
                              status=self.core.bench_status(i).decode("ascii"),
                              fresh=self.core.mo_read(self.boot, i, s.generation, now) == 0,
                              tip=s.tip, distance_mm=s.distance if s.has_sample and s.tip == 2 else None,
                              temp_centi=s.temp if s.has_sample and s.tip == 1 else None,
                              humidity_centi=s.humidity if s.has_sample and s.tip == 1 else None))
        c = self.core.mo_course().contents
        field = "distance_mm" if c.tip == 2 else "temp_centi"
        samples = [{"ms": c.times[i], field: c.values[i]} for i in range(c.count)]
        return dict(boot=f"{self.boot:016x}", bus_ready=True, slots=slots,
                    course=dict(run=c.run, phase=PHASES[c.phase], slot=c.slot, goal=10, samples=samples,
                                tip=c.tip, events=c.events, timer_paused=bool(c.paused),
                                elapsed_ms=self.core.mo_elapsed_ms(now)))

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
        if path in ("/sim/presence", "/sim/distance", "/sim/fault"):
            slot = self.core.mo_slot(idx).contents
            if boot != self.boot:
                return 2
            if gen != slot.generation:
                return 3
            if path == "/sim/presence":
                if number("present", "[01]", 1):
                    kind = number("tip", "[12]", 2) if "tip" in query else 1
                    self.core.bench_attach(idx, kind == 2)
                else:
                    self.core.bench_contact(idx, False)
            elif path == "/sim/distance":
                if slot.tip != 2:
                    return 9
                self.core.bench_distance(idx, number("mm", r"[0-9]{1,5}", 32767))
            else:
                if slot.tip == 2:
                    return 9
                self.core.bench_fault(idx, number("mode", "[0-3]", 3))
            self.tick()
            return 0
        run = number("run", r"[0-9]{1,10}", UINT32_MAX)
        if path == "/sim/voice":
            phrases = query.get("phrase", [])
            if len(phrases) != 1 or "\x00" in phrases[0]:
                raise ValueError
            action = self.core.mo_phrase_action(phrases[0].encode())
        elif path == "/sim/button":
            action = self.core.mo_button_action(number("key", "[1-3]", 3), idx)
        elif path == "/api/action":
            action = number("action", "[1-4]", 4)
        else:
            action = 1 if path == "/api/start" else 3
        if not action:
            raise ValueError
        return self.core.mo_dispatch(action, boot, idx, gen, now, run)


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
        if req.path not in ("/api/start", "/api/resume", "/api/action", "/sim/presence", "/sim/fault",
                            "/sim/distance", "/sim/button", "/sim/voice"):
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
        server.serve_forever(poll_interval=0.01)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
    except FileNotFoundError as error:
        sys.exit(f"Install CMake and a native C compiler, then run again. {error}")
