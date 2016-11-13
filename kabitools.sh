#!/bin/bash
#
# kabitools
#

cmdline=kabitools

usagestr=$(
cat <<EOF

kabitools.sh -[cjh]

	Must be invoked from the top of the kernel tree.

	-c	- make clean first
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

no_patch_str=$(
cat <<EOF

$(basename $0) could not find the correct patch for the kernel Makefiles.

Please check for the existence of:

/usr/share/kabitools-rhel-kernel-make.patch
/usr/share/kabitools-fedora-kernel-make.patch

\0
EOF
)

no_os_str=$(
cat <<EOF

$(basename $0) does not handle distro: $distro

\0
EOF
)

not_kernel_str=$(
cat <<EOF

$(basename $0) must be invoked from the top of a kernel tree.

You are here: $PWD

\0
EOF
)

usage() {
	echo -e "$usagestr"
	exit
}

# run if user hits control-c
#
control_c()
{
	echo -en "\nCtrl-c detected\nCleaning up and exiting.\n"
	patch -Rs -p1 < "$patch"
  	exit 127
}

[[ -f README ]] || { echo -e "$not_kernel_str"; exit 3; }

declare kerneltree="$(head -1 README)"
kerneltree=$(echo $kerneltree)
[[ ${kerneltree:0:20} == "Linux kernel release" ]] || \
	{ echo -e "not_kernel_str"; exit 4; }


declare cpus
declare clean=false
declare patch
declare distro="$(cat /etc/os-release | grep -w ID | cut -d'=' -f2)"

# strip outside quotes from the distro string
#
distro=$(sed -e 's/^"//' -e 's/"$//' <<<"$distro")

while getopts chj: OPTION; do
    case "$OPTION" in

	c )	clean=true
		;;
	h ) 	usage
		;;
	j )	cpus=$OPTARG
		;;
	* )	echo "unrecognized option"
		usage
		exit 1
    esac
done

case $distro in
	"rhel")		patch="/usr/share/kabitools-rhel-kernel-make.patch"
			;;
	"fedora")	patch="/usr/share/kabitools-fedora-kernel-make.patch"
			;;
	* )		echo -e "$no_patch_str"
			exit 1
			;;
esac

[[ -e "$patch" ]] || { echo -e "$no_os_str"; exit 2; }

patch -s -p1 < "$patch"

# trap keyboard interrupt (control-c)
trap control_c SIGINT

$clean && make clean
make -j $cpus K=1
patch -Rs -p1 < "$patch"
exit 0
