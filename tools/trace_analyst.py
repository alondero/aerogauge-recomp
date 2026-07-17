"""Token-efficient summary of a large port log: first crash, repetition, markers.

Adapted from automobililamborghini-recomp/tools/trace_analyst.py for this
port's log vocabulary. Read the summary, THEN read only the pinpointed raw
lines -- never page a multi-thousand-line log into an agent context.

Usage: python tools/trace_analyst.py <log_file> [pattern]
       optional extra regex is counted + sampled (first/last 3 hits).
"""
import re
import sys
from collections import Counter


def analyze_trace(file_path, extra=None):
    try:
        with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
            lines = f.readlines()
    except Exception as e:
        return f"Error reading log: {e}"

    patterns = {
        'CRASH': re.compile(r'CRASH|SIGSEGV|Segmentation fault|Assertion.*failed|'
                            r'assert|Exception \(PC=|RSP ucode exited unexpectedly|'
                            r'terminate called|abort', re.I),
        'STUB': re.compile(r'STUB: ([\w_]+)'),
        'FUNC_CALL': re.compile(r'(func_8[0-9A-Fa-f]{7})'),
        'SEND_DL': re.compile(r'\[rt64\] send_dl'),
        'PROBE': re.compile(r'\[probe\]'),
        'VI_SWAP': re.compile(r'fb-swap|VI_ORIGIN'),
    }
    if extra:
        patterns['EXTRA'] = re.compile(extra)

    results = {k: [] for k in patterns}
    line_counts = Counter()
    first_crash = None

    for i, line in enumerate(lines):
        line_counts[line.strip()] += 1
        for name, pattern in patterns.items():
            m = pattern.search(line)
            if not m:
                continue
            if name == 'CRASH' and first_crash is None:
                first_crash = lines[max(0, i - 2):min(len(lines), i + 3)]
            results[name].append(m.group(1) if m.groups() else line.rstrip())

    out = [f"--- {file_path}: {len(lines)} lines ---"]
    if first_crash:
        out.append("\n[!] FIRST CRASH/ERROR:")
        out.extend(f"  {l.strip()}" for l in first_crash)

    out.append("\nTop 5 most frequent lines:")
    for line, count in line_counts.most_common(5):
        if count > 1:
            out.append(f"  ({count}x) {line[:100]}")

    for key in ('STUB', 'FUNC_CALL'):
        top = Counter(results[key]).most_common(10)
        if top:
            out.append(f"\nUnique {key} (top 10):")
            out.extend(f"  - {item} ({n}x)" for item, n in top)

    out.append("\nMarker counts: " + ", ".join(
        f"{k}={len(results[k])}" for k in ('SEND_DL', 'PROBE', 'VI_SWAP')))
    for k in ('SEND_DL', 'PROBE'):
        if results[k]:
            out.append(f"  last {k}: {results[k][-1][:120]}")

    if extra and results.get('EXTRA'):
        hits = results['EXTRA']
        out.append(f"\nEXTRA /{extra}/: {len(hits)} hit(s)")
        sample = hits[:3] + (["  ..."] if len(hits) > 6 else []) + hits[-3:] \
            if len(hits) > 6 else hits
        out.extend(f"  {h[:120]}" for h in sample if h != "  ...")

    return "\n".join(out)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python tools/trace_analyst.py <log_file> [extra_regex]")
        sys.exit(1)
    print(analyze_trace(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else None))
