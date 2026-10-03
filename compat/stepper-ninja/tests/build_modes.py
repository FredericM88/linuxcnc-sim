#!/usr/bin/env python3
"""Exercise real module builds, path quoting and isolated RIP selection failures."""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
for option in ("cmake", "compiler", "upstream", "modinc", "includes", "linuxcnc-source"):
    parser.add_argument(f"--{option}", required=True)
args = parser.parse_args()
compat = Path(__file__).resolve().parents[1]


def run(command, expected=None):
    result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if expected is None:
        assert result.returncode == 0, result.stdout
    else:
        assert result.returncode != 0 and expected in result.stdout, result.stdout
    return result.stdout


def configure(build, source="", expected=None, modinc=None, upstream=None):
    return run([args.cmake, "-S", str(compat), "-B", str(build),
                "-DBUILD_TESTING=OFF", f"-DCMAKE_C_COMPILER={args.compiler}",
                f"-DSTEPPER_NINJA_SOURCE={upstream or args.upstream}",
                f"-DLINUXCNC_SOURCE_DIR={source}",
                f"-DLINUXCNC_MODINC={modinc or args.modinc}",
                f"-DLINUXCNC_INCLUDE_DIR={args.includes}"], expected)


with tempfile.TemporaryDirectory(prefix="hal-build-modes-") as temporary:
    root = Path(temporary)
    upstream = root / "upstream with spaces"
    upstream.symlink_to(Path(args.upstream).resolve(), target_is_directory=True)
    build = root / "build with spaces"
    if args.linuxcnc_source:
        source = Path(args.linuxcnc_source).resolve()
        alias = root / "LinuxCNC source with spaces"
        alias.symlink_to(source, target_is_directory=True)
        # Deliberately pass stale installed-mode cache values: RIP must ignore them.
        configure(build, alias, modinc="/not/the/requested/Makefile.modinc", upstream=upstream)
    else:
        # GNU make's include directive must escape spaces, not use shell quoting.
        modinc = root / "rules with spaces" / "Makefile.modinc"
        modinc.parent.mkdir()
        shutil.copyfile(args.modinc, modinc)
        configure(build, modinc=modinc, upstream=upstream)
    run([args.cmake, "--build", str(build), "-j2"])
    module = build / "stepgen-ninja.so"
    assert module.is_file()
    symbols = run(["nm", "-D", str(module)])
    assert " T rtapi_app_main" in symbols and " T rtapi_app_exit" in symbols
    if args.linuxcnc_source:
        for suffix in ("bool", "real", "si32", "ui32"):
            assert f" U hal_pin_new_{suffix}\n" in symbols, symbols
        assert "_newf" not in symbols, symbols

        # A private source-header fixture leaves the real configured tree intact.
        fixture = root / "unconfigured"
        for directory in ("hal", "rtapi"):
            target = fixture / "src" / directory
            target.mkdir(parents=True)
            for header in (source / "src" / directory).glob("*.h"):
                shutil.copyfile(header, target / header.name)
        check_build = root / "compile-check"
        output = configure(check_build, fixture)
        assert "Compile check only" in output, output
        run([args.cmake, "--build", str(check_build), "-j2"])
        assert list(check_build.rglob("stepgen-ninja.c.o"))
        assert not (check_build / "stepgen-ninja.so").exists()

        modinc = fixture / "src/Makefile.modinc"
        modinc.symlink_to(args.modinc)
        configure(root / "foreign-link", fixture, "must belong to the requested tree")
        modinc.unlink()
        # A copied modinc still belongs to its original configured root.
        original = Path(args.modinc).read_text()
        modinc.write_text(original)
        configure(root / "foreign-copy", fixture, "not configured for the requested RIP tree")
        own = original.replace(str(source), str(fixture))
        modinc.write_text(own)
        # Reuse the previous build: configure detects a partial configured tree.
        output = configure(check_build, fixture)
        assert "generated headers are incomplete" in output, output
        run([args.cmake, "--build", str(check_build), "-j2"])
        assert not (check_build / "stepgen-ninja.so").exists()
        modinc.write_text(own + "\nRTLIBDIR := /foreign/rtlib\n")
        configure(root / "foreign-rtlib", fixture, "not configured for the requested RIP tree")
        modinc.write_text(own)
        include = fixture / "include"
        include.mkdir()
        for name in ("hal.h", "rtapi.h"):
            (include / name).symlink_to(source / "include" / name)
        configure(root / "foreign-headers", fixture, "points outside the requested tree")

print("PASS: module build, paths with spaces and applicable RIP selection checks")
