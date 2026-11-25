"""Build and load the same course implementation used by the S31 firmware."""
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
PHASES = ("idle", "running", "waiting", "done")

class Slot(c.Structure):
    _fields_ = [("generation", U), ("present", U), ("has_sample", U),
                ("temp", I), ("humidity", U), ("count", U), ("sampled", B)]

class Course(c.Structure):
    _fields_ = [("run", U), ("phase", U), ("slot", U), ("generation", U),
                ("count", U), ("temps", I * 10), ("times", B * 10)]

def load():
    subprocess.run(["cmake", "-S", str(HERE), "-B", str(BUILD),
                    "-DCMAKE_BUILD_TYPE=Release"], check=True, stdout=sys.stderr)
    subprocess.run(["cmake", "--build", str(BUILD), "--config", "Release"],
                   check=True, stdout=sys.stderr)
    path = (BUILD / "library-Release.txt").read_text(encoding="utf-8")
    library = c.CDLL(path)
    signatures = {
        "mo_init": ([B], None), "mo_presence": ([U, U], I),
        "mo_slot": ([U], c.POINTER(Slot)), "mo_course": ([], c.POINTER(Course)),
        "mo_sample": ([B, U, U, B, I, U], I), "mo_read": ([B, U, U, B], I),
        "mo_start": ([B, U, U, B, U], I), "mo_resume": ([B, U, U, B, U], I),
    }
    for name, (args, result) in signatures.items():
        fn = getattr(library, name)
        fn.argtypes, fn.restype = args, result
    return library
