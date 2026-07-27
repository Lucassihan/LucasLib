on run argv
	if (count of argv) < 1 then
		error "Usage: osascript send-claude-prompt.applescript PROMPT_FILE [--send] [--no-restore-clipboard]"
	end if

	set promptPath to item 1 of argv
	set shouldSend to false
	set shouldRestoreClipboard to true

	repeat with rawArg in argv
		set argText to rawArg as text
		if argText is "--send" then set shouldSend to true
		if argText is "--no-restore-clipboard" then set shouldRestoreClipboard to false
	end repeat

	set promptText to (read (POSIX file (promptPath as text)))
	set previousClipboard to ""
	try
		set previousClipboard to the clipboard
	end try

	set the clipboard to promptText
	do shell script "/usr/bin/open 'claude://resume'"
	delay 4

	tell application "Claude" to activate
	delay 1

	tell application "System Events"
		tell process "Claude"
			set frontmost to true
			keystroke "v" using command down
			delay 0.6
			if shouldSend then
				key code 36
			end if
		end tell
	end tell

	delay 1
	if shouldRestoreClipboard then
		try
			set the clipboard to previousClipboard
		end try
	end if
end run
