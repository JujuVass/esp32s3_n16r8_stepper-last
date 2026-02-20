"""
PlatformIO extra script: adds 'coredump-info' target.

Usage:
  pio run -e esp32s3_usb -t coredump-info   # Read & decode coredump from flash

Requires: pip install esp-coredump  (installed once)
"""
import os
import subprocess
import sys

Import("env")

# ---------------------------------------------------------------------------
# Paths resolved from PlatformIO environment
# ---------------------------------------------------------------------------
platform_packages = env.PioPlatform().get_package_dir("tool-xtensa-esp-elf-gdb")
gdb_path = os.path.join(platform_packages, "bin", "xtensa-esp32s3-elf-gdb.exe")

rom_elf = os.path.join(
    env.PioPlatform().get_package_dir("tool-esp-rom-elfs"),
    "esp32s3_rev0_rom.elf",
)

# Firmware ELF (built by PlatformIO)
firmware_elf = os.path.join(env.subst("$BUILD_DIR"), "firmware.elf")

# Coredump partition offset (must match default_16MB.csv)
COREDUMP_OFFSET = "0xFC0000"

# Serial port (from platformio.ini upload_port or auto-detect)
def get_port():
    port = env.subst("$UPLOAD_PORT")
    if port:
        return port
    from platformio.device.finder import SerialPortFinder
    return SerialPortFinder(
        board_config=env.BoardConfig(),
        upload_protocol=env.subst("$UPLOAD_PROTOCOL"),
    ).find()


# ---------------------------------------------------------------------------
# Target: coredump-info — read coredump from flash and decode it
# ---------------------------------------------------------------------------
def coredump_info_action(target, source, env):
    port = get_port()
    print(f"\n{'='*70}")
    print(f"  COREDUMP ANALYSIS")
    print(f"  Port: {port}")
    print(f"  ELF:  {firmware_elf}")
    print(f"{'='*70}\n")

    # esp_coredump CLI v1.15+:
    #   Global flags (--port, --chip) BEFORE subcommand
    #   Subcommand flags (--gdb, --off, --rom-elf, prog) AFTER subcommand
    cmd = [
        sys.executable, "-m", "esp_coredump",
        "--chip", "esp32s3",
        "--port", port,
        "info_corefile",
        "--gdb", gdb_path,
        "--off", COREDUMP_OFFSET,
        "--parttable-off", "0x8000",
        "--rom-elf", rom_elf,
        "--core-format", "elf",
        firmware_elf,
    ]

    print(f"Running: {' '.join(cmd)}\n")
    result = subprocess.run(cmd, cwd=env.subst("$PROJECT_DIR"))
    if result.returncode != 0:
        print("\n❌ Coredump analysis failed. Is the ESP32 connected via USB?")
        print("   Make sure the device has crashed at least once since last flash.")
        return result.returncode
    return 0


env.AddCustomTarget(
    name="coredump-info",
    dependencies=None,
    actions=coredump_info_action,
    title="Coredump Info",
    description="Read and decode coredump from ESP32 flash via USB",
    always_build=True,
)
