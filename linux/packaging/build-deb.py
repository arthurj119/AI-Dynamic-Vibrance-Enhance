#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Build and verify Debian/Ubuntu .deb and portable Linux .tar.gz bundles using Python stdlib."""

from __future__ import annotations

import argparse
import gzip
import hashlib
import io
import math
import shutil
import tarfile
import time
from pathlib import Path

PKG_NAME = "ai-dynamic-vibrance-enhance"
VERSION = "2.0.0"
FIXED_MTIME = 1795406400  # Deterministic build timestamp


def _norm_lf(path: Path) -> bytes:
    return path.read_bytes().replace(b"\r\n", b"\n")


def _add_tar_dir(tf: tarfile.TarFile, arcname: str, mode: int = 0o755) -> None:
    ti = tarfile.TarInfo(arcname)
    ti.type = tarfile.DIRTYPE
    ti.mode = mode
    ti.uid = 0
    ti.gid = 0
    ti.uname = "root"
    ti.gname = "root"
    ti.mtime = FIXED_MTIME
    tf.addfile(ti)


def _add_tar_bytes(tf: tarfile.TarFile, arcname: str, data: bytes, mode: int) -> None:
    ti = tarfile.TarInfo(arcname)
    ti.type = tarfile.REGTYPE
    ti.size = len(data)
    ti.mode = mode
    ti.uid = 0
    ti.gid = 0
    ti.uname = "root"
    ti.gname = "root"
    ti.mtime = FIXED_MTIME
    tf.addfile(ti, io.BytesIO(data))


def _make_gzip_tar(entries: list[tuple[str, bytes | None, int]]) -> bytes:
    """Create a deterministic gzip-compressed tar archive."""
    tar_buf = io.BytesIO()
    with tarfile.open(fileobj=tar_buf, mode="w", format=tarfile.USTAR_FORMAT) as tf:
        for arcname, payload, mode in entries:
            if payload is None:
                _add_tar_dir(tf, arcname, mode)
            else:
                _add_tar_bytes(tf, arcname, payload, mode)

    gz_buf = io.BytesIO()
    with gzip.GzipFile(fileobj=gz_buf, mode="wb", mtime=FIXED_MTIME) as gz:
        gz.write(tar_buf.getvalue())
    return gz_buf.getvalue()


def _ar_member(name: str, data: bytes, mode: int = 0o100644) -> bytes:
    ar_name = f"{name}/"
    if len(ar_name) > 16:
        raise ValueError(f"ar member name too long: {name}")
    header = (
        f"{ar_name:<16}"
        f"{FIXED_MTIME:<12}"
        f"{0:<6}"
        f"{0:<6}"
        f"{oct(mode)[2:]:<8}"
        f"{len(data):<10}"
        f"`\n"
    ).encode("ascii")
    if len(header) != 60:
        raise AssertionError(f"Invalid ar header length {len(header)}")
    pad = b"\n" if (len(data) % 2 == 1) else b""
    return header + data + pad


