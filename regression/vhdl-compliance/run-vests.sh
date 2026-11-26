#!/usr/bin/env bash
#
# Runs the parser of the VHDL front-end over the VHDL-93 compliance tests
# of VESTs (Vhdl tESTs, Clifton Labs / University of Cincinnati), as
# distributed with GHDL in testsuite/vests/vhdl-93. The suite is GPL
# licensed and is thus not part of this repository; it is fetched here.
#
# Only parsing is tested: the "compliant" tests must be accepted by the
# parser, and the acceptance rate must not fall below the baseline in
# baseline.txt. The baseline is raised as the grammar grows.
#
# Usage: run-vests.sh [path/to/ebmc] [report.html]
#   When a report path is given, an HTML report is written to it.

set -eu

EBMC=${1:-$(dirname "$0")/../../src/ebmc/ebmc}
EBMC=$(realpath "$EBMC")
REPORT=${2:-}
BASELINE=$(dirname "$0")/baseline.txt
VESTS_DIR=${VESTS_DIR:-/tmp/ghdl-vests}
VESTS_URL=https://github.com/ghdl/ghdl

if [ ! -d "$VESTS_DIR/testsuite/vests/vhdl-93" ]; then
  git clone --depth 1 --filter=blob:none --sparse "$VESTS_URL" "$VESTS_DIR"
  git -C "$VESTS_DIR" sparse-checkout set testsuite/vests/vhdl-93
fi

html_escape()
{
  sed -e 's/&/\&amp;/g' -e 's/</\&lt;/g' -e 's/>/\&gt;/g'
}

# per-suite results, for the report
declare -a SUMMARY_ROWS
RESULT_ROWS=""

# Runs one suite; sets ACCEPTED and TOTAL, and appends to RESULT_ROWS.
run_suite()
{
  local suite=$1
  local dir=$VESTS_DIR/testsuite/vests/vhdl-93/$suite
  ACCEPTED=0
  TOTAL=0

  for f in "$dir"/*.vhd*; do
    TOTAL=$((TOTAL+1))
    local name
    name=$(basename "$f")
    local output
    if output=$(timeout 10 "$EBMC" "$f" --show-parse 2>&1); then
      ACCEPTED=$((ACCEPTED+1))
      if [ -n "$REPORT" ]; then
        RESULT_ROWS+="<tr class=\"ok\"><td>$suite</td><td>$name</td><td>accepted</td></tr>"$'\n'
      fi
    else
      if [ -n "$REPORT" ]; then
        local reason
        reason=$(echo "$output" | grep -m1 -E "syntax error|error" | sed -E 's/^file [^:]*: //' | html_escape)
        RESULT_ROWS+="<tr class=\"fail\"><td>$suite</td><td>$name</td><td>${reason:-rejected}</td></tr>"$'\n'
      fi
    fi
  done
}

status=0

while read -r suite baseline; do
  [ -z "$suite" ] && continue
  case "$suite" in \#*) continue;; esac

  run_suite "$suite"
  echo "$suite: $ACCEPTED of $TOTAL accepted by the parser (baseline $baseline)"
  SUMMARY_ROWS+=("<tr><td>$suite</td><td>$TOTAL</td><td>$ACCEPTED</td><td>$baseline</td></tr>")

  if [ "$ACCEPTED" -lt "$baseline" ]; then
    echo "FAILED: fewer tests accepted than the baseline"
    status=1
  elif [ "$ACCEPTED" -gt "$baseline" ]; then
    echo "NOTE: more tests accepted than the baseline; raise it in baseline.txt"
  fi
done < "$BASELINE"

if [ -n "$REPORT" ]; then
  VERSION=$("$EBMC" --version 2>/dev/null | head -1 | html_escape)
  VESTS_COMMIT=$(git -C "$VESTS_DIR" rev-parse --short HEAD 2>/dev/null || echo unknown)
  DATE=$(date -u +"%Y-%m-%d %H:%M UTC")

  cat > "$REPORT" <<EOF
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>EBMC VHDL compliance: VESTs VHDL-93</title>
<style>
  :root { color-scheme: light dark; }
  body { font-family: -apple-system, system-ui, sans-serif; margin: 2rem auto;
         max-width: 60rem; padding: 0 1rem; line-height: 1.5; }
  h1 { margin-bottom: 0.25rem; }
  .meta { color: #666; font-size: 0.9rem; margin-bottom: 1.5rem; }
  table { border-collapse: collapse; width: 100%; margin-bottom: 2rem; }
  th, td { text-align: left; padding: 0.35rem 0.75rem; border-bottom: 1px solid #ddd; }
  th { position: sticky; top: 0; background: Canvas; }
  tr.ok td:last-child { color: #1a7f37; }
  tr.fail td:last-child { color: #cf222e; }
</style>
</head>
<body>
<h1>VESTs VHDL-93 compliance tests</h1>
<p class="meta">
  $VERSION &middot; VESTs at ghdl/ghdl@$VESTS_COMMIT &middot; $DATE
</p>
<p>
  The parser of the VHDL front-end is run over the compliant tests of
  <a href="$VESTS_URL/tree/master/testsuite/vests/vhdl-93">VESTs</a>
  (Vhdl tESTs, University of Cincinnati / Clifton Labs) for
  IEEE 1076-1993. A test is accepted when <code>ebmc --show-parse</code>
  succeeds. Type checking and synthesis are not exercised; the tests are
  simulation testbenches, not RTL designs.
</p>
<h2>Summary</h2>
<table>
<tr><th>Suite</th><th>Tests</th><th>Accepted</th><th>Baseline</th></tr>
$(printf '%s\n' "${SUMMARY_ROWS[@]}")
</table>
<h2>Results</h2>
<table>
<tr><th>Suite</th><th>Test</th><th>Result</th></tr>
$RESULT_ROWS
</table>
</body>
</html>
EOF
  echo "report written to $REPORT"
fi

exit $status
