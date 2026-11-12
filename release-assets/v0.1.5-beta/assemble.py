"""Reassemble pinned release assets without extracting the ZIPs."""

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import stat
import tempfile
import zipfile


ROOT = Path(__file__).resolve().parent
TAG = "v0.1.5-beta"
RUNTIME = f"lvmeng-mod-manager-{TAG}.zip"
SOURCES = f"lvmeng-mod-source-{TAG}.zip"
CARDS = {
    "litelisi-crossgame-2b",
    "litelisi-crossgame-kafka",
    "litelisi-crossgame-cantarella",
    "litelisi-crossgame-lyfe",
    "litelisi-starsea",
    "litelisi-redblack",
}
PRIVATE = {
    "litelisi-body-blue", "litelisi-body-red", "litelisi-crimson-vesper",
    "game-location.json", "user-data", "control", "logs", "settings.json",
    "reshade.log", "reshade.ini", ".git",
}
MIB = 1024 * 1024
ASSET_LIMIT = 2 * 1024 * MIB
PART_PATTERN = re.compile(r"parts/[a-z0-9][a-z0-9._-]*\.part", re.ASCII)


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"Duplicate JSON key {key!r}")
        result[key] = value
    return result


def json_object(raw, label):
    value = json.loads(raw, object_pairs_hook=unique_object)
    if not isinstance(value, dict):
        raise ValueError(f"{label} must be a JSON object")
    return value


def digest(value, length):
    if not isinstance(value, str) or not re.fullmatch(f"[0-9a-f]{{{length}}}", value):
        raise ValueError(f"Expected a {length}-character hexadecimal digest")
    if not value.strip("0"):
        raise ValueError("An all-zero digest is invalid")
    return value


def length(value, limit):
    if type(value) is not int or not 0 < value < limit:
        raise ValueError(f"Invalid byte count {value!r}")
    return value


def assemble_asset(spec, filename, output, used_parts):
    if not isinstance(spec, dict) or set(spec) != {"fileName", "bytes", "sha256", "parts"}:
        raise ValueError(f"Invalid specification for {filename}")
    if spec["fileName"] != filename:
        raise ValueError(f"Expected asset name {filename}")
    expected_size = length(spec["bytes"], ASSET_LIMIT)
    expected_hash = digest(spec["sha256"], 64)
    if not isinstance(spec["parts"], list) or not 1 <= len(spec["parts"]) <= 1024:
        raise ValueError(f"Invalid part list for {filename}")
    whole_hash = hashlib.sha256()
    whole_size = 0
    with output.open("xb") as archive:
        for part in spec["parts"]:
            if not isinstance(part, dict) or set(part) != {"path", "bytes", "sha256"}:
                raise ValueError(f"Invalid part record for {filename}")
            relative = part["path"]
            if not isinstance(relative, str) or not PART_PATTERN.fullmatch(relative) or relative in used_parts:
                raise ValueError(f"Unsafe or repeated part path {relative!r}")
            used_parts.add(relative)
            part_size = length(part["bytes"], 100 * MIB + 1)
            part_hash = digest(part["sha256"], 64)
            source = ROOT / relative
            if (source.parent.is_symlink() or source.is_symlink() or not source.is_file()
                    or source.stat().st_size != part_size or whole_size + part_size > expected_size):
                raise ValueError(f"Unsafe part or byte-count mismatch {relative}")
            checksum = hashlib.sha256()
            streamed_size = 0
            with source.open("rb") as stream:
                while block := stream.read(MIB):
                    streamed_size += len(block)
                    if streamed_size > part_size:
                        raise ValueError(f"Part grew while reading {relative}")
                    checksum.update(block)
                    whole_hash.update(block)
                    archive.write(block)
            if streamed_size != part_size or checksum.hexdigest() != part_hash:
                raise ValueError(f"Part SHA-256 or byte-count mismatch {relative}")
            whole_size += streamed_size
    if whole_size != expected_size or whole_hash.hexdigest() != expected_hash:
        raise ValueError(f"Complete ZIP SHA-256 or byte-count mismatch for {filename}")
    return {"fileName": filename, "bytes": whole_size, "sha256": expected_hash}


