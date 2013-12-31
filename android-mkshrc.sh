# put comments on their own line
# this file is in the same dir as everything
# it runs with root perms

# utility functions
prompt(){
# debug-friendly:
#	echo 'i+&*!Vs0' >&2
	echo -e '\0\c' >&2
}
prompt
remount(){
	mount -o remount,"${1:-rw}" $(mount | grep -o '.* /system ')
}

# rpc
# install <relay> [--cache]
install(){
	remount
	if [ "$2" != "--cache" -o ! -d miredo ]
	then
		rm -rf miredo
		mkdir miredo
		cp "$FILES_DIR"/* miredo
		chmod 755 miredo/{miredo{,-privproc},client-hook}
		chgrp 3003 miredo/miredo-privproc
		echo success
	fi
	relay="$1"
	echo > miredo/miredo.conf '=====CONFIG====='
	prompt
}
uninstall(){
	remount
	rm -rf miredo
	remount ro
	[ -d /system/miredo ] && echo failure || echo success
	prompt
	exit 0
}
# must be installed first
miredo(){
	ps miredo | while read _ pid _
	do
		[ "$pid" = "PID" ] || kill "$pid"
	done
	cd miredo
# ignore race on $miredo pid reuse
	trap 'kill "$miredo" "$read" 2>/dev/null; exit 143' TERM
	maxfd="$(ulimit -n)"
# mksh doesn't use this fd
	exec 3<&0
	(read -u3; kill "$$" 2>/dev/null) & read="$!"
	while :
	do
		./miredo -f & miredo="$!"
		wait "$miredo"
		echo status $?
	done
}
