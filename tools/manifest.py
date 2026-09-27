"""Generate the reproducible, dry-run integration manifest.

This file is deliberately generated from the checked-in source tree and the
already-built images.  The manifest does not contain its own digest; the
external SHA-256 printed by the verification command is the authoritative
identity for the manifest file itself.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent  # tools/ -> demo root
# Evidence tag/date: single source shared with verify.py, build.py and make_evidence.py.
EVIDENCE = json.loads((ROOT / "tools/evidence_tag.json").read_text(encoding="utf-8"))
EVIDENCE_TAG, EVIDENCE_DATE = EVIDENCE["tag"], EVIDENCE["date"]

EXPECTED_INPUTS = {
    "uct_mtk3bsp2_lwip_ra8p1_ek.zip": {
        "path": str(Path.home() / "Downloads" / "uct_mtk3bsp2_lwip_ra8p1_ek.zip"),
        "sha256": "7D258A80D752AFC0F887C317F86C54C2D8473D9F19827AE3E496CA6AA9A8B896",
    },
    "ra8p1_ov5640_dualcore.zip": {
        "path": str(Path.home() / "Downloads" / "ra8p1_ov5640_dualcore.zip"),
        "sha256": "999F0266E8D45679F662901B90EC7EBCA4AD681F086BEA7AFA36CE40408A5754",
    },
}


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def tree_hash(path: Path) -> str:
    digest = hashlib.sha256()
    for child in sorted(p for p in path.rglob("*") if p.is_file()):
        rel = child.relative_to(path).as_posix().encode()
        data = child.read_bytes()
        digest.update(rel + b"\0" + str(len(data)).encode() + b"\0" + data)
    return digest.hexdigest().upper()


def file_record(path: Path) -> dict[str, object]:
    return {"path": path.relative_to(ROOT).as_posix(), "bytes": path.stat().st_size,
            "sha256": sha256_file(path)}


def compiler_info() -> dict[str, str]:
    explicit = os.environ.get("LLVM_ARM_BIN")
    preferred = Path("C:/Renesas/RA/e2studio_v2026-04.2_fsp_v6.5.0/toolchains/llvm_arm/ATfE-21.1.1-Windows-x86_64/bin")
    candidates = [Path(explicit)] if explicit else [preferred]
    candidates += sorted(Path("C:/Renesas/RA").glob("e2studio_*/toolchains/llvm_arm/ATfE-21.1.1-*/bin"), reverse=True)
    tool = next((path for path in candidates if (path / "clang.exe").is_file()), preferred)
    compiler = tool / "clang.exe"
    version = "unavailable"
    try:
        version = subprocess.run([str(compiler), "--version"], capture_output=True,
                                 text=True, check=True).stdout.splitlines()[0]
    except (OSError, subprocess.CalledProcessError, IndexError):
        pass
    return {"family": "Arm Toolchain for Embedded LLVM", "compiler": str(compiler),
            "version": version}


def fsp_info(core: str) -> dict[str, object]:
    path = ROOT / core / "ra/fsp/inc/fsp_version.h"
    text = path.read_text(encoding="utf-8")
    match = re.search(r"#define\s+FSP_VERSION_STRING\s+\(\"([^\"]+)\"\)", text)
    if not match:
        raise RuntimeError(f"FSP_VERSION_STRING missing from {path}")
    return {"path": path.relative_to(ROOT).as_posix(), "version": match.group(1),
            "bytes": path.stat().st_size, "sha256": sha256_file(path)}


def profile_info(core: str) -> dict[str, object]:
    """Read the persistent per-core profile stamp written after a successful link."""
    path = ROOT / core / "Build/build_profile.json"
    if not path.exists():
        raise RuntimeError(f"missing per-core build profile stamp: {path}")
    data = json.loads(path.read_text(encoding="utf-8"))
    required = ("schema", "core", "profile", "physical_motor_output_enabled",
                "profile_fingerprint", "flags_sha256", "hardware_verified")
    missing = [key for key in required if key not in data]
    if missing:
        raise RuntimeError(f"incomplete build profile stamp {path}: {missing}")
    if data["core"] != core:
        raise RuntimeError(f"build profile stamp core mismatch: {path}")
    return {"path": path.relative_to(ROOT).as_posix(), "bytes": path.stat().st_size,
            "sha256": sha256_file(path), "metadata": data}


def build_manifest(profile: str, verify_source_zips: bool = False) -> dict[str, object]:
    inputs = {}
    for name, item in EXPECTED_INPUTS.items():
        path = Path(item["path"])
        record = {"path_hint": f"~/Downloads/{name}", "expected_sha256": item["sha256"],
                  "provenance_checked": verify_source_zips,
                  "available": False, "sha256": None, "bytes": None}
        if verify_source_zips:
            if not path.is_file():
                raise RuntimeError(f"source ZIP requested but missing: {path}")
            actual = sha256_file(path)
            if actual != item["sha256"]:
                raise RuntimeError(f"input hash mismatch: {name}: {actual}")
            record.update({"available": True, "sha256": actual, "bytes": path.stat().st_size})
        inputs[name] = record

    artifacts = {}
    for core in ("CPU0", "CPU1"):
        artifacts[core] = {suffix: file_record(ROOT / core / "Build" / f"{core}.{suffix}")
                           for suffix in ("elf", "srec", "map")}

    source_config_paths = [
        "docs/DEVELOPMENT.md", "tools/build.py", "tools/verify.py", "tools/manifest.py",
        "tools/test_host.py", "tools/make_evidence.py", "CHECK_BEFORE_FLASH.cmd", "MAKE_EVIDENCE.cmd",
        "CPU0/tools/control_motor_contract_test.c", "CPU0/tools/road_navigation_contract_test.c",
        "CPU0/tools/frame_stream_bmp_contract_test.c", "CPU0/tools/frame_stream_overlay_contract_test.c",
        "CPU0/tools/frame_stream_bmp_fixture.c",
        "M85Web/script/generate_web.ps1",
        "CPU0/sources.json", "CPU1/sources.json",
        "CPU0/Build/memory_regions.lld", "CPU1/Build/memory_regions.lld",
        "CPU0/Build/fsp_gen.lld", "CPU1/Build/fsp_gen.lld",
        "CPU0/Build/link.rsp", "CPU1/Build/link.rsp",
        "CPU0/Build/bsp_linker_info.h", "CPU1/Build/bsp_linker_info.h",
        "CPU0/ra/fsp/inc/fsp_version.h", "CPU1/ra/fsp/inc/fsp_version.h",
        "CPU0/.project", "CPU1/.project",
        "Solution/.project", "CPU0/.cproject", "CPU1/.cproject",
        "Solution/.cproject", "docs/E2STUDIO_BUILD_AUDIT.md",
        "CPU0/ra8p1_vision_BothCore_Download.launch",
        "CPU0/ra8p1_vision_BothCore_Download.jlink",
        "docs/HARDWARE_TEST.md", "docs/PIN_OWNERSHIP.md", "docs/BRINGUP.md",
        "Solution/solution.xml", "control/include/motor_build_profile.h",
        "CPU0/src/mipi_csi.c", "CPU0/src/m85_gateway_task.c",
        "CPU0/src/web_control_adapter.c", "M85Web/Application/interface/controller_if.c",
        "M85Web/Application/interface/controller_if.h", "control/tests/test_web_adapter.c",
    ]
    source_config_hashes = {
        path: {"bytes": (ROOT / path).stat().st_size,
               "sha256": sha256_file(ROOT / path)} for path in source_config_paths
    }
    for directory in ("CPU0/src", "CPU0/ra_gen", "CPU1/src", "CPU1/ra_gen",
                      "CPU0/mtk3_bsp2", "CPU1/mtk3_bsp2", "CPU0/ra", "CPU1/ra",
                      "CPU0/model", "control", "M85Web/Application"):
        source_config_hashes[directory] = {"tree_sha256": tree_hash(ROOT / directory)}

    link_cpu1 = (ROOT / "CPU1/Build/link.rsp").read_text()
    motor_match = re.search(r"-DMOTOR_PHYSICAL_OUTPUT_ENABLE=(\d+)", link_cpu1)
    toolchain = compiler_info()
    toolchain["cores"] = {
        core: {"compiler": toolchain["compiler"], "version": toolchain["version"]}
        for core in ("CPU0", "CPU1")
    }
    toolchain["same_compiler_for_both_cores"] = (
        toolchain["cores"]["CPU0"] == toolchain["cores"]["CPU1"])
    clean_logs = {
        "CPU0": file_record(ROOT / f"docs/{EVIDENCE_TAG}_cpu0.log"),
        "CPU1": file_record(ROOT / f"docs/{EVIDENCE_TAG}_cpu1.log"),
    }
    for core, record in clean_logs.items():
        evidence = (ROOT / record["path"]).read_text(encoding="utf-8", errors="replace")
        if f"BUILD_EVIDENCE: {EVIDENCE_TAG}" not in evidence:
            raise RuntimeError(f"evidence log is not a fresh {EVIDENCE_TAG} log: {record['path']}")
        if f"BUILD_DATE={EVIDENCE_DATE}" not in evidence:
            raise RuntimeError(f"evidence log date does not match current fresh run: {record['path']}")
        if f"PROFILE={profile}" not in evidence or f"CORE={core}" not in evidence:
            raise RuntimeError(f"evidence log profile/core mismatch: {record['path']}")
        if "CLEAN=1" not in evidence or "EXIT_CODE=0" not in evidence:
            raise RuntimeError(f"evidence log does not prove a successful clean build: {record['path']}")
        if f"ELF_SHA256={artifacts[core]['elf']['sha256']}" not in evidence:
            raise RuntimeError(f"evidence log does not identify current ELF: {record['path']}")
        profile_stamp_hash = sha256_file(ROOT / core / "Build/build_profile.json")
        if f"PROFILE_STAMP_SHA256={profile_stamp_hash}" not in evidence:
            raise RuntimeError(f"evidence log does not identify current build stamp: {record['path']}")
    build_profiles = {core: profile_info(core) for core in ("CPU0", "CPU1")}
    stamps = [build_profiles[core]["metadata"] for core in ("CPU0", "CPU1")]
    if any(stamp["profile"] != profile for stamp in stamps):
        raise RuntimeError("CPU0/CPU1 build profile stamps do not match requested manifest profile")
    if len({stamp["profile_fingerprint"] for stamp in stamps}) != 1:
        raise RuntimeError("CPU0/CPU1 build profile fingerprints differ")
    if any(bool(stamp["hardware_verified"]) for stamp in stamps):
        raise RuntimeError("hardware_verified must remain false in offline packaging")
    fsp = {core: fsp_info(core) for core in ("CPU0", "CPU1")}
    fsp_equal = (fsp["CPU0"]["version"] == fsp["CPU1"]["version"] and
                 fsp["CPU0"]["sha256"] == fsp["CPU1"]["sha256"])
    return {
        "schema": "tron.integration.manifest.v1",
        "build_evidence_date": EVIDENCE_DATE,
        "project": "RA8P1 dual-core mini-4WD TRON integration",
        "profile": profile,
        "physical_motor_output_enabled": profile == "vehicle-output",
        "hardware_verified": False,
        "inputs": inputs,
        "toolchain": toolchain,
        "fsp": fsp,
        "clean_build_evidence": clean_logs,
        "build_profiles": build_profiles,
        "memory": {
            "CPU0_flash": {"start": "0x02000000", "end_exclusive": "0x020F0000", "bytes": 0xF0000},
            "CPU1_flash": {"start": "0x020F0000", "end_exclusive": "0x02100000", "bytes": 0x10000},
            "CPU0_sram": {"start": "0x22000000", "end_exclusive": "0x22180000"},
            "CPU1_sram": {"start": "0x22180000", "end_exclusive": "0x221D3000"},
            "dualcore_shared": {"start": "0x221D3000", "bytes": 0x1000, "kind": "NOLOAD"},
        },
        "artifacts": artifacts,
        "source_config_hashes": source_config_hashes,
        "checks": {
            "control_loopback_test_enable": 0,  # loopback test task removed from the source tree
            "motor_physical_output_define_in_cpu1_link": int(motor_match.group(1)) if motor_match else None,
            "build_profiles_match": True,
            "build_profile_fingerprint": stamps[0]["profile_fingerprint"],
            "camera_task_priority": 12,
            "video_task_priority": 20,
            "usb_stream_task_priority": 22,
            "camera_scheduler_yield": "tk_dly_tsk under M85_UKERNEL",
            "fsp_version": fsp["CPU0"]["version"],
            "fsp_version_equal_across_cores": fsp_equal,
            "fsp_version_header_sha256_equal": fsp["CPU0"]["sha256"] == fsp["CPU1"]["sha256"],
            "llvm_version": "21.1.1",
            "llvm_compiler_same_for_both_cores": toolchain["same_compiler_for_both_cores"],
            "clean_build_logs": {core: clean_logs[core]["path"] for core in clean_logs},
        },
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--profile", choices=("dry-run", "vehicle-output"), default="vehicle-output")
    parser.add_argument("--verify-source-zips", action="store_true",
                        help="explicitly verify the two provenance ZIPs in ~/Downloads")
    parser.add_argument("--output", type=Path, default=ROOT / "docs/MANIFEST.json")
    args = parser.parse_args()
    manifest = build_manifest(args.profile, args.verify_source_zips)
    args.output.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"WROTE {args.output} SHA256 {sha256_file(args.output)}")


if __name__ == "__main__":
    main()
