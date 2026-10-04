"""Detect the LinuxCNC HAL API and validate module pin symbols."""
from pathlib import Path
import re


LEGACY_PIN_SYMBOLS = {
    "hal_pin_bit_newf",
    "hal_pin_float_newf",
    "hal_pin_s32_newf",
    "hal_pin_u32_newf",
}
API1_PIN_SYMBOLS = {
    "hal_pin_new_bool",
    "hal_pin_new_real",
    "hal_pin_new_si32",
    "hal_pin_new_ui32",
}


def detect_hal_api(header):
    """Return the numeric API exposed by hal.h; no version macro means API 0."""
    header = Path(header)
    contents = header.read_text()
    definition = re.search(
        r"^[ \t]*#[ \t]*define[ \t]+HAL_API_VERSION[ \t]+([^\r\n]+)",
        contents,
        re.MULTILINE,
    )
    if definition is None:
        return 0
    value = re.match(r"[ \t]*\(?[ \t]*([0-9]+)", definition.group(1))
    if value is None:
        raise AssertionError(f"Cannot parse HAL_API_VERSION in {header}")
    return int(value.group(1))


def assert_pin_symbols(nm_output, api_version):
    """Require the pin constructors selected by hal_compat.h for this API."""
    symbols = {line.split()[-1] for line in nm_output.splitlines() if line.split()}
    if api_version >= 1:
        expected, forbidden = API1_PIN_SYMBOLS, LEGACY_PIN_SYMBOLS
    else:
        expected, forbidden = LEGACY_PIN_SYMBOLS, API1_PIN_SYMBOLS
    missing = expected - symbols
    unexpected = forbidden & symbols
    assert not missing, f"HAL API {api_version}: missing symbols {sorted(missing)}\n{nm_output}"
    assert not unexpected, (
        f"HAL API {api_version}: unexpected symbols {sorted(unexpected)}\n{nm_output}"
    )
