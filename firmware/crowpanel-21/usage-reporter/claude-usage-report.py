#!/usr/bin/env python3
"""Claude Code status line wrapper: sends the plan-usage numbers to the dashboard so the desk panel can show them.

Claude Code runs the status line command often and hands it a JSON document on stdin. For Pro/Max plans that document has
`rate_limits.five_hour` and `rate_limits.seven_day`, each `{used_percentage, resets_at}` (epoch seconds). Those are the numbers
Claude Code itself shows, so no Claude login, API key or local file is read or needed. The fields appear after the first reply
in a session; until then (or on other plans) this script sends nothing.

What it does on each call:
  1. read the JSON from stdin
  2. at most once a minute (and not at all if the numbers are unchanged for 5 minutes), POST the numbers to
     <PANEL_USAGE_URL> with `Authorization: Bearer <PANEL_USAGE_TOKEN>`, 2 second timeout, failures ignored
  3. print the status line: the output of CLAUDE_USAGE_NEXT (your previous status line command, optional) fed the same stdin,
     or a short default

Configuration, environment variables or the file ~/.config/claude-usage-report.env (KEY=value lines, mode 600):
  PANEL_USAGE_URL      full address of the dashboard endpoint, ending in /api/panel/usage
  PANEL_USAGE_TOKEN    the same value as PANEL_USAGE_TOKEN on the dashboard
  CLAUDE_USAGE_NEXT    optional: command line of the status line you used before (run through the shell)

It never fails the status line: any error means the status line still prints. Python 3 standard library only.
"""
import json
import os
import subprocess
import sys
import tempfile
import time
import urllib.request

CONF = os.path.expanduser("~/.config/claude-usage-report.env")
STATE = os.path.join(tempfile.gettempdir(), "claude-usage-report.state")
MIN_GAP_S = 60          # never post more often than this
REPEAT_S = 300          # post unchanged numbers again after this long, so the panel keeps them "fresh"


def conf():
    out = {k: os.environ[k] for k in ("PANEL_USAGE_URL", "PANEL_USAGE_TOKEN", "CLAUDE_USAGE_NEXT") if os.environ.get(k)}
    try:
        with open(CONF, encoding="utf-8") as fh:
            for line in fh:
                line = line.strip()
                if line and not line.startswith("#") and "=" in line:
                    k, v = line.split("=", 1)
                    out.setdefault(k.strip(), v.strip().strip('"').strip("'"))
    except OSError:
        pass
    return out


def window(w):
    if not isinstance(w, dict):
        return None
    p = w.get("used_percentage")
    if isinstance(p, bool) or not isinstance(p, (int, float)):
        return None
    out = {"used_pct": max(0.0, min(1000.0, float(p)))}
    r = w.get("resets_at")
    if isinstance(r, (int, float)) and not isinstance(r, bool) and r > 0:
        out["resets_at"] = int(r)
    return out


def build(data):
    rl = data.get("rate_limits") if isinstance(data, dict) else None
    if not isinstance(rl, dict):
        return None
    body = {"v": 1}
    h5, wk = window(rl.get("five_hour")), window(rl.get("seven_day"))
    if h5:
        body["five_hour"] = h5
    if wk:
        body["weekly"] = wk
    return body if len(body) > 1 else None


def due(body):
    now = time.time()
    try:
        with open(STATE, encoding="utf-8") as fh:
            last = json.load(fh)
    except (OSError, ValueError):
        last = {}
    gap = now - float(last.get("t", 0))
    same = last.get("b") == body
    if gap < MIN_GAP_S or (same and gap < REPEAT_S):
        return False
    try:
        fd = os.open(STATE, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
        with os.fdopen(fd, "w", encoding="utf-8") as fh:
            json.dump({"t": now, "b": body}, fh)
    except OSError:
        pass
    return True


def post(url, token, body):
    req = urllib.request.Request(
        url, data=json.dumps(body).encode(), method="POST",
        headers={"Content-Type": "application/json", "Authorization": "Bearer " + token},
    )
    urllib.request.urlopen(req, timeout=2).read(256)


def main():
    raw = sys.stdin.read()
    try:
        data = json.loads(raw)
    except ValueError:
        data = {}
    c = conf()
    try:
        body = build(data)
        if body and c.get("PANEL_USAGE_URL") and c.get("PANEL_USAGE_TOKEN") and due(body):
            post(c["PANEL_USAGE_URL"], c["PANEL_USAGE_TOKEN"], body)
    except Exception:
        pass
    nxt = c.get("CLAUDE_USAGE_NEXT")
    if nxt:
        try:
            r = subprocess.run(nxt, input=raw, capture_output=True, text=True, shell=True, timeout=5)
            sys.stdout.write(r.stdout)
            return
        except Exception:
            pass
    b = build(data)
    h = (b or {}).get("five_hour", {}).get("used_pct")
    print("5h %d%%" % round(h) if h is not None else "")


if __name__ == "__main__":
    main()
