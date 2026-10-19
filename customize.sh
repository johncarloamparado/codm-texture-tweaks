#!/system/bin/sh
# Runs inside Magisk / Kitsune Magisk / KernelSU / SukiSU / APatch installers.
SKIPUNZIP=0
ui_print "- CODM Blocky Textures"
ui_print "- Contributors: zynn, Shari"
ui_print "- Needs Zygisk (Magisk: Settings > Zygisk ON, or ZygiskNext on KernelSU/APatch)"
# v0.40: there is no x86 / x86_64 library. Stop here instead of installing a module that never loads.
case "$ARCH" in
  x86|x64|x86_64)
    abort "! x86 / x86_64 devices are not supported (this device: $ARCH). The module only ships arm64-v8a and armeabi-v7a libraries."
    ;;
esac
case "$(getprop ro.product.cpu.abi)" in
  x86|x86_64)
    abort "! x86 / x86_64 devices are not supported (abi: $(getprop ro.product.cpu.abi)). The module only ships arm64-v8a and armeabi-v7a libraries."
    ;;
esac
# $ARCH comes from ro.product.cpu.abi, which other modules can spoof and which says "arm" on a 32-bit Android
# running on a 64-bit CPU. So look at what the phone can really run before deciding.
ABILIST64="$(getprop ro.product.cpu.abilist64)"
REAL64=0
if [ "$ARCH" = "arm64" ] || [ "$IS64BIT" = "true" ] || [ -n "$ABILIST64" ] || [ -e /system/bin/linker64 ]; then REAL64=1; fi
ui_print "- arch check: ARCH=$ARCH IS64BIT=$IS64BIT abi=$(getprop ro.product.cpu.abi) abilist64=${ABILIST64:-none} linker64=$([ -e /system/bin/linker64 ] && echo yes || echo no) zygote=$(getprop ro.zygote)"

# v0.39: the zip carries a 64-bit and a 32-bit library. Zygisk loads the one that matches the game's process.
HAVE64=0
HAVE32=0
[ -f "$MODPATH/zygisk/arm64-v8a.so" ] && HAVE64=1
[ -f "$MODPATH/zygisk/armeabi-v7a.so" ] && HAVE32=1
ui_print "- ABI package: arm64=$HAVE64 arm32=$HAVE32"
if [ "$HAVE64" = "0" ] && [ "$HAVE32" = "0" ]; then
  abort "! No zygisk/*.so in this zip (need arm64-v8a and/or armeabi-v7a). Re-download the module."
fi

if [ "$REAL64" = "1" ]; then
  if [ "$ARCH" != "arm64" ]; then
    ui_print "! Note: the installer reports ARCH=$ARCH but 64-bit support exists (abi property may be spoofed). Continuing."
  fi
  if [ "$HAVE64" = "0" ]; then
    if [ "$HAVE32" = "1" ]; then
      ui_print "! Warning: 64-bit phone but zygisk/arm64-v8a.so is missing. Only a 32-bit CODM would load the module."
    else
      abort "! zygisk/arm64-v8a.so is missing from this zip. Re-download the module."
    fi
  fi
else
  if [ "$ARCH" = "arm" ] || [ -n "$KSU" ] || [ -n "$APATCH" ]; then
    ui_print "- 32-bit Android detected: the module uses zygisk/armeabi-v7a.so"
    if [ "$HAVE32" = "0" ]; then
      abort "! zygisk/armeabi-v7a.so is missing from this zip. Re-download the module."
    fi
  else
    abort "! Only arm / arm64 phones are supported (this device: $ARCH)"
  fi
fi

ui_print "- Settings UI: KernelSU/SukiSU/APatch open it from the manager."
ui_print "  Magisk has no built-in WebUI: use KsuWebUI or MMRL to open this module's page."
ui_print "- Reboot, then open CODM once (until the lobby). The module writes codm_tex.status there."
ui_print "  If the WebUI later says the module never ran: check Zygisk is ON and CODM is not on the DenyList."
