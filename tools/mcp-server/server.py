#!/usr/bin/env python3
"""
rmg-rfid-station-knowledge MCP server.

Serves the rnd-southerniot/app-rmg-rfid-station-fw repo's knowledge (docs/, project
skills, .planning/knowledge, auto-memory) as queryable MCP tools, so an agent
working through the SIoT MCP gateway can pick up the RMG RFID station (ESP32 and the
RAK3212 ESP32-S3 + SX1262 port with LoRaWAN offline fallback) without local file access.

Upstream contract (matches the gateway's other servers): a FastMCP server
serving /mcp over streamable-HTTP.
  Run: uv run python server.py streamable-http 127.0.0.1 8020

Knowledge root is an rsync'd mirror of the repo on the VM, refreshed by
tools/sync-knowledge-mcp.sh in the repo. Override with RMGRFID_KNOWLEDGE_ROOT.
"""
import json
import os
import re
import sys
from pathlib import Path

from mcp.server.fastmcp import FastMCP

transport_mode = sys.argv[1] if len(sys.argv) > 1 else "streamable-http"
server_host = sys.argv[2] if len(sys.argv) > 2 else "127.0.0.1"
server_port = int(sys.argv[3]) if len(sys.argv) > 3 else 8020

ROOT = Path(
    os.environ.get("RMGRFID_KNOWLEDGE_ROOT", "/home/mcp/knowledge/rmg-rfid-station-fw")
).resolve()

DOC_DIRS = ["docs", "skills", "memory"]
EXTRA_FILES = ["CLAUDE.md", "README.md"]

mcp = FastMCP("rmg-rfid-station-knowledge", host=server_host, port=server_port)


def _iter_docs():
    for d in DOC_DIRS:
        base = ROOT / d
        if base.is_dir():
            for p in sorted(base.rglob("*.md")):
                yield p
    for f in EXTRA_FILES:
        p = ROOT / f
        if p.is_file():
            yield p


def _rel(p: Path) -> str:
    try:
        return str(p.relative_to(ROOT))
    except ValueError:
        return str(p)


def _title(p: Path) -> str:
    try:
        for line in p.read_text(errors="replace").splitlines():
            if line.startswith("# "):
                return line[2:].strip()
    except Exception:
        pass
    return ""


@mcp.tool()
def list_docs() -> list[dict]:
    """List available RMG RFID station knowledge documents (path + title)."""
    return [{"path": _rel(p), "title": _title(p)} for p in _iter_docs()]


@mcp.tool()
def get_doc(path: str) -> str:
    """Return the full markdown of a knowledge doc by its path (from list_docs())."""
    p = (ROOT / path).resolve()
    if p != ROOT and ROOT not in p.parents:
        return "error: path outside knowledge root"
    if not p.is_file():
        return f"error: not found: {path}"
    return p.read_text(errors="replace")


# Dropped from a query while other terms remain. A knowledge base is asked questions in prose —
# "why does it drop rather than queue" — and requiring "does"/"rather"/"than" to appear turns a
# perfectly reasonable question into zero results. Negations ("no", "not", "never") are deliberately
# NOT here: in these documents they carry the meaning.
_STOPWORDS = {
    "a", "an", "and", "are", "as", "at", "be", "but", "by", "can", "do", "does", "for", "from",
    "how", "i", "if", "in", "into", "is", "it", "its", "of", "on", "or", "our", "so", "than",
    "that", "the", "their", "them", "then", "there", "these", "they", "this", "to", "was", "we",
    "what", "when", "where", "which", "why", "will", "with", "you",
}


def _term_re(term: str) -> "re.Pattern[str]":
    """
    A term matches at the **start of a word**, not anywhere inside one.

    Plain substring matching makes short terms catastrophically noisy: searching for `no` also hits
    *know*, *cannot* and *nothing*, which is how a four-word query reported 1527 matches across the
    knowledge base. Anchoring to a word start keeps the useful looseness — `queue` still finds
    *queues* and *queueing* — while `no` stops matching the middle of unrelated words.
    """
    return re.compile(r"(?<![a-z0-9])" + re.escape(term))


def _flatten(text: str) -> str:
    """
    Whitespace-normalised text.

    This is what lets a phrase match across a **hard line wrap**. Every document here is prose wrapped
    at ~100 characters, so line-at-a-time matching misses any phrase unlucky enough to straddle a
    break — which is most of them.
    """
    return " ".join(text.lower().split())


