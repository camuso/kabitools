#!/bin/bash
#
# kabilist
#

cmdline=kabilist

usagestr=$(
cat <<EOF

kabilist - Must be invoked from the top of the linux tree.
	   Takes no arguments.
	   Creates a list of all exported functions and their arguments
	   in ./redhat/kabi/kabi-parser.log. Destroys the contents of
	   any previous log, so mv or cp it if you want to save it.
\0
EOF
)

usage() {
	echo -e "$usagestr"
	exit
}

# [ $# -gt 0 ] || usage
[ "$1" == "-h" ] || [ "$!" == "--help" ] && usage

cat /dev/null > ./redhat/kabi/kabi-parser.log

 One file at a time method. Save as possible future option

find ./ -name \*.i -exec sh -c \
	'grep -qm1 "__ksymtab_" $1; \
	[ $? -eq 0 ] && ./redhat/kabi/kabi-parser $1 2>/dev/null' \
	sh '{}' \; | tee -a ./redhat/kabi/kabi-parser.log

#files=$(find ./ -type f -name \*.i)
#./redhat/kabi/kabi-parser $files | tee -a ./redhat/kabi/kabi-parser.log
