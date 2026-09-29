"""Samsung Odin factory-firmware inspection and boot extraction."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import re
import shutil
import tarfile
import zipfile

from .common import executable, run
from .errors import BopeError


AP_NAME = re.compile(
    r"^AP_(?P<build>S\d{3}[A-Z][A-Z0-9]+)_.*_meta_OS(?P<os>\d+)\.tar\.md5$"
)


@dataclass(frozen=True)
class FactoryInfo:
    path: Path
    ap_entry: str
    boot_entry: str
    device: str
    pre_build: str
    post_build: str
    pre_fingerprint: str
    post_fingerprint: str
    android_version: str

    @property
    def slug(self) -> str:
        match = re.search(r"([A-Z0-9]{4})$", self.post_build)
        if not match:
            raise BopeError(f"cannot derive target slug from {self.post_build!r}")
        return match.group(1).lower()

    @property
    def model(self) -> str:
        match = re.match(r"(S\d{3}[A-Z])", self.post_build)
        return f"SM-{match.group(1)}" if match else "UNKNOWN"


def inspect_factory(
    path: Path,
    *,
    donor_build: str,
    donor_fingerprint: str,
    target_fingerprint: str,
    device: str,
) -> FactoryInfo:
    if not path.is_file():
        raise BopeError(f"factory firmware archive does not exist: {path}")
    try:
        with zipfile.ZipFile(path) as archive:
            candidates: list[tuple[str, re.Match[str]]] = []
            for name in archive.namelist():
                match = AP_NAME.match(Path(name).name)
                if match:
                    candidates.append((name, match))
    except zipfile.BadZipFile as error:
        raise BopeError(f"invalid factory firmware ZIP: {path}") from error
    if len(candidates) != 1:
        raise BopeError(
            f"expected one Samsung AP tar.md5, found {len(candidates)}"
        )
    ap_entry, match = candidates[0]
    build = match.group("build")
    android_version = match.group("os")
    expected_fragment = f"/{build}:user/release-keys"
    if expected_fragment not in target_fingerprint:
        raise BopeError(
            "target fingerprint does not contain the exact factory build and "
            "release-keys suffix"
        )
    if f":{android_version}/" not in target_fingerprint:
        raise BopeError(
            f"target fingerprint Android version does not match meta_OS{android_version}"
        )
    if donor_build not in donor_fingerprint:
        raise BopeError("donor fingerprint does not contain the donor build")
    fingerprint_device = target_fingerprint.split(":", 1)[0].rsplit("/", 1)[-1]
    if fingerprint_device != device:
        raise BopeError(
            f"fingerprint device {fingerprint_device!r} does not match {device!r}"
        )
    return FactoryInfo(
        path=path.resolve(),
        ap_entry=ap_entry,
        boot_entry="boot.img.lz4",
        device=device,
        pre_build=donor_build,
        post_build=build,
        pre_fingerprint=donor_fingerprint,
        post_fingerprint=target_fingerprint,
        android_version=android_version,
    )


def extract_factory_member(
    info: FactoryInfo,
    destination: Path,
    *,
    member_name: str,
) -> Path:
    destination.parent.mkdir(parents=True, exist_ok=True)
    found = False
    try:
        with zipfile.ZipFile(info.path) as archive:
            with archive.open(info.ap_entry) as ap_stream:
                with tarfile.open(fileobj=ap_stream, mode="r|") as ap:
                    for member in ap:
                        normalized = member.name.removeprefix("./")
                        if normalized != member_name:
                            continue
                        if not member.isfile():
                            raise BopeError(
                                f"factory AP member is not a regular file: {member_name}"
                            )
                        source = ap.extractfile(member)
                        if source is None:
                            raise BopeError(
                                f"factory AP member could not be opened: {member_name}"
                            )
                        with source, destination.open("wb") as output:
                            shutil.copyfileobj(source, output, 1024 * 1024)
                        found = True
                        break
    except (zipfile.BadZipFile, tarfile.TarError, OSError) as error:
        destination.unlink(missing_ok=True)
        raise BopeError(f"could not stream {member_name} from factory AP: {error}") from error
    if not found:
        destination.unlink(missing_ok=True)
        raise BopeError(f"factory AP does not contain {member_name}")
    return destination


def extract_factory_boot(
    info: FactoryInfo,
    compressed: Path,
    output: Path,
    *,
    lz4: Path | None = None,
) -> Path:
    extract_factory_member(info, compressed, member_name=info.boot_entry)
    decoder = executable("lz4", explicit=lz4)
    output.parent.mkdir(parents=True, exist_ok=True)
    run([decoder, "-d", "-f", compressed, output])
    with output.open("rb") as stream:
        magic = stream.read(8)
    if magic != b"ANDROID!":
        output.unlink(missing_ok=True)
        raise BopeError("decompressed factory boot image has invalid Android magic")
    return output
