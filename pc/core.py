"""Build and load the four-node bench and the shared S31 course implementation."""
import ctypes as c
from pathlib import Path
import subprocess
import sys

HERE = Path(__file__).resolve().parent
BUILD = HERE.parent / "build-pc"

U = c.c_uint32
I = c.c_int32
B = c.c_uint64
LABELS = ("左手", "右手", "左脚", "右脚")
PHASES = ("idle", "running", "waiting", "done", "stopped")

class Slot(c.Structure):
    _fields_ = [("generation", U), ("present", U), ("has_sample", U),
                ("temp", I), ("humidity", U), ("count", U), ("sampled", B),
                ("tip", U), ("distance", I)]

class Course(c.Structure):
    _fields_ = [("run", U), ("phase", U), ("slot", U), ("generation", U),
                ("count", U), ("values", I * 10), ("times", B * 10),
                ("tip", U), ("events", U), ("paused", U), ("elapsed", B), ("since", B)]

def load():
    subprocess.run(["cmake", "-S", str(HERE), "-B", str(BUILD),
                    "-DCMAKE_BUILD_TYPE=Release"], check=True, stdout=sys.stderr)
    subprocess.run(["cmake", "--build", str(BUILD), "--config", "Release"],
                   check=True, stdout=sys.stderr)
    path = (BUILD / "library-Release.txt").read_text(encoding="utf-8")
    library = c.CDLL(path)
    signatures = {
        "bench_init": ([B], c.c_bool), "bench_contact": ([U, c.c_bool], None),
        "bench_attach": ([U, c.c_bool], None), "bench_distance": ([U, U], None),
        "bench_fault": ([U, U], None), "bench_status": ([U], c.c_char_p),
        "bench_tick": ([U, B, c.c_int, c.c_int], None),
        "mo_slot": ([U], c.POINTER(Slot)), "mo_course": ([], c.POINTER(Course)),
        "mo_read": ([B, U, U, B], I),
        "mo_dispatch": ([I, B, U, U, B, U], I), "mo_elapsed_ms": ([B], B),
        "mo_phrase_action": ([c.c_char_p], I), "mo_button_action": ([I, U], I),
    }
    for name, (args, result) in signatures.items():
        fn = getattr(library, name)
        fn.argtypes, fn.restype = args, result
    return library
