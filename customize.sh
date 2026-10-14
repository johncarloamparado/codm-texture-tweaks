#!/system/bin/sh
# Runs inside Magisk / Kitsune Magisk / KernelSU / SukiSU / APatch installers.
SKIPUNZIP=0
ui_print "- CODM Blocky Textures"
ui_print "- Needs Zygisk (Magisk: Settings > Zygisk ON, or ZygiskNext on KernelSU/APatch)"
if [ "$ARCH" != "arm64" ] && [ -z "$KSU" ] && [ -z "$APATCH" ]; then
  abort "! Only arm64 phones are supported (this device: $ARCH)"
fi
if [ ! -f "$MODPATH/zygisk/arm64-v8a.so" ]; then
  abort "! zygisk/arm64-v8a.so is missing from this zip. Re-download the module."
fi
ui_print "- Settings UI: KernelSU/SukiSU/APatch open it from the manager."
ui_print "  Magisk has no built-in WebUI: use KsuWebUI or MMRL to open this module's page."
ui_print "- Reboot, then open CODM once (until the lobby). The module writes codm_tex.status there."
ui_print "  If the WebUI later says the module never ran: check Zygisk is ON and CODM is not on the DenyList."
