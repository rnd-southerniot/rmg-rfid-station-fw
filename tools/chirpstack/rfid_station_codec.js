// ChirpStack v4 device-profile codec for rmg-rfid-station-fw (payload schema 1).
// Paste the whole file into Device profile → Codec → "Custom JavaScript codec functions".
// Byte layout: src/lora_payload.h and docs/LORAWAN_PAYLOAD.md. All multi-byte fields big-endian.
// Tested by tools/chirpstack/codec_test.js against the firmware's golden vectors.

function u32(b, i) { return ((b[i] << 24) >>> 0) + (b[i + 1] << 16) + (b[i + 2] << 8) + b[i + 3]; }
function u16(b, i) { return (b[i] << 8) + b[i + 1]; }
function i8(v) { return v > 127 ? v - 256 : v; }
function hex(b, i, n) {
  var s = "";
  for (var k = 0; k < n; k++) s += ("0" + b[i + k].toString(16)).slice(-2).toUpperCase();
  return s;
}

var EVENT_TYPES = { 1: "COMPLETE", 2: "QC_PASS", 3: "QC_FAIL" };
var OFFLINE_REASONS = { 0: "none", 1: "WIFI_DOWN", 2: "SERVER_UNREACHABLE", 3: "BOOT_NO_WIFI" };

function decodeUplink(input) {
  var b = input.bytes, port = input.fPort, warnings = [];
  if (!b || b.length < 2) return { errors: ["empty payload"] };
  if (b[0] !== 1) return { errors: ["unsupported schema " + b[0]] };
  var flags = b[1];

  if (port === 10) {
    if (b.length < 10) return { errors: ["scan frame too short (" + b.length + ")"] };
    var uidLen = b[9];
    if (uidLen < 4 || uidLen > 10 || b.length < 10 + uidLen) return { errors: ["bad uid_len " + uidLen] };
    var epoch = u32(b, 2), seq = u16(b, 6);
    var data = {
      type: "scan",
      schema: 1,
      time_synced: (flags & 1) !== 0,
      qc_station: (flags & 2) !== 0,
      replay: (flags & 8) !== 0,
      epoch: epoch,
      seq: seq,
      event_id: "E_" + epoch + "_" + seq,
      event_type: EVENT_TYPES[b[8]] || ("UNKNOWN_" + b[8]),
      rfid_uid: hex(b, 10, uidLen)
    };
    if ((flags & 4) !== 0) {
      var opLen = b[10 + uidLen];
      if (opLen >= 4 && opLen <= 10 && b.length >= 11 + uidLen + opLen) {
        data.operator_uid = hex(b, 11 + uidLen, opLen);
      } else {
        warnings.push("operator trailer truncated");
      }
    }
    return { data: data, warnings: warnings };
  }

  if (port === 11) {
    if (b.length < 20) return { errors: ["heartbeat frame too short (" + b.length + ")"] };
    return {
      data: {
        type: "heartbeat",
        schema: 1,
        time_synced: (flags & 1) !== 0,
        operator_logged_in: (flags & 2) !== 0,
        station_mapped: (flags & 4) !== 0,
        wifi_associated: (flags & 8) !== 0,
        uptime_s: u32(b, 2),
        queued_http_events: u16(b, 6),
        last_seq: u16(b, 8),
        lora_dropped: b[10],
        fw: b[11] + "." + b[12] + "." + b[13],
        wifi_rssi: i8(b[14]),
        offline_reason: OFFLINE_REASONS[b[15]] || ("UNKNOWN_" + b[15]),
        epoch: u32(b, 16)
      }
    };
  }

  return { errors: ["unknown fPort " + port] };
}

// Schema 1 defines no downlinks.
function encodeDownlink(input) {
  return { bytes: [] };
}
