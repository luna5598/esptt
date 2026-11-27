"""Verify the S31 voice flash layout against the actual ESP-IDF flash manifest."""
import argparse
import json
import struct
import subprocess
from pathlib import Path


def check(condition, message):
    if not condition:
        raise ValueError(message)


def partition_table(image, flash_size):
    entry = struct.Struct("<HBBII16sI")
    layout = {}
    previous_end = 0
    for start in range(0, len(image) - entry.size + 1, entry.size):
        magic, kind, subtype, offset, size, raw_name, _ = entry.unpack_from(image, start)
        if magic in (0xEBEB, 0xFFFF):
            break
        check(magic == 0x50AA, "invalid partition table entry")
        name = raw_name.rstrip(b"\0").decode("ascii")
        check(name and name not in layout and size > 0, "invalid partition name or size")
        check(previous_end <= offset and offset + size <= flash_size, f"{name} overlaps or exceeds flash")
        layout[name] = (offset, size, kind, subtype)
        previous_end = offset + size
    return layout


def packed_models(blob):
    check(len(blob) >= 4, "missing model header")
    count, = struct.unpack_from("<I", blob)
    position = 4
    names = set()
    spans = []
    for _ in range(count):
        raw, files = struct.unpack_from("<32sI", blob, position)
        position += 36
        name = raw.rstrip(b"\0").decode("ascii")
        check(name and name not in names and files > 0, "empty or duplicate model")
        names.add(name)
        for _ in range(files):
            _, offset, length = struct.unpack_from("<32sII", blob, position)
            position += 40
            spans.append((offset, length))
    for offset, length in sorted(spans):
        check(length and offset == position and offset + length <= len(blob), "invalid model data range")
        position += length
    check(position == len(blob), "unexpected trailing model data")
    return names


def linked_symbols(build, wired):
    project = json.loads((build / "project_description.json").read_text())
    elf = build / project["app_elf"]
    result = subprocess.run(["riscv32-esp-elf-nm", "--defined-only", str(elf)],
                            check=True, capture_output=True, text=True)
    symbols = {line.split()[-1] for line in result.stdout.splitlines() if line.split()}
    required = {"voice_feed", "esp_mn_handle_from_name", "esp_srmodel_init"}
    if wired:
        required.update({"limb_poll", "i2s_channel_read"})
    check(required <= symbols, f"missing from linked firmware: {sorted(required - symbols)}")


def report(build, wired):
    linked_symbols(build, wired)
    manifest = json.loads((build / "flasher_args.json").read_text())
    check(manifest["flash_settings"]["flash_size"] == "16MB", "expected a 16 MiB flash build")
    flash_size = 16 * 1024 * 1024
    files = {int(offset, 0): build / path for offset, path in manifest["flash_files"].items()}
    table = build / "partition_table/partition-table.bin"
    check(table in files.values(), "partition table missing from flash manifest")
    layout = partition_table(table.read_bytes(), flash_size)
    for name, kind, subtype in (("ota_0", 0, 16), ("ota_1", 0, 17), ("model", 1, 130),
                                ("otadata", 1, 0), ("agent_cfg", 1, 2)):
        check(layout[name][2:] == (kind, subtype), f"invalid {name} partition type")
    slot, app_capacity, *_ = layout["ota_0"]
    check(int(manifest["app"]["offset"], 0) == slot, "application offset differs from ota_0")
    app = files[slot]
    check(app == build / manifest["app"]["file"], "application image mismatch")
    model_offset, model_capacity, *_ = layout["model"]
    model = files[model_offset].read_bytes()
    names = packed_models(model)
    check({"wn9_hilexin", "mn7_cn", "fst"} <= names, f"missing speech models: {sorted(names)}")
    app_size = app.stat().st_size
    check(0 < app_size <= min(app_capacity, layout["ota_1"][1]), "application exceeds an OTA slot")
    check(len(model) <= model_capacity, "models exceed partition")
    tail = flash_size - max(offset + size for offset, size, *_ in layout.values())
    print(f"app={app_size} bytes, ota_0_free={app_capacity-app_size}, ota_1_free={layout['ota_1'][1]-app_size}")
    print(f"models={len(model)} bytes ({', '.join(sorted(names))}), model_free={model_capacity-len(model)}")
    print(f"flash_tail={tail} bytes")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path)
    parser.add_argument("--wired", action="store_true", help="also require microphone and limb I/O")
    args = parser.parse_args()
    report(args.build, args.wired)
