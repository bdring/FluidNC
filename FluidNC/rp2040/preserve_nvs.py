#!/usr/bin/env python3
"""
Preserve NVS sector during build on RP2040.

The PlatformIO builder's ElfToHex rule includes '-R .eeprom' which strips
the EEPROM section from the hex file. Since our NVS sector is in the last 4KB
of flash (0x1FF000), we need to ensure it's included in the final binary.
"""

Import("env")

def inspect_firmware(source, target, env):
    """Inspect and report on NVS sector in the firmware."""
    elf_path = str(target[0])
    uf2_path = str(target[0]).replace('.elf', '.uf2')
    
    print("\n" + "="*70)
    print("NVS Sector Preservation Check")
    print("="*70)
    print(f"ELF file: {elf_path}")
    print(f"UF2 file: {uf2_path}")
    
    # Use objdump to check what sections are in the ELF file
    import subprocess
    result = subprocess.run(
        ['arm-none-eabi-objdump', '-h', elf_path],
        capture_output=True,
        text=True
    )
    
    print("\nELF Sections:")
    print(result.stdout)
    
    # Look for .eeprom section
    if '.eeprom' in result.stdout:
        print("✓ .eeprom section found in ELF")
    else:
        print("⚠ .eeprom section NOT found in ELF - NVS sector may not be included!")
    
    print("="*70 + "\n")

# Hook after ELF is generated but before hex is created
env.AddPostAction("$BUILD_DIR/firmware.elf", inspect_firmware)
