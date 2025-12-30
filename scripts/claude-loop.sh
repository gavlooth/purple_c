#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

PROMPT_FILE="${PROMPT_FILE:-scripts/claude-loop.prompt}"
BMAD_AGENT_FILE="${BMAD_AGENT_FILE:-}"
MODEL_ENV="${MODEL:-}"
MODEL="${CLAUDE_MODEL:-}"
if [[ -z "$MODEL" ]]; then
  if [[ -n "$MODEL_ENV" && "$MODEL_ENV" != *"/"* && "$MODEL_ENV" != *".xml" ]]; then
    MODEL="$MODEL_ENV"
  else
    MODEL="opus"
  fi
fi
if [[ "$MODEL" == *"/"* || "$MODEL" == *".xml" ]]; then
  echo "Invalid model '$MODEL'; falling back to 'opus'. Set CLAUDE_MODEL to override." >&2
  MODEL="opus"
fi
MAX_ITERS="${MAX_ITERS:-25}"
PERMISSION_MODE="${PERMISSION_MODE:-acceptEdits}"
ALLOWED_TOOLS="${ALLOWED_TOOLS:-Bash(git:*) Bash(rg:*) Bash(ls:*) Bash(cat:*) Bash(sed:*) Bash(awk:*) Bash(grep:*) Bash(make:*) Bash(./tests.sh:*) Edit Read}"
MAX_BUDGET_USD="${MAX_BUDGET_USD:-}"
RUN_TESTS="${RUN_TESTS:-}"
STOP_ON_TEST_FAIL="${STOP_ON_TEST_FAIL:-1}"

if [[ ! -f "$PROMPT_FILE" ]]; then
  echo "Missing prompt file: $PROMPT_FILE" >&2
  exit 1
fi

if [[ -z "${ALLOW_DIRTY:-}" ]] && [[ -n "$(git status --porcelain)" ]]; then
  echo "Working tree not clean. Commit/stash or set ALLOW_DIRTY=1." >&2
  exit 1
fi

append_prompt="$(cat "$PROMPT_FILE")"
if [[ -n "$BMAD_AGENT_FILE" ]]; then
  if [[ ! -f "$BMAD_AGENT_FILE" ]]; then
    echo "BMAD agent file not found: $BMAD_AGENT_FILE" >&2
    exit 1
  fi
  append_prompt="$(cat "$BMAD_AGENT_FILE")"$'\n\n'"$append_prompt"
fi

budget_args=()
if [[ -n "$MAX_BUDGET_USD" ]]; then
  budget_args+=(--max-budget-usd "$MAX_BUDGET_USD")
fi

iter=1
while true; do
  if [[ "$MAX_ITERS" -gt 0 && "$iter" -gt "$MAX_ITERS" ]]; then
    echo "Reached MAX_ITERS=$MAX_ITERS; stopping."
    break
  fi

  echo "=== Claude loop iteration $iter ==="

  set +e
  output=$(claude -p \
    --model "$MODEL" \
    --permission-mode "$PERMISSION_MODE" \
    --allowedTools "$ALLOWED_TOOLS" \
    --append-system-prompt "$append_prompt" \
    --no-session-persistence \
    "${budget_args[@]}" \
    "Find and fix one issue in this repo.")
  status=$?
  set -e

  if [[ $status -ne 0 ]]; then
    printf '%s\n' "$output" >&2
    echo "Claude CLI failed (exit $status)." >&2
    exit $status
  fi

  printf '%s\n' "$output"

  if printf '%s\n' "$output" | grep -q '^NO_ISSUES\b'; then
    echo "No issues reported; stopping."
    break
  fi

  if [[ -n "$RUN_TESTS" ]]; then
    echo "Running tests: $RUN_TESTS"
    if ! bash -lc "$RUN_TESTS"; then
      if [[ "$STOP_ON_TEST_FAIL" = "1" ]]; then
        echo "Tests failed; stopping."
        exit 1
      fi
    fi
  fi

  if [[ -z "$(git status --porcelain)" ]]; then
    echo "No changes detected; stopping."
    break
  fi

  commit_msg=$(printf '%s\n' "$output" | sed -n 's/^COMMIT_MSG:[[:space:]]*//p' | head -n 1)
  if [[ -z "$commit_msg" ]]; then
    commit_msg="Fix issue (iteration $iter)"
  fi

  git add -A
  git commit -m "$commit_msg"

  iter=$((iter + 1))
done
