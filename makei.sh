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

START1=$(date +%s)

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


END=$(date +%s)
DIFF=$(( $END - $START1 ))

minutes=$(( $DIFF / 60 ))
seconds=$(( $DIFF % 60 ))
echo
echo "That took $minutes minutes and $seconds seconds."
echo

START2=$(date +%s)

echo -e "\n************** make the .i files *****************\n"
echo -e "\tGo get some coffee. This will take some time."
echo
echo -e "\tThis script seeks to execute the gcc preprocessor"
echo -e "\ton every .c file in the tree starting at $dirspec."
echo
echo "$ make -k -j$cpucount \<list of .i files\>"
echo
make -k -j$cpucount $files 2>/dev/null

END=$(date +%s)
DIFF=$(( $END - $START2 ))

minutes=$(( $DIFF / 60 ))
seconds=$(( $DIFF % 60 ))
echo
echo "That took $minutes minutes and $seconds seconds."
echo

DIFF=$(( $END - $START1 ))

minutes=$(( $DIFF / 60 ))
seconds=$(( $DIFF % 60 ))
echo
echo "Total running time: $minutes minutes and $seconds seconds."
echo


