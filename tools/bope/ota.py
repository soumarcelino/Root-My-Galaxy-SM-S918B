"""Samsung block-OTA inspection and boot patch extraction."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import re
import zipfile

from .errors import BopeError


METADATA_NAMES = (
    "META-INF/com/android/metadata",
    "META-INF/com/google/android/metadata",
)
UPDATER_NAMES = (
    "META-INF/com/google/android/updater-script",
    "META-INF/com/android/updater-script",
)


@dataclass(frozen=True)
class OtaInfo:
    path: Path
    device: str
    pre_build: str
    post_build: str
    pre_fingerprint: str
    post_fingerprint: str
    patch_entry: str
    source_size: int
    source_sha1: str
    target_size: int
    target_sha1: str
    metadata: dict[str, str]

    @property
    def slug(self) -> str:
        match = re.search(r"([A-Z0-9]{4})$", self.post_build)
        if not match:
            raise BopeError(f"cannot derive target slug from {self.post_build!r}")
        return match.group(1).lower()

    @property
    def source_slug(self) -> str:
        match = re.search(r"([A-Z0-9]{4})$", self.pre_build)
        if not match:
            raise BopeError(f"cannot derive source slug from {self.pre_build!r}")
        return match.group(1).lower()

    @property
    def model(self) -> str:
        match = re.match(r"(S\d{3}[A-Z])", self.post_build)
        return f"SM-{match.group(1)}" if match else "UNKNOWN"


def _read_first(archive: zipfile.ZipFile, names: tuple[str, ...]) -> tuple[str, str]:
    for name in names:
        try:
            return name, archive.read(name).decode(errors="replace")
        except KeyError:
            continue
    raise BopeError(f"OTA does not contain any of: {', '.join(names)}")


def _metadata(text: str) -> dict[str, str]:
    values: dict[str, str] = {}
    for line in text.splitlines():
        key, separator, value = line.partition("=")
        if separator:
            values[key.strip()] = value.strip()
    return values


def inspect_ota(path: Path) -> OtaInfo:
    if not path.is_file():
        raise BopeError(f"OTA archive does not exist: {path}")
    try:
        with zipfile.ZipFile(path) as archive:
            _, metadata_text = _read_first(archive, METADATA_NAMES)
            _, updater = _read_first(archive, UPDATER_NAMES)
            metadata = _metadata(metadata_text)

            patch_calls: list[tuple[str, str]] = []
            for call in re.findall(
                r"(?<!check)patch_partition\((.*?)\)\s*\|\|",
                updater,
                re.DOTALL,
            ):
                extracted = re.search(
                    r'package_extract_file\(\s*"([^"]+)"\s*\)', call
                )
                if extracted and Path(extracted.group(1)).name.lower() == "boot.img.p":
                    patch_calls.append((call, extracted.group(1)))
            if len(patch_calls) != 1:
                raise BopeError(
                    f"expected one boot patch operation, found {len(patch_calls)}"
                )
            call, patch_entry = patch_calls[0]
            specs = re.findall(r":(\d+):([0-9a-fA-F]{40})", call)
            if len(specs) < 2:
                raise BopeError("could not parse source/target boot hashes from updater-script")
            (target_size, target_sha1), (source_size, source_sha1) = specs[:2]
            if patch_entry not in archive.namelist():
                raise BopeError(f"boot patch is referenced but absent: {patch_entry}")
    except zipfile.BadZipFile as error:
        raise BopeError(f"invalid OTA ZIP: {path}") from error

    required = (
        "pre-build-incremental",
        "post-build-incremental",
        "pre-build",
        "post-build",
        "pre-device",
    )
    missing = [key for key in required if not metadata.get(key)]
    if missing:
        raise BopeError(f"OTA metadata is missing: {', '.join(missing)}")
    return OtaInfo(
        path=path.resolve(),
        device=metadata["pre-device"],
        pre_build=metadata["pre-build-incremental"],
        post_build=metadata["post-build-incremental"],
        pre_fingerprint=metadata["pre-build"],
        post_fingerprint=metadata["post-build"],
        patch_entry=patch_entry,
        source_size=int(source_size),
        source_sha1=source_sha1.lower(),
        target_size=int(target_size),
        target_sha1=target_sha1.lower(),
        metadata=metadata,
    )


def extract_patch(info: OtaInfo, destination: Path) -> Path:
    destination.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(info.path) as archive:
        with archive.open(info.patch_entry) as source, destination.open("wb") as target:
            while chunk := source.read(1024 * 1024):
                target.write(chunk)
    return destination
