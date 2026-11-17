"""Reconstruct and verify the reviewed release files on the GitHub runner."""

from argparse import ArgumentParser
from hashlib import file_digest, sha1
import json
from pathlib import Path
import re
from tempfile import TemporaryDirectory
from zipfile import ZipFile

ROOT = Path(__file__).resolve().parent
TAG = "0.1.9-beta"
BASE = {"bytes": 333820628, "sha256": "e8e420a7f79fbb2dab146f77c5084971da10221995db85eec2f6d378b4ece9b2"}
MANAGER = f"lvmeng-mod-manager-{TAG}.zip"
SOURCE = f"lvmeng-mod-source-{TAG}.zip"
PNGS = {"palette-combat-01.png", "palette-combat-02.png", "palette-workbench.png", "palette-manager.png"}
NAMES = {MANAGER, SOURCE, MANAGER + ".sha256", SOURCE + ".sha256"} | PNGS


def digest(path):
    with path.open("rb") as stream:
        return file_digest(stream, "sha256").hexdigest()


def check_file(path, item):
    assert path.is_file() and not path.is_symlink()
    assert path.stat().st_size == item["bytes"] and digest(path) == item["sha256"], path.name


def transfer(source, target, start, size, limit):
    assert type(start) is type(size) is int and 0 <= start < start + size <= limit
    source.seek(start)
    while size:
        data = source.read(min(1024*1024, size))
        assert data
        target.write(data)
        size -= len(data)


def assemble(previous, output):
    data = json.loads((ROOT / "manifest.json").read_bytes())
    assert data["tag"] == TAG and data["base"] == BASE and re.fullmatch(r"[0-9a-f]{40}", data["targetCommit"])
    assert {item["name"] for item in data["assets"]} == NAMES and len(data["assets"]) == len(NAMES)
    assert (ROOT / "notes.md").read_bytes().strip()
    check_file(previous, BASE)
    output.mkdir()
    with TemporaryDirectory(prefix="release-019-", dir=output.parent) as scratch:
        combined = Path(scratch) / "delta"
        with combined.open("xb") as target:
            for i, part in enumerate(data["parts"]):
                assert part["path"] == f"parts/data-{i:03}.part" and 0 < part["bytes"] <= 512*1024
                path = ROOT / part["path"]
                check_file(path, part)
                block = path.read_bytes()
                assert sha1(f"blob {len(block)}\0".encode() + block).hexdigest() == part["gitBlobSha1"]
                target.write(block)
        with previous.open("rb") as base, combined.open("rb") as delta:
            ranges = {"base": (base, BASE["bytes"]), "data": (delta, combined.stat().st_size)}
            for item in data["assets"]:
                path = output / item["name"]
                assert sum(row[2] for row in item["ranges"]) == item["bytes"]
                with path.open("xb") as target:
                    for kind, start, size in item["ranges"]:
                        src, limit = ranges[kind]
                        transfer(src, target, start, size, limit)
                check_file(path, item)
    for name in MANAGER, SOURCE:
        with ZipFile(output / name) as archive:
            assert archive.testzip() is None
            if name == MANAGER:
                assert archive.read("VERSION.txt").decode().strip() == TAG
        assert (output / (name + ".sha256")).read_text().split()[0] == digest(output / name)
    for name in PNGS:
        assert (output / name).read_bytes().startswith(b"\x89PNG\r\n\x1a\n")
    return {"tag": TAG, "targetCommit": data["targetCommit"],
            "assets": [{k: item[k] for k in ("name", "bytes", "sha256")} for item in data["assets"]]}


def verify_upload(expected, remote):
    wanted = {a["name"]: (a["bytes"], "sha256:" + a["sha256"], "uploaded")
              for a in json.loads(expected.read_bytes())["assets"]}
    actual = json.loads(remote.read_bytes())
    assert len(actual) == len(wanted) and wanted == {a["name"]: (a["size"], a.get("digest"), a["state"]) for a in actual}
    return {"uploadedAssetsVerified": sorted(wanted)}


if __name__ == "__main__":
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("--verify-upload", action="store_true")
    parser.add_argument("first", type=Path)
    parser.add_argument("second", type=Path)
    args = parser.parse_args()
    print(json.dumps(verify_upload(args.first, args.second) if args.verify_upload else assemble(args.first, args.second)))
