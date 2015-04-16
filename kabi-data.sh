#!/bin/bash
#
# kabilist
#

usagestr=$(
cat <<EOF

$0 -d <directory> -f <filelist> [-s subdir] | -h

  - Recursively finds .i files from the top of the directory given by
    required -d switch and sends them to the kabi-parse to create
    .kb_dat files to be used by kabi-lookup.

    This script uses the kabi-parser executable and expects it to be in
    the ./redhat/kabi/ directory relative to the top of the kernel tree.

  -d directory - Required. Directory at top of tree to be parsed.
  -s subdir    - Optional directory from which to start parsing, relative
                 to the top of the tree defined by the required "directory"
                 argument above.
                 Default is from the top of the kernel tree.
  -f filelist  - Optional path to filelist.
                 Default is ./redhat/kabi/parser/kabi-files.list
  -h           - This help message

\0
EOF
)

currentdir=$PWD
directory="$PWD"
subdir=""
datafile="kabi-data.dat"

usage() {
	echo -e "$usagestr"
	cd $currentdir
	exit 1
}

nodir() {
	echo -e "\n\tPlease specify a directory in the kernel tree."
	usage
}

noparser() {
	echo -e "\n\t\$directory/kabi-parser does not exist.\n\n"
	exit 1
}

noexistdir() {
	echo -e "\n\tDirectory $1 does not exist\n\n"
	nodir
}

while getopts "vhd:s:f:e:" OPTION; do
    case "$OPTION" in

	d )	directory="$OPTARG"
		[ "$directory" ] || nodir
		;;
	s )	subdir="$OPTARG"
		;;
	f )	filelist="$OPTARG"
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

[ -e "$directory/redhat/kabi/kabi-parser" ] || noparser
[ -d "$directory" ] || noexistdir $directory

if [ "$subdir" ]; then
	[ -d "$subdir" ] || noexistdir $subdir
fi

cd $directory
echo "executing from $PWD"

outdir=$PWD/"redhat/kabi/parser"
filelist=$outdir/"kabi-files.list"
[ -d "$outdir" ] || mkdir -p $outdir

rm -vf $filelist

echo "kabi file list: $filelist"

START=$(date +%s)

find $directory$subdir -name \*.i -exec sh -c \
	'grep -qm1 "__ksymtab_" $1; \
	if [ $? -eq 0 ]; then \
		echo $1; \
		./redhat/kabi/kabi-parser -xf ${1%.*}.kb_dat -Wno-sparse-error $1; \
		echo "${1%.*}.kb_dat" >> $2; \
	fi' \
	sh '{}' $filelist \;
echo
cd -
echo "returned to $PWD"
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
# tar --remove-files -uf $3 ${1%.*}.kb_dat;
