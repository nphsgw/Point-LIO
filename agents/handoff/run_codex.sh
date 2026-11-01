#!/usr/bin/env bash
# Run Codex on an implementation brief, with access to the Docker daemon.
# Usage: agents/handoff/run_codex.sh new <brief.md> <output prefix>
#        agents/handoff/run_codex.sh resume <session id> <brief.md> <output prefix>
# Writes <prefix>.events.jsonl, <prefix>.report.json and <prefix>.session (the session id).
# The model is the default in ~/.codex/config.toml; CODEX_MODEL overrides it.
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
schema="$repo/agents/handoff/report.schema.json"
usage() { echo "usage: $0 new <brief> <prefix> | resume <session id> <brief> <prefix>" >&2; exit 2; }

# The Docker client reaches /var/run/docker.sock only when the sandbox allows network access.
opts=(${CODEX_MODEL:+-m "$CODEX_MODEL"}
      -c sandbox_mode='"workspace-write"'
      -c sandbox_workspace_write.network_access=true
      --json --output-schema "$schema")

case "${1:-}" in
    new)
        [[ $# -eq 3 ]] || usage
        brief=$2 prefix=$3
        cmd=(codex exec -C "$repo" "${opts[@]}" -o "$prefix.report.json" -) ;;
    resume)
        [[ $# -eq 4 ]] || usage
        brief=$3 prefix=$4
        cmd=(codex exec resume "${opts[@]}" -o "$prefix.report.json" "$2" -) ;;
    *) usage ;;
esac
[[ -f $brief ]] || { echo "no brief: $brief" >&2; exit 2; }

cd "$repo"
"${cmd[@]}" < "$brief" > "$prefix.events.jsonl"
python3 -I -c '
import json, sys
for line in open(sys.argv[1]):
    e = json.loads(line)
    if e.get("type") == "thread.started":
        print(e["thread_id"]); break
else:
    sys.exit("no thread.started event")
' "$prefix.events.jsonl" > "$prefix.session"
