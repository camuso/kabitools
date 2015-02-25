#!/bin/bash
#
# kabi-hpar.sh
#

cmdline=fodir

usagestr=$(
cat <<EOF

kabi-hpar.sh
\0
EOF
)

count=0
pflag=false

usage() {
	echo -e "$usagestr"
	exit
}

#[ $# -gt 0 ] || usage

START=$(date +%s)

for d in $(ls -1d */); do
	if [ -d "$d" ]; then
		dname="${d%/*}"
		echo "subdir: $dname"
		./redhat/kabi/kabi-data.sh -d ./ \
			-s "$dname" \
			-b ../"$dname".csv \
			-B ../"$dname".sql \
			-t ../"$dname"-type.csv

#		size=$(du $d -hs)
#		echo "$d size: $size"
#		let count++;
#
#		cd $d;
#
#		for dd in $(ls -1d */); do
#			if [ -d "$dd" ]; then
#				size=$(du $dd -hs)
#				echo -e "\t$dd size: $size"
#				let count++;
#			fi;
#		done;
#		cd ..
	fi
done;

END=$(date +%s)
DIFF=$(( $END - $START ))

minutes=$(( $DIFF / 60 ))
seconds=$(( $DIFF % 60 ))
echo
echo "Total Rendering Time: $minutes minutes and $seconds seconds"
echo
