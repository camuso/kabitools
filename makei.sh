#!/bin/bash
#
# makei.sh
#

cmdline=makei

usagestr=$(
cat <<EOF

makei.sh <directory>

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

cpucount=$(cat /proc/cpuinfo | grep processor | wc -l)

# leave one processor on multi-processor systems.
#
[ $cpucount -gt 1 ] && let --cpucount

echo -e "\n********* Creating file list *********** \n"
echo -e "\tThis could take a while.\n"

files=$(find $dirspec -type f -name \*.c -exec sh -c \
	'path=$(dirname $1); \
	file=$(basename $1); \
	suffix=${file#*.}; \
	[ "$suffix" != "c" ] && exit; \
	stem=${file%.*}; \
	echo -n "$path/$stem.i "' \
	sh '{}' \;)

echo -e "\n************** make the .i files *****************\n"
echo -e "\tGo get some coffee. This will take some time."
echo
echo -e "\tThis script seeks to execute the gcc preprocessor"
echo -e "\ton every .c file in the tree starting at $dirspec."
echo
echo "$ make -k -j$cpucount \<list of .i files\>"
echo
make -k -j$cpucount $files 2>/dev/null

