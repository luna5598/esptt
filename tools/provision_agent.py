"""Provision one MO companion over USB from an activated ESP-IDF environment."""
import argparse
import csv
from getpass import getpass
import os
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory
from urllib.parse import urlsplit


def validate(ssid, password, url, model, key):
    for name, value, maximum in (("ssid", ssid, 32), ("password", password, 64),
                                 ("url", url, 255), ("model", model, 95), ("key", key, 511)):
        if (not value and name != "password") or len(value.encode()) > maximum:
            raise ValueError(f"Invalid length for {name} (maximum {maximum} bytes)")
        if any(ord(ch) < 32 for ch in value):
            raise ValueError(f"Control character in {name}")
    parsed = urlsplit(url)
    if (parsed.scheme != "https" or not parsed.hostname or parsed.username is not None
            or parsed.password is not None or parsed.query or parsed.fragment):
        raise ValueError("Supply the provider's HTTPS base URL without credentials or query")
    return {"ssid": ssid, "password": password, "url": url.rstrip("/"), "model": model, "key": key}


def make_image(directory, config):
    directory = Path(directory)
    source, image = directory / "agent.csv", directory / "agent.bin"
    with source.open("w", encoding="utf-8", newline="") as output:
        writer = csv.writer(output)
        writer.writerow(("key", "type", "encoding", "value"))
        writer.writerow(("agent", "namespace", "", ""))
        for name, value in config.items():
            writer.writerow((name, "data", "string", value))
    subprocess.run([sys.executable, "-X", "utf8", "-m", "esp_idf_nvs_partition_gen", "generate",
                    str(source), str(image), "0x6000"], check=True)
    return image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="USB serial port, e.g. COM5")
    parser.add_argument("--ssid", required=True, help="Classroom Wi-Fi network")
    parser.add_argument("--url", required=True, help="OpenAI-compatible HTTPS base URL including API prefix")
    parser.add_argument("--model", required=True, help="Provider model ID with tool calling")
    args = parser.parse_args()
    if "IDF_PATH" not in os.environ:
        parser.error("Run in an activated ESP-IDF environment")
    config = validate(args.ssid, getpass("Wi-Fi password (empty for open Wi-Fi): "),
                      args.url, args.model, getpass("Model API key: "))
    parttool = Path(os.environ["IDF_PATH"]) / "components/partition_table/parttool.py"
    with TemporaryDirectory() as directory:
        image = make_image(directory, config)
        subprocess.run([sys.executable, str(parttool), "--port", args.port,
                        "write_partition", "--partition-name", "agent_cfg", "--input", str(image)],
                       check=True)


if __name__ == "__main__":
    main()
