# rmg-rfid-station-knowledge — MCP upstream

The knowledge server deployed on the SIoT MCP gateway for this repo. **This folder is the source
of record**; the gateway runs a copy.

| | |
|---|---|
| Gateway | `10.10.8.113` (`ssh mcp-gateway`, root) |
| Upstream name | `rmg-rfid-station-knowledge`, prefix `rmgrfid` |
| Listen | `127.0.0.1:8020` (streamable-HTTP `/mcp`) |
| Server dir | `/home/mcp/mcp-servers/rmg-rfid-station-knowledge/` |
| Mirror | `/home/mcp/knowledge/rmg-rfid-station-fw/` (from `tools/sync-knowledge-mcp.sh`) |
| Unit | `mcp-rmg-rfid-station-knowledge.service` |
| mcp SDK | 1.x locked (`<2.0` because 2.x removed `mcp.server.fastmcp`) |

Deployed 2026-09-24. Cloned from `as5047p-knowledge`: the generic doc/search engine is kept
verbatim; the project tools were replaced.

## Tools

`list_docs` · `get_doc` · `search` — generic, word-anchored search across the mirror.
`get_contract` · `get_handoff` · `get_runbook` · `get_pin_map` · `get_lorawan_payload` ·
`get_architecture` · `get_gotchas` · `get_bench_facts` · `get_plan` · `list_skills` · `get_skill` ·
`get_session_log` — named pointers to the load-bearing documents.

```
call_upstream_tool("rmg-rfid-station-knowledge", "get_handoff", {})
call_upstream_tool("rmg-rfid-station-knowledge", "search", {"query": "reset on open"})
```

## Update content (the common case)

```bash
tools/sync-knowledge-mcp.sh      # stage → gitleaks → rsync → restart → verify handshake
```

## Update the server code

```bash
scp tools/mcp-server/server.py mcp-gateway:/home/mcp/mcp-servers/rmg-rfid-station-knowledge/
ssh mcp-gateway 'chown mcp:mcp /home/mcp/mcp-servers/rmg-rfid-station-knowledge/server.py && systemctl restart mcp-rmg-rfid-station-knowledge.service'
```

## First deployment (done 2026-09-24)

1. `ssh mcp-gateway 'mkdir -p /home/mcp/mcp-servers/rmg-rfid-station-knowledge'`; `scp` `server.py`,
   `pyproject.toml`; `sudo -u mcp uv lock && uv sync` in that dir (a cloned `uv.lock` carries the
   template's project name and `uv sync --frozen` refuses it — always re-lock).
2. Install the unit into `/etc/systemd/system/`, `daemon-reload`, `enable --now`.
3. `tools/sync-knowledge-mcp.sh` to populate the mirror.
4. Back up `proxy-config.json`, append the `servers[]` entry (name, url, prefix, enabled, timeout,
   description), `systemctl restart mcp-proxy.service`, then `reload_configuration` +
   `check_upstream_health` through the gateway.