def build_artifacts(repo_root: Path) -> tuple[Path, Path]:
    linux_dir = repo_root / "linux"
    dist_dir = linux_dir / "dist"
    dist_dir.mkdir(parents=True, exist_ok=True)

    hub_bytes = _norm_lf(linux_dir / "adv_control_hub.py")
    fx_bytes = _norm_lf(repo_root / "shaders" / "AIDynamicVibranceEnhance.fx")
    fxh_bytes = _norm_lf(linux_dir / "shaders" / "ReShade.fxh")
    desktop_bytes = _norm_lf(linux_dir / "assets" / f"{PKG_NAME}.desktop")
    svg_bytes = _norm_lf(linux_dir / "assets" / f"{PKG_NAME}.svg")
    license_bytes = _norm_lf(repo_root / "LICENSE")
    install_bytes = _norm_lf(linux_dir / "install.sh")
    uninstall_bytes = _norm_lf(linux_dir / "uninstall.sh")
    control_base = _norm_lf(linux_dir / "packaging" / "debian" / "control").decode("utf-8").rstrip("\n")

    # 1. Build Debian/Ubuntu .deb package (Ubuntu / Pop!_OS / Linux Mint)
    data_files: list[tuple[str, bytes, int]] = [
        (f"usr/bin/{PKG_NAME}", hub_bytes, 0o755),
        (f"usr/share/{PKG_NAME}/shaders/AIDynamicVibranceEnhance.fx", fx_bytes, 0o644),
        (f"usr/share/{PKG_NAME}/shaders/ReShade.fxh", fxh_bytes, 0o644),
        (f"usr/share/applications/{PKG_NAME}.desktop", desktop_bytes, 0o644),
        (f"usr/share/icons/hicolor/scalable/apps/{PKG_NAME}.svg", svg_bytes, 0o644),
        (f"usr/share/doc/{PKG_NAME}/copyright", license_bytes, 0o644),
    ]

    data_dirs = [
        "./",
        "./usr/",
        "./usr/bin/",
        "./usr/share/",
        f"./usr/share/{PKG_NAME}/",
        f"./usr/share/{PKG_NAME}/shaders/",
        "./usr/share/applications/",
        "./usr/share/icons/",
        "./usr/share/icons/hicolor/",
        "./usr/share/icons/hicolor/scalable/",
        "./usr/share/icons/hicolor/scalable/apps/",
        "./usr/share/doc/",
        f"./usr/share/doc/{PKG_NAME}/",
    ]

    data_entries: list[tuple[str, bytes | None, int]] = [(d, None, 0o755) for d in data_dirs]
    md5_lines: list[str] = []
    total_bytes = 0
    for rel, content, mode in data_files:
        data_entries.append((f"./{rel}", content, mode))
        md5_hex = hashlib.md5(content).hexdigest()
        md5_lines.append(f"{md5_hex}  {rel}")
        total_bytes += len(content)

    installed_kb = max(1, math.ceil(total_bytes / 1024))
    control_text = f"{control_base}\nInstalled-Size: {installed_kb}\n".encode("utf-8")
    md5sums_text = ("\n".join(md5_lines) + "\n").encode("utf-8")
    postinst_text = (
        "#!/bin/sh\n"
        "set -e\n"
        "if command -v update-desktop-database >/dev/null 2>&1; then\n"
        "  update-desktop-database /usr/share/applications >/dev/null 2>&1 || true\n"
        "fi\n"
        "if command -v gtk-update-icon-cache >/dev/null 2>&1; then\n"
        "  gtk-update-icon-cache -f -t /usr/share/icons/hicolor >/dev/null 2>&1 || true\n"
        "fi\n"
        "exit 0\n"
    ).encode("utf-8")

    control_entries: list[tuple[str, bytes | None, int]] = [
        ("./", None, 0o755),
        ("./control", control_text, 0o644),
        ("./md5sums", md5sums_text, 0o644),
        ("./postinst", postinst_text, 0o755),
    ]

    data_tar_gz = _make_gzip_tar(data_entries)
    control_tar_gz = _make_gzip_tar(control_entries)

    ar_bytes = (
        b"!<arch>\n"
        + _ar_member("debian-binary", b"2.0\n", 0o100644)
        + _ar_member("control.tar.gz", control_tar_gz, 0o100644)
        + _ar_member("data.tar.gz", data_tar_gz, 0o100644)
    )
    deb_path = dist_dir / f"{PKG_NAME}_{VERSION}_all.deb"
    deb_path.write_bytes(ar_bytes)

    # 2. Build universal portable Linux tarball bundle
    bundle_root = f"AIDynamicVibranceEnhance-Linux-v{VERSION}"
    readme_linux = (
        f"AI Dynamic Vibrance Enhance v{VERSION} (Linux Gaming Bundle)\n"
        "Supported Gaming Distros: Arch / CachyOS / EndeavourOS, SteamOS, Fedora / Nobara / Bazzite, Ubuntu / Pop!_OS / Mint\n\n"
        "Quick Install:\n"
        "  chmod +x install.sh\n"
        "  ./install.sh\n\n"
        "Uninstall:\n"
        "  ./uninstall.sh\n\n"
        "Steam Launch Option:\n"
        "  ENABLE_VKBASALT=1 %command%\n"
    ).encode("utf-8")

    bundle_entries: list[tuple[str, bytes | None, int]] = [
        (f"{bundle_root}/", None, 0o755),
        (f"{bundle_root}/shaders/", None, 0o755),
        (f"{bundle_root}/assets/", None, 0o755),
        (f"{bundle_root}/install.sh", install_bytes, 0o755),
        (f"{bundle_root}/uninstall.sh", uninstall_bytes, 0o755),
        (f"{bundle_root}/adv_control_hub.py", hub_bytes, 0o755),
        (f"{bundle_root}/shaders/AIDynamicVibranceEnhance.fx", fx_bytes, 0o644),
        (f"{bundle_root}/shaders/ReShade.fxh", fxh_bytes, 0o644),
        (f"{bundle_root}/assets/{PKG_NAME}.desktop", desktop_bytes, 0o644),
        (f"{bundle_root}/assets/{PKG_NAME}.svg", svg_bytes, 0o644),
        (f"{bundle_root}/LICENSE", license_bytes, 0o644),
        (f"{bundle_root}/README-LINUX.txt", readme_linux, 0o644),
    ]
    tar_path = dist_dir / f"AIDynamicVibranceEnhance-Linux-v{VERSION}.tar.gz"
    tar_path.write_bytes(_make_gzip_tar(bundle_entries))
    return deb_path, tar_path


