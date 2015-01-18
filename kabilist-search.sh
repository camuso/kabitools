#!/bin/bash
#
# kabilist-search
#

cmdline=kabilist-search

usagestr=$(
cat <<EOF

kabilist-search
\0
EOF
)

usage() {
	echo -e "$usagestr"
	exit
}

#[ $# -gt 1 ] || usage

pattern="$1"

while read line; do
	[[ $(echo $line | grep EXPORTED) ]] &&
		exported="$(echo $line | cut -d' ' -f2-)"
	if [[ $(echo $line | grep "$pattern") ]]; then
		srchline="$(echo $line | cut -d' ' -f2-)"
		echo "$srchline IN $exported"
	fi
done < ./redhat/kabi/kabi-parser.log

