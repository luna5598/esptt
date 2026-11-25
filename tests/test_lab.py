"""Exercise the local lab through its HTTP API and shared course implementation."""
import json
from pathlib import Path
import subprocess
import sys
import time
import unittest
from urllib.error import HTTPError
from urllib.parse import urlencode
from urllib.request import ProxyHandler, Request, build_opener

ROOT = Path(__file__).resolve().parents[1]


class TemperatureLab(unittest.TestCase):
    def setUp(self):
        self.process = subprocess.Popen(
            [sys.executable, str(ROOT / "pc/simulate.py"), "--port", "0"],
            stdout=subprocess.PIPE, text=True,
        )
        self.addCleanup(self.shutdown)
        self.base = self.process.stdout.readline().strip().split()[-1]
        self.http = build_opener(ProxyHandler({}))

    def shutdown(self):
        self.process.terminate()
        self.process.wait(timeout=10)
        self.process.stdout.close()

    def state(self):
        with self.http.open(self.base + "/api/state", timeout=5) as response:
            return json.load(response)

    def post(self, path, **params):
        req = Request(self.base + path + "?" + urlencode(params), method="POST",
                      headers={"X-MOBan-UI": "1"})
        try:
            with self.http.open(req, timeout=5) as response:
                return response.status, json.load(response)
        except HTTPError as error:
            with error:
                return error.code, json.load(error)

    def test_replace_module_and_finish_lesson(self):
        initial = self.state()
        left = dict(boot=initial["boot"], slot=0, gen=initial["slots"][0]["generation"])
        self.assertEqual(self.post("/api/start", **left, run=0)[0], 200)
        self.assertEqual(self.post("/api/start", **left, run=0), (409, {"error": 6}))
        deadline = time.monotonic() + 5
        while not self.state()["course"]["samples"]:
            self.assertLess(time.monotonic(), deadline)
            time.sleep(0.1)
        self.assertEqual(self.post("/sim/presence", **left, present=0)[0], 200)
        paused = self.state()
        self.assertEqual(paused["course"]["phase"], "waiting")
        self.assertTrue(paused["slots"][1]["fresh"])
        new = dict(left, gen=paused["slots"][0]["generation"])
        self.assertEqual(self.post("/sim/presence", **new, present=1)[0], 200)
        self.assertEqual(self.post("/api/resume", **left, run=1), (409, {"error": 3}))
        restored = self.state()
        self.assertEqual(restored["course"]["samples"], paused["course"]["samples"])
        new["gen"] = restored["slots"][0]["generation"]
        self.assertEqual(self.post("/api/resume", **new, run=1)[0], 200)
        deadline = time.monotonic() + 16
        while time.monotonic() < deadline:
            course = self.state()["course"]
            if course["phase"] == "done":
                break
            time.sleep(0.2)
        self.assertEqual((course["phase"], len(course["samples"])), ("done", 10))
        stamps = [s["ms"] for s in course["samples"]]
        self.assertTrue(all(b - a >= 1000 for a, b in zip(stamps, stamps[1:])))


if __name__ == "__main__":
    unittest.main()
