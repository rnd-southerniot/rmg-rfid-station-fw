# Knowledge MCP upstream — rmg-rfid-station-knowledge

| | |
|---|---|
| Gateway | `10.10.8.113` (`ssh mcp-gateway`, root), proxy `mcp-proxy.service`, `/home/mcp/mcp-gateway/configs/proxy-config.json` |
| Upstream name / prefix | `rmg-rfid-station-knowledge` / `rmgrfid` |
| Listen | `127.0.0.1:8020` (streamable-HTTP `/mcp`) — ports 8000–8019 were taken (`ss -tlnp` first) |
| Server dir | `/home/mcp/mcp-servers/rmg-rfid-station-knowledge/` — source of record `tools/mcp-server/` in this repo |
| Mirror | `/home/mcp/knowledge/rmg-rfid-station-fw/` (`CLAUDE.md README.md docs/ skills/ memory/`) |
| Unit | `mcp-rmg-rfid-station-knowledge.service` |
| Resync | `tools/sync-knowledge-mcp.sh` (gitleaks-guarded; aborts on anything credential-shaped) |

Use through the gateway: `call_upstream_tool("rmg-rfid-station-knowledge", "search", {"query": "…"})`,
`get_contract`, `get_handoff`, `get_runbook`, `get_pin_map`, `get_lorawan_payload`, `get_gotchas`,
`get_bench_facts`, `list_skills`, `get_skill`, `get_session_log`.
