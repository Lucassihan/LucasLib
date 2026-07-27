#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage:
  scripts/install-claude-prompt-launchagent.sh --at "YYYY-MM-DD HH:MM" --prompt-file PATH [--send]

Installs a one-time macOS LaunchAgent that opens Claude Desktop with claude://resume,
pastes the prompt file into the active/resumed chat, and optionally presses Return.

Requirements:
  - The Mac must be awake and logged in at the scheduled time.
  - The automation runner must have Accessibility permission in System Settings.
USAGE
}

send_flag=""
at_value=""
prompt_file=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --at)
      at_value="${2:-}"
      shift 2
      ;;
    --prompt-file)
      prompt_file="${2:-}"
      shift 2
      ;;
    --send)
      send_flag="--send"
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

if [[ -z "$at_value" || -z "$prompt_file" ]]; then
  usage >&2
  exit 2
fi

if [[ ! -f "$prompt_file" ]]; then
  echo "Prompt file does not exist: $prompt_file" >&2
  exit 1
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_dir="$(cd "$script_dir/.." && pwd)"
apple_script="$script_dir/send-claude-prompt.applescript"

if [[ ! -f "$apple_script" ]]; then
  echo "Missing AppleScript bridge: $apple_script" >&2
  exit 1
fi

parsed="$(/bin/date -j -f "%Y-%m-%d %H:%M" "$at_value" "+%Y %m %d %H %M %Y%m%d%H%M" 2>/dev/null || true)"
if [[ -z "$parsed" ]]; then
  echo "Could not parse --at value. Expected: YYYY-MM-DD HH:MM" >&2
  exit 1
fi

read -r year month day hour minute stamp <<<"$parsed"
label="com.lucaschen.claude-prompt.$stamp"
launch_agents="$HOME/Library/LaunchAgents"
log_dir="$repo_dir/.claude-bridge/logs"
plist="$launch_agents/$label.plist"

/bin/mkdir -p "$launch_agents" "$log_dir"

program_args_send=""
if [[ -n "$send_flag" ]]; then
  program_args_send='
    <string>--send</string>'
fi

/bin/cat > "$plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key>
  <string>$label</string>
  <key>ProgramArguments</key>
  <array>
    <string>/usr/bin/osascript</string>
    <string>$apple_script</string>
    <string>$prompt_file</string>$program_args_send
  </array>
  <key>StartCalendarInterval</key>
  <dict>
    <key>Year</key>
    <integer>$year</integer>
    <key>Month</key>
    <integer>$month</integer>
    <key>Day</key>
    <integer>$day</integer>
    <key>Hour</key>
    <integer>$hour</integer>
    <key>Minute</key>
    <integer>$minute</integer>
  </dict>
  <key>StandardOutPath</key>
  <string>$log_dir/$label.out.log</string>
  <key>StandardErrorPath</key>
  <string>$log_dir/$label.err.log</string>
</dict>
</plist>
PLIST

/bin/launchctl unload "$plist" >/dev/null 2>&1 || true
/bin/launchctl load "$plist"

echo "Installed $label"
echo "Plist: $plist"
echo "Logs: $log_dir"
