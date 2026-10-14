#!/system/bin/sh
# Runs inside Magisk / Kitsune Magisk / KernelSU / SukiSU / APatch installers.
SKIPUNZIP=0
ui_print "- CODM Blocky Textures"
ui_print "- Needs Zygisk (Magisk: Settings > Zygisk ON, or ZygiskNext on KernelSU/APatch)"
# $ARCH comes from ro.product.cpu.abi, which other modules can spoof and which says "arm" on a 32-bit Android
# running on a 64-bit CPU. So look at what the phone can really run before refusing.
ABILIST64="$(getprop ro.product.cpu.abilist64)"
REAL64=0
if [ "$ARCH" = "arm64" ] || [ "$IS64BIT" = "true" ] || [ -n "$ABILIST64" ] || [ -e /system/bin/linker64 ]; then REAL64=1; fi
ui_print "- arch check: ARCH=$ARCH IS64BIT=$IS64BIT abi=$(getprop ro.product.cpu.abi) abilist64=${ABILIST64:-none} linker64=$([ -e /system/bin/linker64 ] && echo yes || echo no) zygote=$(getprop ro.zygote)"
if [ "$REAL64" != "1" ] && [ -z "$KSU" ] && [ -z "$APATCH" ]; then
  ui_print "! This phone looks like a 32-bit Android (no 64-bit support found)."
  ui_print "! The module is arm64 only, so it cannot run here."
  abort "! Only arm64 phones are supported (this device: $ARCH)"
fi
if [ "$ARCH" != "arm64" ]; then
  ui_print "! Note: the installer reports ARCH=$ARCH but 64-bit support exists (abi property may be spoofed). Continuing."
fi
if [ ! -f "$MODPATH/zygisk/arm64-v8a.so" ]; then
  abort "! zygisk/arm64-v8a.so is missing from this zip. Re-download the module."
fi
ui_print "- Settings UI: KernelSU/SukiSU/APatch open it from the manager."
ui_print "  Magisk has no built-in WebUI: use KsuWebUI or MMRL to open this module's page."
ui_print "- Reboot, then open CODM once (until the lobby). The module writes codm_tex.status there."
ui_print "  If the WebUI later says the module never ran: check Zygisk is ON and CODM is not on the DenyList."
