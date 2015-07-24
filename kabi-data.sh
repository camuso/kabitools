#!/bin/bash
#
# kabilist
#

usagestr=$(
cat <<EOF

$0 -d <directory> -d directory [-s subdir -f filelist -e errfile ] [-h]

  - Given a directory tree, calls kabi-parser to create a graph file
    from each .i file in the tree. The graph is a representation of all
    exported symbols and all their dependencies, and all instances of
    those dependencies in each graph.

  -d directory - Required. Directory at top of tree to be parsed.
  -s subdir    - Optional directory from which to start parsing, relative
                 to the top of the tree defined by the required "directory"
                 argument above.
                 Default is from the top of the kernel tree.
  -f filelist  - Optional. Default is redhat/kabi/parser/kabi-files.list
                 relative to the top of the kernel tree. This file will
		 contain a list of graph files that were created from .i
		 files generated previously by the preprocessor.
                 If it already exists, this file will be destroyed and
                 rebuilt.
  -e errfile   - Optional error file. By default, errors are sent
                 to /dev/null
  -h           - This help message

\0
EOF
)

currentdir=$PWD
verbose=false
directory=""
subdir="./"
datafile="kabi-data.dat"
errfile="/dev/null"

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
	e )	errfile="$OPTARG"
		;;
	v )	verbose=true
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

# [ -e "$directory/redhat/kabi/kabi-parser" ] || noparser
[ -d "$directory" ] || noexistdir $directory

cd $directory
echo "executing from $PWD"

outdir=$PWD/"redhat/kabi/parser"
filelist=$outdir/"kabi-files.list"
[ -d "$outdir" ] || mkdir -p $outdir

rm -vf $filelist
[ -d "$subdir" ] || noexistdir $subdir

echo "kabi file list: $filelist"

START=$(date +%s)

find $subdir -name \*.i -exec sh -c \
	'echo $1; \
        grep -qm1 ksymtab $1; \
        [ $? -eq 0 ] || exit; \
	kabi-parser -xf ${1%.*}.kb_dat $1 2>$2; \
	echo "${1%.*}.kb_dat" >> $3;' \
	sh '{}' $errfile $filelist \;
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
