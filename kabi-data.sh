#!/bin/bash
#
# kabilist
#

usagestr=$(
cat <<EOF

$0 -d <directory> -o <textfile> [-s subdir -b datafile -e errfile -v -h]

  - Creates a SQLite database and a text file listing all exported
    functions, their arguments, their return values, and any data
    structures explicitly or implicitly used by them.

    This script uses the kabi-parser executable and expects it to be in
    the redhat/kabi/ directory.

  -d directory - Required. Directory at top of tree to be parsed.
  -s subdir    - Optional directory from which to start parsing, relative
                 to the top of the tree defined by the required "directory"
                 argument above.
                 Default is from the top of the kernel tree.
  -b database  - Optional. Default is ../kabi-data.dat relative to
                 the top of the kernel tree. This file will contain the
                 parser output of the hierarchical kabi data graph.
                 If it already exists, this file will be destroyed and
                 rebuilt.
  -e errfile   - Optional error file. By default, errors are sent
                 to /dev/null
  -v           - Verbose output
  -h           - This help message

\0
EOF
)

currentdir=$PWD
verbose=false
directory=""
subdir="./"
datafile="../kabi-data.dat"
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

while getopts "vhd:s:B:b:t:e:" OPTION; do
    case "$OPTION" in

	d )	directory="$OPTARG"
		[ "$directory" ] || nodir
		;;
	s )	subdir="$OPTARG"
		;;
	b )	datafile="$OPTARG"
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

[ -e "$directory/redhat/kabi/kabi-parser" ] || noparser
[ -d "$directory" ] || noexistdir $directory

cd $directory
echo "executing from $PWD"

cat /dev/null > $datafile
[ -d "$subdir" ] || noexistdir $subdir

START=$(date +%s)

find $subdir -name \*.i -exec sh -c \
	'grep -qm1 "__ksymtab_" $2; \
	if [ $? -eq 0 ]; then \
		echo $2; \
		redhat/kabi/kabi-parser -d $1 $2 2>$3; \
	fi' \
	sh $datafile '{}' $errfile \;
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
