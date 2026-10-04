#!/usr/bin/env python3
"""Regression coverage for HAL API detection and module symbol validation."""
from pathlib import Path
import tempfile

from hal_api_symbols import API1_PIN_SYMBOLS, LEGACY_PIN_SYMBOLS
from hal_api_symbols import assert_pin_symbols, detect_hal_api


def nm_output(symbols):
    return "".join(f"                 U {symbol}\n" for symbol in sorted(symbols))


def must_reject(symbols, api_version):
    try:
        assert_pin_symbols(nm_output(symbols), api_version)
    except AssertionError:
        return
    raise AssertionError(f"HAL API {api_version} accepted the wrong pin symbols")


with tempfile.TemporaryDirectory(prefix="hal-api-headers-") as temporary:
    root = Path(temporary)
    api0 = root / "api0" / "hal.h"
    api0.parent.mkdir()
    api0.write_text("int hal_pin_bit_newf(void);\n")
    api1 = root / "api1" / "hal.h"
    api1.parent.mkdir()
    api1.write_text("#define HAL_API_VERSION 1\nint hal_pin_new_bool(void);\n")

    assert detect_hal_api(api0) == 0
    assert detect_hal_api(api1) == 1
    assert_pin_symbols(nm_output(LEGACY_PIN_SYMBOLS), detect_hal_api(api0))
    assert_pin_symbols(nm_output(API1_PIN_SYMBOLS), detect_hal_api(api1))
    must_reject(API1_PIN_SYMBOLS, detect_hal_api(api0))
    must_reject(LEGACY_PIN_SYMBOLS, detect_hal_api(api1))
    must_reject(API1_PIN_SYMBOLS | {"hal_pin_bit_newf"}, detect_hal_api(api1))

print("PASS: HAL API header detection and pin symbol expectations")
