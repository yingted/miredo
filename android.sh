#!/bin/sh
# build and package for android
set -ex
#[ -f configure ] ||
	./autogen.sh
shopt -s nullglob
[ ! -f include/gettext.h ] || echo '#define gettext 
#define bindtextdomain ' > include/gettext.h
: ${NDK:=~/bin/arm-linux-androideabi-17}
if [ -z "$JUDY_SRC" ]
then
	for JUDY_SRC in ../judy*/src
	do
		:
	done
fi
if [ -n "$JUDY_SRC" ]
then
	JUDY_SRC="$(readlink -f "$JUDY_SRC")"
	JUDY_INCLUDE="-I$JUDY_SRC"
	JUDY_LIBS="-L$JUDY_SRC/obj/.libs"
fi
export PATH=$NDK/bin:"$PATH"
rm -rf build miredo.zip package
mkdir -p build
#[ -f Makefile ] ||
	CFLAGS="-s \
		-ffunction-sections -fdata-sections -Wl,--gc-sections \
		-Wl,--wrap,pthread_create,--wrap,pthread_kill \
		-O2 \
		-DNDEBUG \
		$JUDY_INCLUDE $JUDY_LIBS" \
	ac_cv_file__proc_self_maps=true \
	./configure \
	--host=arm-linux-androideabi \
	--prefix "$(readlink -f build)" \
	--enable-android
make -j install
mkdir -p package/miredo
(
	cd package
	(
		cd miredo
		cp ../../build/{sbin/miredo,libexec/miredo/miredo-privproc,etc/miredo/client-hook} .
		awk -v x="$(grep '^[^#]' ../../build/etc/miredo/miredo.conf | sed -e "s/'/'\\''/g" -e 's/^\(ServerAddress \).*/\1'\''"$relay\"'\''/')" '{gsub("=====CONFIG=====",x);print}' ../../android-mkshrc.sh > mkshrc
		sed -e '1s:/s\?bin/:/system&:' \
			-e 's:/sbin/ip:/system/bin/ip:g' \
			-e '2,${/^#/d}' \
			-e '/^$/d' \
			-i client-hook
		zip -r ../../miredo.zip * # exclude leading miredo/
	)
)
exit
adb push miredo.zip /sdcard/miredo.zip
adb shell 'su -c '\''
set -x
killall miredo
killall -9 miredo miredo-privproc
cd /system || exit 1
rm -rf miredo
set -e
unzip -o /sdcard/miredo
cd miredo
chgrp 3003 miredo-privproc
./miredo
'\'''
