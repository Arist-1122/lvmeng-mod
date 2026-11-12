"""Rebuild byte-identical release assets from the prior ZIP and verified ranges."""
from hashlib import file_digest, sha256
from pathlib import Path
import json
import sys
from tempfile import TemporaryFile

ROOT = Path(__file__).resolve().parent
BLOCK = 1024 * 1024
TAG = "0.1.5-BETA-V2"


def check(path, spec):
    with path.open("rb") as stream:
        if path.stat().st_size != spec["bytes"] or file_digest(stream, "sha256").hexdigest() != spec["sha256"]:
            raise ValueError(f"Digest or size mismatch for {path.name}")


def copy_range(source, output, offset, size):
    source.seek(offset)
    for _ in range((size + BLOCK - 1) // BLOCK):
        amount = min(BLOCK, size)
        block = source.read(amount)
        if len(block) != amount:
            raise ValueError("Incomplete archive range")
        output.write(block)
        size -= amount


def assemble(base, output):
    manifest = json.loads((ROOT / "manifest.json").read_text(encoding="ascii"))
    check(base, manifest["base"])
    manager, source = (manifest[key] for key in ("manager", "source"))
    if manager["filename"] != f"lvmeng-mod-manager-{TAG}.zip" or source["filename"] != f"lvmeng-mod-source-{TAG}.zip":
        raise ValueError("Unexpected asset name")
    output.mkdir(parents=True, exist_ok=True)
    manager_path, source_path = (output / row["filename"] for row in (manager, source))
    with TemporaryFile() as data, base.open("rb") as original:
        for index, part in enumerate(manifest["parts"]):
            path = ROOT / f"parts/data-{index:03}.part"
            if path.relative_to(ROOT).as_posix() != part["path"] or not 0 < part["bytes"] <= BLOCK:
                raise ValueError("Unexpected part")
            block = path.read_bytes()
            if len(block) != part["bytes"] or sha256(block).hexdigest() != part["sha256"]:
                raise ValueError(f"Invalid part {index}")
            data.write(block)
        sources = {"base": (original, manifest["base"]["bytes"]), "data": (data, data.tell())}
        with manager_path.open("xb") as stream:
            for kind, offset, size in manager["ranges"]:
                input_file, limit = sources[kind]
                if not 0 <= offset < offset + size <= limit:
                    raise ValueError("Archive range exceeds source")
                copy_range(input_file, stream, offset, size)
        if not 0 <= source["offset"] < source["offset"] + source["bytes"] == sources["data"][1]:
            raise ValueError("Source ZIP range exceeds payload")
        with source_path.open("xb") as stream:
            copy_range(data, stream, source["offset"], source["bytes"])
    check(manager_path, manager)
    check(source_path, source)
    manager_path.with_suffix(".zip.sha256").write_text(
        f"{manager['sha256']}  {manager_path.name}\n", encoding="ascii", newline="\n")
    print(f"Verified {manager_path.name} and {source_path.name}")


if __name__ == "__main__":
    assemble(Path(sys.argv[1]), Path(sys.argv[2]))
