#!/bin/bash
#
# kabitools
#

cmdline=kabitools

usagestr=$(
cat <<EOF

kabitools directory

	Install the kabitools rpm and build the kernel kabi graph,
	where "directory" is the top of the kernel tree or a path
\0
EOF
)

usage() {
	echo -e "$usagestr"
	exit
}

[ $# -eq 1 ] || usage

declare dir="$1"
declare cpucount=$(cat /proc/cpuinfo | grep processor | wc -l)

[[ $cpucount > 1 ]] && let --cpucount

yum install -y http://people.redhat.com/tcamuso/kabitools/kabitools-3.5.3-3.el7.x86_64.rpm
cd "$dir"
patch -p1 < /usr/share/kabitools-rhel-kernel-make.patch
make -j $cpucount K=1
patch -R -p1 < /usr/share/kabitools-rhel-kernel-make.patch
cd -