def verify_artifacts(deb_path: Path, tar_path: Path) -> None:
    raw = deb_path.read_bytes()
    assert raw.startswith(b"!<arch>\n"), "Missing ar magic header"
    pos = 8
    members: dict[str, bytes] = {}
    while pos < len(raw):
        hdr = raw[pos:pos + 60]
        assert len(hdr) == 60 and hdr[58:60] == b"`\n", f"Corrupt ar header at {pos}"
        name = hdr[:16].decode("ascii").strip().rstrip("/")
        size = int(hdr[48:58].decode("ascii").strip())
        pos += 60
        payload = raw[pos:pos + size]
        assert len(payload) == size, f"Truncated ar member {name}"
        members[name] = payload
        pos += size + (size % 2)

    assert list(members.keys()) == ["debian-binary", "control.tar.gz", "data.tar.gz"], f"Unexpected .deb order: {list(members.keys())}"
    assert members["debian-binary"] == b"2.0\n", "Invalid debian-binary content"

    with tarfile.open(fileobj=io.BytesIO(members["control.tar.gz"]), mode="r:gz") as ctf:
        cnames = {m.name: m for m in ctf.getmembers()}
        for req in ("./control", "./md5sums", "./postinst"):
            assert req in cnames, f"Missing {req} in control.tar.gz"
        assert (cnames["./postinst"].mode & 0o777) == 0o755
        md5_map: dict[str, str] = {}
        md5_bytes = ctf.extractfile("./md5sums").read()
        assert b"\r" not in md5_bytes
        for line in md5_bytes.decode("utf-8").splitlines():
            digest, rel = line.split(None, 1)
            md5_map[rel] = digest

    with tarfile.open(fileobj=io.BytesIO(members["data.tar.gz"]), mode="r:gz") as dtf:
        dmembers = {m.name: m for m in dtf.getmembers() if m.isfile()}
        assert len(dmembers) == len(md5_map) == 6
        assert (dmembers[f"./usr/bin/{PKG_NAME}"].mode & 0o777) == 0o755
        for rel, expected_md5 in md5_map.items():
            fbytes = dtf.extractfile(f"./{rel}").read()
            assert b"\r" not in fbytes, f"CRLF detected in .deb member {rel}"
            assert hashlib.md5(fbytes).hexdigest() == expected_md5, f"MD5 mismatch for {rel}"

    with tarfile.open(tar_path, mode="r:gz") as btf:
        bmembers = {m.name: m for m in btf.getmembers() if m.isfile()}
        root = f"AIDynamicVibranceEnhance-Linux-v{VERSION}"
        for exe in ("install.sh", "uninstall.sh", "adv_control_hub.py"):
            m = bmembers[f"{root}/{exe}"]
            assert (m.mode & 0o777) == 0o755, f"{exe} is not 0755"
        for m in bmembers.values():
            fbytes = btf.extractfile(m).read()
            assert b"\r" not in fbytes, f"CRLF detected in tarball member {m.name}"


def main() -> int:
    parser = argparse.ArgumentParser(description="Build .deb and portable Linux .tar.gz packages.")
    parser.add_argument("--verify", action="store_true", help="Verify built archives byte-for-byte")
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parent.parent.parent
    deb_path, tar_path = build_artifacts(repo_root)
    print(f"[PASS] Built Debian/Ubuntu package: {deb_path} ({deb_path.stat().st_size} bytes)")
    print(f"[PASS] Built portable Linux bundle : {tar_path} ({tar_path.stat().st_size} bytes)")

    if args.verify:
        verify_artifacts(deb_path, tar_path)
        print("[PASS] Verified .deb ar/control/md5sums/data permissions & zero-CRLF tarball integrity")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