@mcp.tool()
def search(query: str, max_results: int = 8, per_doc: int = 2) -> dict:
    """
    Ranked search across all knowledge docs.

    Returns {query, total, returned, truncated, results:[{path,line,score,snippet}]}.

    Three deliberate behaviours, each replacing something the previous implementation got wrong:

    1. **Terms, not one literal string.** It used to test the entire query as a single substring, so
       any multi-word question missed unless quoted verbatim from a document.
    2. **Phrases match across line wraps**, because these files are hard-wrapped prose.
    3. **Everything is scanned, ranked, and only then truncated** — and at most [per_doc] hits come
       from any one document. The old version returned at the eighth match in directory order, so a
       common term returned whichever file happened to be scanned first: searching "mqtt" gave eight
       hits from one file and never reached the reference document for it.

    [total] and [truncated] exist because silent truncation reads as "that is all there is".
    """
    raw = query.strip()
    if not raw:
        # Same keys as every other return: a caller that indexes the reply must not have to special
        # case the empty query.
        return {
            "query": query,
            "documents": 0,
            "total": 0,
            "returned": 0,
            "truncated": False,
            "results": [],
        }

    phrase = _flatten(raw)
    terms = [t for t in phrase.split(" ") if t]
    # Keep the stopwords if that is all there is, so searching "how to" still does something.
    meaningful = [t for t in terms if t not in _STOPWORDS] or terms
    patterns = [_term_re(t) for t in meaningful]

    scored: list[tuple] = []

    for p in _iter_docs():
        try:
            text = p.read_text(errors="replace")
        except Exception:
            continue
        lines = text.splitlines()
        flat = _flatten(text)
        rel = _rel(p)
        rel_l = rel.lower()

        matched = [t for t, pat in zip(meaningful, patterns) if pat.search(flat) or pat.search(rel_l)]
        if not matched:
            continue
        complete = len(matched) == len(meaningful)
        has_phrase = len(terms) > 1 and phrase in flat

        # Best lines within the document: the ones carrying the most terms, headings first.
        line_hits: list[tuple[int, int]] = []
        for i, line in enumerate(lines):
            low = line.lower()
            carried = sum(1 for pat in patterns if pat.search(low))
            if carried:
                heading = 1 if line.lstrip().startswith("#") else 0
                line_hits.append((carried + heading, i))
        if not line_hits:
            # The terms are in the document but split across wrapped lines. Still a real match, so
            # surface the document rather than discarding it for a formatting accident.
            line_hits = [(0, 0)]
        line_hits.sort(key=lambda h: (-h[0], h[1]))

        score = (
            (100 if has_phrase else 0)
            + 40 * len(matched)
            + (20 if complete else 0)
            + (10 if any(pat.search(rel_l) for pat in patterns) else 0)  # the path is a strong hint
            + min(line_hits[0][0], 5)
        )
        scored.append((score, rel, lines, line_hits, complete))

    # Prefer documents carrying every term; fall back to partial matches only when none does, so a
    # precise query is not diluted by documents that merely share a word with it.
    if any(entry[4] for entry in scored):
        scored = [entry for entry in scored if entry[4]]

    # Counted **after** the filter, over exactly the documents [documents] refers to. Counting before
    # it meant the two numbers described different sets — 4 documents beside 1296 matches — which is
    # the same defect as a truncation flag that disagrees with its own totals.
    total = sum(len(entry[3]) for entry in scored)

    scored.sort(key=lambda entry: (-entry[0], entry[1]))

    results: list[dict] = []
    for score, rel, lines, line_hits, _complete in scored:
        for _rank, i in line_hits[:per_doc]:
            results.append(
                {
                    "path": rel,
                    "line": i + 1,
                    "score": score,
                    "snippet": "\n".join(lines[max(0, i - 1): i + 2]),
                }
            )

    returned = results[:max_results]
    return {
        "query": raw,
        "documents": len(scored),
        "total": total,
        "returned": len(returned),
        # Compared against [total], not against the already-capped list: hits dropped by [per_doc]
        # are still hits the caller is not seeing. Reporting "truncated: false" beside "total: 10,
        # returned: 3" would be a contradiction, and the whole reason this field exists is that a
        # silently shortened result set reads as "that is all there is".
        "truncated": total > len(returned),
        "results": returned,
    }



# --- project-specific convenience tools ---------------------------------------------------------
# Each is a thin, named pointer into the mirror so an agent can reach the load-bearing documents
# without knowing the file layout. Mirror layout (see tools/sync-knowledge-mcp.sh):
#   CLAUDE.md, README.md          repo root
#   docs/*.md                     PIN_MAP, LORAWAN_PAYLOAD, RUNBOOK, ARCHITECTURE
#   skills/<name>/SKILL.md        project skills
#   memory/knowledge/...          .planning/knowledge (architecture, api-contracts, gotchas, sessions, devops)
#   memory/*.md                   auto-memory quick facts


