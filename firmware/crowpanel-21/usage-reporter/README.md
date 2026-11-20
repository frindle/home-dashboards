# Claude usage reporter

Sends your Claude plan usage (5-hour and weekly percent, with reset times) to the dashboard so the desk panel can show it
and raise "usage near limit" alerts.

## Where the numbers come from
Claude Code passes a JSON document to its status line command. For Pro and Max plans it contains `rate_limits.five_hour` and
`rate_limits.seven_day` with `used_percentage` and `resets_at`. That is the same figure Claude Code shows you, it needs no
login or API key, and the dashboard container never sees one. The fields appear after the first reply in a session and are
absent on other plans (the panel then shows "waiting for the reporter"). Per-model weekly limits and a burn rate are not in
that document: the dashboard works the burn rate out from how fast the 5-hour window has filled.

## Install (on the Mac that runs Claude Code)
1. Choose a long random string; put it in the dashboard `.env` as `PANEL_USAGE_TOKEN` and restart the dashboard.
2. Create `~/.config/claude-usage-report.env` (`chmod 600`):
   ```
   PANEL_USAGE_URL=https://<your-dashboard>/api/panel/usage
   PANEL_USAGE_TOKEN=<the same string>
   # optional: the status line command you used before, so it keeps working
   # CLAUDE_USAGE_NEXT=<your old statusLine command>
   ```
3. Point Claude Code's status line at the script in `~/.claude/settings.json`:
   ```
   "statusLine": { "type": "command", "command": "python3 /path/to/claude-usage-report.py" }
   ```
It posts at most once a minute with a 2 second timeout and never blocks or breaks the status line. Numbers only update while
Claude Code is in use; the panel marks them "old data" after `PANEL_USAGE_STALE_S` (default 20 minutes).

## Check without Claude Code
```
echo '{"rate_limits":{"five_hour":{"used_percentage":12,"resets_at":1999999999}}}' | python3 claude-usage-report.py
```
prints `5h 12%` (and posts if the config file exists).
