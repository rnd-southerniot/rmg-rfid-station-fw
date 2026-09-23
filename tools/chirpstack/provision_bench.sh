#!/usr/bin/env bash
# tools/chirpstack/provision_bench.sh — register one RAK3212 RFID station on the bench ChirpStack v4
# (10.10.8.140, tenant from ~/.config/siot/chirpstack-dev.env) and put its AppKey into
# include/credentials.h (gitignored). gRPC only (ChirpStack v4 has no REST API).
#
#   tools/chirpstack/provision_bench.sh <DevEUI-hex16> [device-name]
#   e.g. tools/chirpstack/provision_bench.sh 3CDC75FFFE6F85DC rfid-station-6F85DC
#
# Idempotent: reuses the device profile "rmg-rfid-station" and application "rmg-rfid-stations"
# if they exist; refuses to overwrite an existing device (delete it in ChirpStack first, then
# `lora clear-session yes` on the node, because a re-created device rejects the old session).
# The key is never printed — only its first 4 hex digits as a fingerprint.
set -euo pipefail

DEVEUI="${1:-}"; NAME="${2:-}"
[[ "$DEVEUI" =~ ^[0-9A-Fa-f]{16}$ ]] || { echo "usage: $0 <DevEUI 16 hex> [name]" >&2; exit 2; }
DEVEUI="$(echo "$DEVEUI" | tr 'A-F' 'a-f')"
[[ -n "$NAME" ]] || NAME="rfid-station-$(echo "$DEVEUI" | cut -c11-16 | tr 'a-f' 'A-F')"

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
CODEC="$ROOT/tools/chirpstack/rfid_station_codec.js"
CREDS="$ROOT/include/credentials.h"
[[ -f "$CODEC" ]] || { echo "codec not found: $CODEC" >&2; exit 3; }
[[ -f "$CREDS" ]] || { echo "credentials.h not found (cp include/credentials.h.example include/credentials.h)" >&2; exit 3; }

set -a; source "$HOME/.config/siot/chirpstack-dev.env"; set +a
: "${CS_API_TOKEN:?}" "${CS_TENANT_ID:?}"
HOST="${CS_GRPC_HOST:-10.10.8.140:8080}"
H="authorization: Bearer $CS_API_TOKEN"
cs() { grpcurl -plaintext -max-time 10 -H "$H" -d "$2" "$HOST" "$1"; }

PROFILE_NAME="rmg-rfid-station"
APP_NAME="rmg-rfid-stations"

# ── device profile (AS923-1, MAC 1.0.4, RP002-1.0.3, Class A, OTAA, ADR off on the node, codec) ──
PROFILE_ID="$(cs api.DeviceProfileService/List "{\"limit\":100,\"tenantId\":\"$CS_TENANT_ID\"}" \
  | python3 -c "import sys,json; print(next((p['id'] for p in json.load(sys.stdin).get('result',[]) if p.get('name')=='$PROFILE_NAME'),''))")"
if [[ -z "$PROFILE_ID" ]]; then
  echo "creating device profile $PROFILE_NAME"
  REQ="$(python3 - "$CODEC" "$CS_TENANT_ID" <<'PY'
import json, sys
codec = open(sys.argv[1]).read()
print(json.dumps({"deviceProfile": {
    "tenantId": sys.argv[2],
    "name": "rmg-rfid-station",
    "description": "RAK3212 RMG RFID station — LoRaWAN offline fallback (schema 1: fPort 10 scan, fPort 11 heartbeat)",
    "region": "AS923", "regionConfigId": "as923_1",
    "macVersion": "LORAWAN_1_0_4", "regParamsRevision": "RP002_1_0_3",
    "adrAlgorithmId": "default",
    "supportsOtaa": True, "supportsClassB": False, "supportsClassC": False,
    "payloadCodecRuntime": "JS", "payloadCodecScript": codec,
    "flushQueueOnActivate": True,
    "uplinkInterval": 86400,          # LoRa is silent while WiFi is up; do not flag the device inactive every hour
    "deviceStatusReqInterval": 0,
    "tags": {"repo": "rmg-rfid-station-fw", "schema": "1"}
}}))
PY
)"
  PROFILE_ID="$(cs api.DeviceProfileService/Create "$REQ" | python3 -c "import sys,json; print(json.load(sys.stdin)['id'])")"
