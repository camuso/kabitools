#!/bin/bash
#
# makei
#

cmdline=makei

usagestr=$(
cat <<EOF

makei directory

\tCreate .i files from all the c files in a directory tree.
\0
EOF
)

usage() {
	echo -e "$usagestr"
	exit
}

[ $# -gt 0 ] || usage

dirspec=$1

if ! [ -d "$dirspec" ]; then
	echo
	echo "$dirspec is not a directory"
	usage
fi

set -x
find $dirspec -name \*.c -exec sh -c \
	'path=$(dirname $1); \
	file=$(basename $1); \
	suffix=${file#*.}; \
	[ "$suffix" != "c" ] && exit; \
	stem=${file%.*}; \
	echo -n "$path/$stem.i: "; \
	make "$path/$stem.i"' \
	sh '{}' \;
set +x
