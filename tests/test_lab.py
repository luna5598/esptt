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

    def wait_slot(self, slot, predicate, timeout=5):
        deadline = time.monotonic() + timeout
        while True:
            state = self.state()
            if predicate(state["slots"][slot]):
                return state
            self.assertLess(time.monotonic(), deadline, state)
            time.sleep(0.05)

    def test_distance_lesson_with_local_inputs(self):
        state = self.wait_slot(2, lambda s: s["fresh"])
        slot = dict(boot=state["boot"], slot=2, gen=state["slots"][2]["generation"])
        self.assertIsNone(state["slots"][2]["temp_centi"])
        for unsupported in ("未知指令", "开始测距\x00无效"):
            self.assertEqual(self.post("/sim/voice", **slot, phrase=unsupported, run=0)[0], 400)
        self.assertEqual(self.post("/sim/voice", **slot, phrase="开始测距", run=0)[0], 200)
        self.assertEqual(self.post("/sim/distance", **slot, mm=100)[0], 200)
        deadline = time.monotonic() + 4
        while not self.state()["course"]["events"]:
            self.assertLess(time.monotonic(), deadline)
            time.sleep(0.05)
        course = self.state()["course"]
        self.assertTrue(course["timer_paused"])
        self.assertEqual(course["samples"][0]["distance_mm"], 100)
        self.assertEqual(self.post("/sim/distance", **slot, mm=0)[0], 200)
        invalid = self.wait_slot(2, lambda s: not s["fresh"])
        self.assertIsNone(invalid["slots"][2]["distance_mm"])
        self.assertTrue(invalid["slots"][0]["fresh"])
        self.assertEqual(self.post("/sim/presence", **slot, present=0)[0], 200)
        paused = self.state()
        self.assertEqual(paused["course"]["phase"], "waiting")
        new = dict(slot, gen=paused["slots"][2]["generation"])
        self.assertEqual(self.post("/sim/presence", **new, present=1, tip=1)[0], 200)
        wrong = self.wait_slot(2, lambda s: s["fresh"] and s["tip"] == 1)
        new["gen"] = wrong["slots"][2]["generation"]
        self.assertEqual(self.post("/sim/button", **new, key=2, run=1), (409, {"error": 9}))
        self.assertEqual(self.post("/sim/presence", **new, present=1, tip=2)[0], 200)
        identified = self.wait_slot(2, lambda s: s["tip"] == 2)
        new["gen"] = identified["slots"][2]["generation"]
        self.assertEqual(self.post("/sim/distance", **new, mm=300)[0], 200)
        self.wait_slot(2, lambda s: s["fresh"])
        self.assertEqual(self.post("/api/action", **slot, action=3, run=1), (409, {"error": 3}))
        self.assertEqual(self.post("/sim/button", **new, key=2, run=1)[0], 200)
        self.assertEqual(self.post("/sim/voice", **new, phrase="停止记录", run=1)[0], 200)
        stopped = self.state()["course"]
        self.assertEqual((stopped["phase"], stopped["events"]), ("stopped", 1))

    def test_distance_fault_isolation_and_recovery(self):
        state = self.wait_slot(2, lambda s: s["fresh"])
        target = dict(boot=state["boot"], slot=2, gen=state["slots"][2]["generation"])
        for mode in (1, 2, 3):
            with self.subTest(mode=mode):
                self.assertEqual(self.post("/sim/fault", **target, mode=mode)[0], 200)
                invalid = self.wait_slot(2, lambda s: s["status"] == "invalid_sample")
                self.assertIsNone(invalid["slots"][2]["distance_mm"])
                self.assertTrue(invalid["slots"][0]["fresh"])
                self.assertEqual(self.post("/sim/voice", **target, phrase="开始测距", run=0)[0], 409)
                self.assertEqual(self.post("/sim/fault", **target, mode=0)[0], 200)
                healthy = self.wait_slot(2, lambda s: s["fresh"])
                self.assertEqual(healthy["slots"][2]["generation"], target["gen"])

    def test_single_sensor_faults_and_recovery(self):
        initial = self.wait_slot(0, lambda s: s["fresh"])
        left = dict(boot=initial["boot"], slot=0, gen=initial["slots"][0]["generation"])
        for mode in (1, 2, 3):
            with self.subTest(mode=mode):
                self.assertEqual(self.post("/sim/fault", **left, mode=mode)[0], 200)
                invalid = self.wait_slot(0, lambda s: s["status"] == "invalid_sample")
                self.assertFalse(invalid["slots"][0]["fresh"])
                self.assertIsNone(invalid["slots"][0]["temp_centi"])
                self.assertTrue(invalid["slots"][1]["fresh"])
                self.assertEqual(self.post("/api/start", **left, run=0), (409, {"error": 5}))
                self.assertEqual(self.post("/sim/fault", **left, mode=0)[0], 200)
                recovered = self.wait_slot(0, lambda s: s["fresh"])
                self.assertEqual(recovered["slots"][0]["generation"], left["gen"])

    def test_previous_branch_held_during_right_hand_lesson(self):
        state = self.state()
        boot = state["boot"]
        self.assertEqual(self.post("/sim/presence", boot=boot, slot=3,
                                   gen=state["slots"][3]["generation"], present=1, tip=1)[0], 200)
        for idx in range(4):
            state = self.wait_slot(idx, lambda s: s["fresh"])
        generations = [s["generation"] for s in state["slots"]]
        left = dict(boot=boot, slot=0, gen=generations[0])
        right = dict(boot=boot, slot=1, gen=generations[1])
        self.assertEqual(self.post("/api/start", **right, run=0)[0], 200)
        self.assertEqual(self.post("/sim/branch", **left, mode=1)[0], 200)
        fault = self.wait_slot(0, lambda s: s["status"] == "link_fault")
        self.assertIsNone(fault["slots"][0]["temp_centi"])
        before = len(fault["course"]["samples"])
        deadline = time.monotonic() + 5
        while True:
            state = self.state()
            self.assertTrue(state["bus_ready"])
            self.assertFalse(state["bus_fault"])
            self.assertEqual(state["course"]["phase"], "running")
            self.assertEqual(state["slots"][0]["status"], "link_fault")
            for idx in (1, 2, 3):
                self.assertTrue(state["slots"][idx]["fresh"])
                self.assertEqual(state["slots"][idx]["generation"], generations[idx])
            if len(state["course"]["samples"]) > before:
                break
            self.assertLess(time.monotonic(), deadline, state)
            time.sleep(0.05)
        self.assertEqual(self.post("/sim/branch", **left, mode=0), (409, {"error": 3}))
        self.assertEqual(self.post("/sim/branch", **dict(left, gen=state["slots"][0]["generation"]),
                                   mode=0)[0], 200)
        recovered = self.wait_slot(0, lambda s: s["fresh"])
        self.assertNotEqual(recovered["slots"][0]["generation"], generations[0])

    def test_body_bus_fault_retires_targets_until_recovery(self):
        state = self.wait_slot(0, lambda s: s["fresh"])
        boot = state["boot"]
        left = dict(boot=boot, slot=0, gen=state["slots"][0]["generation"])
        self.assertEqual(self.post("/api/start", **left, run=0)[0], 200)
        self.assertEqual(self.post("/sim/bus", boot=boot, mode=2)[0], 200)
        fault = self.wait_slot(0, lambda s: s["status"] == "bus_unavailable")
        self.assertEqual(self.post("/sim/bus", boot="0000000000000000", mode=0), (409, {"error": 2}))
        time.sleep(0.55)  # Includes one retry while SCL is still held.
        held = self.state()
        for snapshot in (fault, held):
            self.assertFalse(snapshot["bus_ready"])
            self.assertTrue(snapshot["bus_fault"])
            self.assertEqual(snapshot["course"]["phase"], "waiting")
            for slot in snapshot["slots"]:
                self.assertEqual(slot["status"], "bus_unavailable")
                self.assertFalse(slot["fresh"])
                self.assertIsNone(slot["temp_centi"])
                self.assertIsNone(slot["distance_mm"])
        self.assertEqual([s["generation"] for s in held["slots"]],
                         [s["generation"] for s in fault["slots"]])
        self.assertEqual(self.post("/sim/bus", boot=boot, mode=0)[0], 200)
        for idx in (0, 1, 2):
            state = self.wait_slot(idx, lambda s: s["fresh"])
        self.assertTrue(state["bus_ready"])
        self.assertFalse(state["bus_fault"])
        self.assertEqual(state["course"]["phase"], "waiting")
        self.assertEqual(self.post("/api/resume", **left, run=1), (409, {"error": 3}))
        current = dict(left, gen=state["slots"][0]["generation"])
        self.assertEqual(self.post("/api/resume", **current, run=1)[0], 200)

    def test_replace_module_and_finish_lesson(self):
        initial = self.wait_slot(0, lambda s: s["fresh"])
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
        restored = self.wait_slot(0, lambda s: s["fresh"])
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
