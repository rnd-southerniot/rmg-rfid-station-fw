#!/usr/bin/env bash
# Resync this repo's knowledge to its MCP upstream on the SIoT gateway.
#
# Mirrors CLAUDE.md, README.md, docs/, .claude/skills/, .planning/knowledge/ and the
# auto-memory store to /home/mcp/knowledge/ on the gateway VM, then restarts the upstream
# so the new content is served.
#
# Run after any meaningful change to docs, skills or knowledge:
#   ./tools/sync-knowledge-mcp.sh
#
# Upstream:  rmg-rfid-station-knowledge  ->  http://127.0.0.1:8020  (prefix: rmgrfid)
# Reachable via: call_upstream_tool("rmg-rfid-station-knowledge", "<tool>", {...})
#
# The mirror is readable by anyone with gateway access, so the staged tree is scanned with
# gitleaks and the sync ABORTS on anything credential-shaped.
set -euo pipefail

GATEWAY="${GATEWAY:-mcp-gateway}"          # ssh alias -> root@10.10.8.113
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MIRROR="/home/mcp/knowledge/rmg-rfid-station-fw"
SERVICE="mcp-rmg-rfid-station-knowledge.service"
PORT=8020

# Claude Code encodes the project root by replacing "/" with "-"
MEM="$HOME/.claude/projects/$(printf '%s' "$REPO" | sed 's#/#-#g')"

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

say() { printf '\n\033[1m== %s\033[0m\n' "$*"; }

say "Staging mirror"
mkdir -p "$STAGE/docs" "$STAGE/skills" "$STAGE/memory/knowledge"
cp "$REPO/CLAUDE.md" "$REPO/README.md" "$STAGE/"
cp "$REPO/docs/"*.md "$STAGE/docs/"
cp -R "$REPO/.claude/skills/." "$STAGE/skills/"
cp -R "$REPO/.planning/knowledge/." "$STAGE/memory/knowledge/"
if [ -d "$MEM/memory" ]; then
  cp "$MEM"/memory/*.md "$STAGE/memory/" 2>/dev/null || true
else
  echo "  note: no auto-memory at $MEM — syncing repo knowledge only"
fi
echo "  $(command find "$STAGE" -type f | wc -l | tr -d ' ') files staged"

say "Guarding against secrets"
if command -v gitleaks >/dev/null 2>&1; then
  gitleaks detect --no-git --source "$STAGE" --redact --exit-code 1 >/dev/null 2>&1 \
    || { echo "ABORT: gitleaks flagged the staged mirror" >&2; exit 1; }
  echo "  gitleaks clean"
else
  echo "  WARNING: gitleaks not installed — skipping scan"
fi

say "Syncing to $GATEWAY:$MIRROR"
rsync -az --delete -e "ssh -o BatchMode=yes" "$STAGE/" "$GATEWAY:$MIRROR/"
ssh -o BatchMode=yes "$GATEWAY" "chown -R mcp:mcp $MIRROR && \
  echo \"  \$(find $MIRROR -type f | wc -l) files on gateway\""

say "Restarting upstream"
ssh -o BatchMode=yes "$GATEWAY" "systemctl restart $SERVICE && sleep 6 && \
  systemctl is-active $SERVICE"

say "Verifying"
ssh -o BatchMode=yes "$GATEWAY" "curl -s -m 10 -X POST http://127.0.0.1:$PORT/mcp \
  -H 'Content-Type: application/json' -H 'Accept: application/json, text/event-stream' \
  -d '{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-06-18\",\"capabilities\":{},\"clientInfo\":{\"name\":\"sync\",\"version\":\"1\"}}}' \
  | grep -q serverInfo && echo '  upstream responding' || { echo '  UPSTREAM NOT RESPONDING' >&2; exit 1; }"

cat <<EOT

Done. Content is live via the SIoT MCP gateway:
  call_upstream_tool("rmg-rfid-station-knowledge", "get_handoff", {})
  call_upstream_tool("rmg-rfid-station-knowledge", "search", {"query": "reset on open"})

Rollback: ssh $GATEWAY 'systemctl stop $SERVICE'
EOT