@mcp.tool()
def get_contract() -> str:
    """The repo CLAUDE.md — targets, pin summary, safety gates, canonical commands, budgets, the
    phase table with PASS/FAIL status and the dated State block. START HERE for current status."""
    return get_doc("CLAUDE.md")


@mcp.tool()
def get_handoff() -> str:
    """The latest HANDOFF for the next session: where each phase stands, decisions still owed by
    the operator, and the exact commands + expected lines for the next phase."""
    base = ROOT / "memory" / "knowledge" / "sessions"
    if base.is_dir():
        logs = sorted(base.glob("*HANDOFF*.md"))
        if logs:
            return logs[-1].read_text(errors="replace")
    return "error: no handoff found"


@mcp.tool()
def get_runbook() -> str:
    """Bench and field procedures: build/flash/monitor per board, why the RAK3212 needs its own
    flash script and serial tool, the console commands, phase gates with exact expected lines,
    ChirpStack steps, a symptom → cause → action troubleshooting table, rollback."""
    return get_doc("docs/RUNBOOK.md")


@mcp.tool()
def get_pin_map() -> str:
    """Authoritative pin tables for the RAK3212 (LCD, touch, reader, NeoPixel, buzzer, SX1262
    internals, reserved pins), the two hard rules (no pin-less Wire/SPI begin), power notes, and
    the esp32dev pins."""
    return get_doc("docs/PIN_MAP.md")


@mcp.tool()
def get_lorawan_payload() -> str:
    """The LoRaWAN offline-fallback contract: radio parameters, fPort 10/11 byte layouts, the
    E_<epoch>_<seq> de-duplication rule for the ETS backend, golden vectors, ChirpStack setup."""
    return get_doc("docs/LORAWAN_PAYLOAD.md")


@mcp.tool()
def get_architecture() -> str:
    """Blocks, state machine (incl. the offline mode), event identity, module table, threads and
    buses, resource budgets."""
    return get_doc("docs/ARCHITECTURE.md")


@mcp.tool()
def get_gotchas() -> str:
    """ESP32-S3 / Arduino core / TFT_eSPI / RadioLib / USB-Serial-JTAG traps with file:line
    evidence: default SDA = SX1262 NRESET, pin-less SPI.begin, HSPI mandatory, LEDC timers,
    HWCDC stalls, reset-on-open, pio upload not syncing."""
    return get_doc("memory/knowledge/gotchas/esp32s3-arduino-rak3212.md")


@mcp.tool()
def get_bench_facts() -> str:
    """Bench-established facts about the reader (115200 ASCII frames, one frame per card entry)
    and the MSP2834 display (backlight gate circuit, touch pull-ups to VCC), each labelled
    PROVEN / UNKNOWN with its evidence."""
    return get_doc("memory/knowledge/gotchas/reader-and-display-bench.md")


@mcp.tool()
def get_plan() -> str:
    """The approved plan of record for the RAK3212 port (context, hardware facts with provenance,
    pin map, architecture, phases with gates, risks)."""
    return get_doc("memory/knowledge/architecture/PLAN-rak3212-port.md")


@mcp.tool()
def list_skills() -> list[dict]:
    """List the project skills shipped with this repo."""
    base = ROOT / "skills"
    if not base.is_dir():
        return []
    return [
        {"skill": p.parent.name, "path": _rel(p), "title": _title(p)}
        for p in sorted(base.glob("*/SKILL.md"))
    ]


@mcp.tool()
def get_skill(skill: str) -> str:
    """Return a project skill's SKILL.md by name (from list_skills()), e.g. 'rak3212-bench' or
    'rfid-uart-reader-discovery'."""
    return get_doc(f"skills/{skill}/SKILL.md")


@mcp.tool()
def get_session_log() -> str:
    """Most recent dated session log: what was done, what was proven, and what is next."""
    base = ROOT / "memory" / "knowledge" / "sessions"
    if not base.is_dir():
        return "error: no session logs"
    logs = sorted(base.glob("*.md"))
    if not logs:
        return "error: no session logs"
    return logs[-1].read_text(errors="replace")


if __name__ == "__main__":
    print(
        f"rmg-rfid-station-knowledge MCP on {server_host}:{server_port} "
        f"({transport_mode}); root={ROOT}",
        flush=True,
    )
    mcp.run(transport="stdio" if transport_mode == "stdio" else "streamable-http")