def zip_member(name):
    clean = name.removesuffix("/")
    parts = clean.split("/")
    if ("\\" in clean or ":" in clean or any(
            not part or part in (".", "..") or part.endswith((".", " "))
            or any(ord(ch) < 32 for ch in part) for part in parts)):
        raise ValueError(f"Unsafe ZIP member {name!r}")
    return PurePosixPath(clean)


def inspect_zip(path, runtime):
    with zipfile.ZipFile(path) as archive:
        entries = {}
        folders = set()
        names_folded = set()
        cards = set()
        for info in archive.infolist():
            name = zip_member(info.filename)
            folded = name.as_posix().casefold()
            if folded in names_folded or stat.S_ISLNK(info.external_attr >> 16):
                raise ValueError(f"Repeated ZIP member or symbolic link: {info.filename}")
            if any(part.casefold() in PRIVATE for part in name.parts):
                raise ValueError(f"Private state or card found: {info.filename}")
            names_folded.add(folded)
            for parent in name.parents:
                folders.add(parent.as_posix().casefold())
            if info.is_dir():
                folders.add(folded)
            else:
                entries[name.as_posix()] = info
            if runtime and name.parts[0].casefold() == "mods" and len(name.parts) >= 2:
                cards.add(name.parts[1])
        if folders.intersection(name.casefold() for name in entries):
            raise ValueError(f"Conflicting file and directory names in {path.name}")
        if runtime:
            if cards != CARDS or "LvmengModManager.exe" not in entries:
                raise ValueError(f"Unexpected runtime card list: {sorted(cards)}")
            for card in sorted(CARDS):
                manifest = f"mods/{card}/mod.json"
                if manifest not in entries or entries[manifest].file_size > 256 * 1024:
                    raise ValueError(f"Missing or oversized card manifest: {card}")
                metadata = json_object(archive.read(manifest), manifest)
                directory = zip_member(metadata["runtimeDirectory"])
                package = zip_member(metadata["packageManifest"])
                package_path = (PurePosixPath("mods", card) / directory / package).as_posix()
                if metadata.get("id") != card or package_path not in entries:
                    raise ValueError(f"Card identity or runtime package is missing: {card}")
        if archive.testzip() is not None:
            raise ValueError(f"ZIP integrity check failed for {path.name}")
    return sorted(cards)


def assemble(output):
    manifest = json_object((ROOT / "manifest.json").read_bytes(), "Release manifest")
    if (set(manifest) not in ({"version", "tag", "targetCommit", "runtime"},
                              {"version", "tag", "targetCommit", "runtime", "sources"})
            or type(manifest["version"]) is not int or manifest["version"] != 1
            or manifest["tag"] != TAG):
        raise ValueError("Unexpected release manifest version, tag, or fields")
    target = digest(manifest["targetCommit"], 40)
    if not (ROOT / "notes.md").is_file() or not (ROOT / "notes.md").stat().st_size:
        raise ValueError("Release notes are required")
    output.mkdir(parents=True, exist_ok=True)
    used_parts = set()
    checksum_name = f"{RUNTIME}.sha256"
    with tempfile.TemporaryDirectory(prefix=".assemble-", dir=output) as scratch:
        scratch = Path(scratch)
        runtime = assemble_asset(manifest["runtime"], RUNTIME, scratch / RUNTIME, used_parts)
        cards = inspect_zip(scratch / RUNTIME, runtime=True)
        (scratch / checksum_name).write_text(
            f"{runtime['sha256']}  {RUNTIME}\n", encoding="ascii", newline="\n"
        )
        result = {"tag": TAG, "targetCommit": target, "runtime": runtime, "cards": cards}
        names = [RUNTIME, checksum_name]
        if "sources" in manifest:
            result["sources"] = assemble_asset(manifest["sources"], SOURCES, scratch / SOURCES, used_parts)
            inspect_zip(scratch / SOURCES, runtime=False)
            names.append(SOURCES)
        if any((output / name).exists() or (output / name).is_symlink() for name in names):
            raise ValueError("The output already contains release assets")
        for name in names:
            (output / name).hardlink_to(scratch / name)
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    arguments = parser.parse_args()
    print(json.dumps(assemble(arguments.output), sort_keys=True))
