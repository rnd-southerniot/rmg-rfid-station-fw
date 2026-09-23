#!/usr/bin/env node
// Cross-checks rfid_station_codec.js against the firmware's golden vectors
// (test/test_lora_payload/test_main.cpp). No dependencies: `node tools/chirpstack/codec_test.js`.
"use strict";
const fs = require("fs");
const path = require("path");

const src = fs.readFileSync(path.join(__dirname, "rfid_station_codec.js"), "utf8");
const codec = new Function(src + "\nreturn { decodeUplink: decodeUplink, encodeDownlink: encodeDownlink };")();

function bytes(hex) {
  const out = [];
  for (let i = 0; i < hex.length; i += 2) out.push(parseInt(hex.substr(i, 2), 16));
  return out;
}

const vectors = [
  {
    name: "S1 scan COMPLETE, no operator",
    fPort: 10, hex: "01016AB13B8004D20104A1B2C3D4",
    expect: { type: "scan", time_synced: true, qc_station: false, replay: false, epoch: 1790000000, seq: 1234,
              event_id: "E_1790000000_1234", event_type: "COMPLETE", rfid_uid: "A1B2C3D4", operator_uid: undefined }
  },
  {
    name: "S2 scan QC_PASS with operator",
    fPort: 10, hex: "01076AB13B8004D20204A1B2C3D404DEADBEEF",
    expect: { type: "scan", qc_station: true, event_type: "QC_PASS", rfid_uid: "A1B2C3D4", operator_uid: "DEADBEEF",
              event_id: "E_1790000000_1234" }
  },
  {
    name: "S3 scan QC_FAIL replay, unsynced, 10-byte UID, operator omitted",
    fPort: 10, hex: "01080000000004D2030A10111213141516171819",
    expect: { type: "scan", time_synced: false, replay: true, epoch: 0, event_type: "QC_FAIL",
              rfid_uid: "10111213141516171819", operator_uid: undefined, event_id: "E_0_1234" }
  },
  {
    name: "H1 heartbeat",
    fPort: 11, hex: "01070000 0E10 0005 04D2 02 000200 BD 01 6AB13B80".replace(/ /g, ""),
    expect: { type: "heartbeat", time_synced: true, operator_logged_in: true, station_mapped: true, wifi_associated: false,
              uptime_s: 3600, queued_http_events: 5, last_seq: 1234, lora_dropped: 2, fw: "0.2.0", wifi_rssi: -67,
              offline_reason: "WIFI_DOWN", epoch: 1790000000 }
  },
  { name: "E1 unknown port",        fPort: 12, hex: "0100",       expectError: /unknown fPort/ },
  { name: "E2 bad schema",          fPort: 10, hex: "0200",       expectError: /unsupported schema/ },
  { name: "E3 short scan",          fPort: 10, hex: "0101",       expectError: /too short/ },
  { name: "E4 bad uid_len",         fPort: 10, hex: "01016AB13B8004D20103A1B2C3", expectError: /bad uid_len/ },
  { name: "E5 short heartbeat",     fPort: 11, hex: "0107000000",  expectError: /too short/ }
];

let pass = 0, fail = 0;
for (const v of vectors) {
  const out = codec.decodeUplink({ bytes: bytes(v.hex), fPort: v.fPort });
  let ok = true, why = "";
  if (v.expectError) {
    ok = Array.isArray(out.errors) && out.errors.some((e) => v.expectError.test(e));
    if (!ok) why = "expected error " + v.expectError + ", got " + JSON.stringify(out);
  } else {
    if (!out.data) { ok = false; why = "no data: " + JSON.stringify(out); }
    else for (const k of Object.keys(v.expect)) {
      if (out.data[k] !== v.expect[k]) { ok = false; why += ` ${k}: expected ${JSON.stringify(v.expect[k])}, got ${JSON.stringify(out.data[k])};`; }
    }
  }
  console.log((ok ? "PASS " : "FAIL ") + v.name + (ok ? "" : " — " + why));
  ok ? pass++ : fail++;
}
const dl = codec.encodeDownlink({ data: {} });
const dlOk = Array.isArray(dl.bytes) && dl.bytes.length === 0;
console.log((dlOk ? "PASS " : "FAIL ") + "D1 encodeDownlink returns no bytes");
dlOk ? pass++ : fail++;
console.log(`codec: ${pass}/${pass + fail} vectors OK`);
process.exit(fail === 0 ? 0 : 1);
