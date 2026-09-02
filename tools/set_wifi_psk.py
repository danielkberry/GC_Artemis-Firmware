#!/usr/bin/env python3
"""Write the WiFi password from stdin into main/src/Secrets.hpp without echoing it.

Usage (on the nitro, pulls it from the saved NetworkManager profile):
  sudo nmcli -s -g 802-11-wireless-security.psk con show thisisnotawifi_5G | python3 tools/set_wifi_psk.py

Handles nmcli's `-g` escaping (a literal ':' arrives as '\\:') and C-string escaping.
Prints only ok / not-replaced and the length; never the password.
"""
import pathlib
import re
import sys

p = pathlib.Path(__file__).resolve().parent.parent / "main" / "src" / "Secrets.hpp"
psk = sys.stdin.read().strip()
psk = psk.replace("\\:", ":")                      # nmcli -g escaping
if not psk:
    sys.exit("set_wifi_psk: empty input (did nmcli need a tty for sudo?)")
c = psk.replace("\\", "\\\\").replace('"', '\\"')  # C string literal escaping
src = p.read_text()
new, n = re.subn(r'(#define HOME_WIFI_PASS\s+)"[^"\n]*"', lambda m: m.group(1) + '"' + c + '"', src)
if n != 1:
    sys.exit("set_wifi_psk: HOME_WIFI_PASS line not found")
p.write_text(new)
print(f"ok: password set ({len(psk)} chars)")
