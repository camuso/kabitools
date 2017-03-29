#!/bin/bash
#
# kabilist
#

toolkitversion="3.4"
fileversion="-1"

attr_bold="\033[1m"
attr_under="\033[4m"
attr_OFF="\033[0m"

usagestr=$(
cat <<EOF

$ $(basename $0) -d directory [-s subdir -f filelist -e errfile -V -h]

  - Given a path to the top of the kernel tree, this script calls the
    kabi-parser tool to create a kbg graph file from each .i file in
    the tree. Each kbg graph file contains an abstract of all exported
    symbols and all their dependencies. A list of kbg files is compiled
    for use by the kabi-lookup tool.

  -d directory - Required. Path to the topmost directory of the kernel tree.
  -s subdir    - Optional. Limit parsing to a sub directory relative
                 to the top of the kernel tree.
  -f filelist  - Optional. Default is redhat/kabi/kabi-datafiles.list
                 relative to the top of the kernel tree. This file will
                 contain a list of kbg graph files that were created from
                 .i files generated previously by the preprocessor.
                 If it already exists, this file will be destroyed and
                 rebuilt.
  -e errfile   - Optional error file. By default, errors are sent
                 to /dev/null
  -V           - Version of this file.
  -h           - This help message

\0
EOF
)

currentdir=$PWD
directory=""
subdir=""
datafile="kabi-data.dat"
errfile="/dev/null"

usage() {
	echo -e "$usagestr"
	exit 1
}

nodir() {
	echo -e $attr_bold
	echo -e "\tPlease specify the directory at the top of the kernel tree."
	echo -en $attr_OFF
	usage
}

noparser() {
	echo -e $attr_bold
	echo -e "\n\t\$directory/kabi-parser does not exist."
	echo -en $attr_OFF
	exit 1
}

noexistdir() {
	echo -e $attr_bold
	echo -e "\n\tDirectory $1 does not exist"
	echo -en $attr_OFF
	usage
}

while getopts "Vhd:s:f:e:" OPTION; do
    case "$OPTION" in

	d )	directory="$OPTARG"
		[ -d "$directory" ] || noexistdir $directory
		;;
	s )	subdir="$OPTARG"
		[ -d "$directory/$subdir" ] || noexistdir "$directory/$subdir"
		;;
	f )	filelist="$OPTARG"
		;;
	e )	errfile="$OPTARG"
		;;
	V )	echo "Version : $toolkitversion$fileversion"
		exit
		;;
	h )	usage
		;;
	* )	echo -e "\n\tunrecognized option: "$OPTARG"\n"
		usage
		exit 1
		;;
    esac
done

[ "$directory" ] || nodir
which kabi-parser
[ $? -eq 0 ] || noparser

outdir=$directory/"redhat/kabi"
filelist=$outdir/"kabi-datafiles.list"
echo "kabi file list: $filelist"
rm -vf $filelist

[ -d "$outdir" ] || mkdir -p $outdir

START=$(date +%s)

find $directory/$subdir -name \*.i -exec sh -c \
        'datafile="${1%.*}.kbg"; \
	kabi-parser -xo "$datafile" -f $1 -S -Wall_off 2>$2; \
	if [ -f "$datafile" ]; then \
                echo "$datafile" >> $3; \
                echo ${1%.*}; \
        fi;' \
	sh '{}' $errfile $filelist \;

END=$(date +%s)
DIFF=$(( $END - $START ))

minutes=$(( $DIFF / 60 ))
seconds=$(( $DIFF % 60 ))

echo
echo "Elapsed time: $minutes minutes and $seconds seconds"
echo
exit 0

# Legacy code, kept for now for interest. Script exits before executing
# the following.
#
echo
echo "File processing time: $minutes minutes and $seconds seconds"
echo
echo "Compressing data ..."

COMPSTART=$(date +%s)

redhat/kabi/kabi-compdb $datafile

COMPEND=$(date +%s)
COMPDIF=$(( $COMPEND - $COMPSTART ))
compmin=$(( $COMPDIF / 60 ))
compsec=$(( $COMPDIF % 60 ))

END=$(date +%s)
DIFF=$(( $END - $START ))
minutes=$(( $DIFF / 60 ))
seconds=$(( $DIFF % 60 ))

echo
echo "Compression time: $compmin minutes and $compsec seconds"

#
# tar --remove-files -uf $3 ${1%.*}.kbg;