else
  echo "device profile $PROFILE_NAME exists ($PROFILE_ID) — updating its codec"
  REQ="$(python3 - "$CODEC" "$PROFILE_ID" <<'PY'
import json, sys
print(json.dumps({"id": sys.argv[2]}))
PY
)"
  CUR="$(cs api.DeviceProfileService/Get "$REQ")"
  UPD="$(python3 - "$CODEC" <<'PY' "$CUR"
import json, sys
codec = open(sys.argv[1]).read()
d = json.loads(sys.argv[2])["deviceProfile"]
d["payloadCodecRuntime"] = "JS"; d["payloadCodecScript"] = codec
print(json.dumps({"deviceProfile": d}))
PY
)"
  cs api.DeviceProfileService/Update "$UPD" >/dev/null
fi
echo "profile id: $PROFILE_ID"

# ── application ──
APP_ID="$(cs api.ApplicationService/List "{\"limit\":100,\"tenantId\":\"$CS_TENANT_ID\"}" \
  | python3 -c "import sys,json; print(next((a['id'] for a in json.load(sys.stdin).get('result',[]) if a.get('name')=='$APP_NAME'),''))")"
if [[ -z "$APP_ID" ]]; then
  echo "creating application $APP_NAME"
  APP_ID="$(cs api.ApplicationService/Create "{\"application\":{\"tenantId\":\"$CS_TENANT_ID\",\"name\":\"$APP_NAME\",\"description\":\"RMG RFID stations (LoRaWAN offline fallback)\"}}" \
    | python3 -c "import sys,json; print(json.load(sys.stdin)['id'])")"
fi
echo "application id: $APP_ID"

# ── device (refuse to overwrite) ──
if cs api.DeviceService/Get "{\"devEui\":\"$DEVEUI\"}" >/dev/null 2>&1; then
  echo "device $DEVEUI already exists — not touching it (delete it in ChirpStack first if you want a new key)" >&2
  exit 4
fi
APPKEY="$(openssl rand -hex 16)"
cs api.DeviceService/Create "{\"device\":{\"devEui\":\"$DEVEUI\",\"name\":\"$NAME\",\"description\":\"RAK3212 RMG RFID station\",\"applicationId\":\"$APP_ID\",\"deviceProfileId\":\"$PROFILE_ID\",\"skipFcntCheck\":false,\"isDisabled\":false,\"joinEui\":\"0000000000000000\",\"tags\":{\"board\":\"rak3212\",\"repo\":\"rmg-rfid-station-fw\"}}}" >/dev/null
# LoRaWAN 1.0.x: ChirpStack's "application key" is stored as nwkKey; RadioLib gets the same key for both.
cs api.DeviceService/CreateKeys "{\"deviceKeys\":{\"devEui\":\"$DEVEUI\",\"nwkKey\":\"$APPKEY\",\"appKey\":\"$APPKEY\"}}" >/dev/null
echo "device $NAME ($DEVEUI) created; AppKey fingerprint ${APPKEY:0:4}…"

# ── write the key into credentials.h (local, gitignored) ──
python3 - "$CREDS" "$APPKEY" <<'PY'
import re, sys
path, key = sys.argv[1], sys.argv[2]
bytes_ = ", ".join("0x" + key[i:i+2].upper() for i in range(0, 32, 2))
s = open(path).read()
new = "#define LORA_APP_KEY   { " + bytes_ + " }\n"
s2, n = re.subn(r"#define LORA_APP_KEY\s+\{[^}]*\}\n", new, s, flags=re.S)
if n != 1:
    sys.exit("could not find exactly one LORA_APP_KEY block in " + path)
open(path, "w").write(s2)
print("credentials.h: LORA_APP_KEY written; JoinEUI stays 0000000000000000; DevEUI derives from the MAC")
PY
echo "next: scripts/flash.sh rak3212  →  lora show / wait for '[LoRa] JOINED AS923'  →  lora hb"
