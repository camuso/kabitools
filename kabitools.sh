#!/bin/bash
#
# kabitools
#

cmdline=kabitools

usagestr=$(
cat <<EOF

kabitools.sh -[j | h]

	Must be invoked from the top of the kernel tree.

	-j	- Optional number of processors to assign to the make task.
	-h	- This help screen.

	Runs the following make commands with the optional -j specifier
	for SMP support.

	make clean
	make 		# compiles all files necessary to make vmlinux
	make modules	# compiles all kmods

	While compiling the kernel and kmods, builds a graph from the
	preprocessor output of each file compiled.

\0
EOF
)

usage() {
	echo -e "$usagestr"
	exit
}

declare cpus

while getopts hj: OPTION; do
    case "$OPTION" in

	h ) 	usage
		;;
	j )	cpus=$OPTARG
		;;
	* )	echo "unrecognized option"
		usage
		exit 1
    esac
done

patch -p1 < /usr/share/kabitools-rhel-kernel-make.patch
make -j $cpus K=1
patch -R -p1 < /usr/share/kabitools-rhel-kernel-make.patch
