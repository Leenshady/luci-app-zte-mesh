# Follow the child log for 160 s (logctrl streams NEW lines only). Do not run another telnet session at the same time: captures get cut off.
/var/tmp/busybox timeout 160 logctrl -r -g 1 -n
