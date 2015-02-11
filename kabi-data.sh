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
  -b datalog  -  Optional. Default is ../kabi-data.csv relative to
                 the top of the kernel tree. This file will contain the
                 parser output of the hierarchical kabi data.
                 If it already exists, this csv file will be destroyed and
                 rebuilt.
  -B datafile  - Optional database file. The default is ../kabi-data.sql
                 relative to the top of the kernel tree.
                 If it already exists, the database file will be destroyed
                 and rebuilt.
  -t typelog   - Optional. Default is  ../kabi-type.csv relative to the
                 top of the kernel tree. This file will contian the parser
                 output of the compound data type definitions and all their
                 descendants.
                 If it already exists, this csv file will be destroyed and
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
datalog="../kabi-data.csv"
typelog="../kabi-types.csv"
datafile="../kabi-data.sql"
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
	b )	datalog="$OPTARG"
		;;
	B )	datafile="$OPTARG"
		;;
	t )	typelog="$OPTARG"
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

cat /dev/null > $datalog
cat /dev/null > $typelog
[ -d "$subdir" ] || noexistdir $subdir
[ -e "$datafile" ] && rm -vf $datafile

START=$(date +%s)

find $subdir -name \*.i -exec sh -c \
	'grep -qm1 "__ksymtab_" $3; \
	if [ $? -eq 0 ]; then \
		echo $3; \
		redhat/kabi/kabi-parser -d $1 -t $2 $3 2>$4; \
	fi' \
	sh $datalog $typelog '{}' $errfile \;

echo
echo "Importing csv files:"
echo -e "\t$datalog"
echo -e "\t$typelog"
echo "to database: $datafile"
echo
echo "This can take a couple minutes."
echo
sqlite3 $datafile <<EOF
create table ktree (level integer, left integer64, right integer64, flags integer, prefix text, decl text, parentdecl text);
create table kabitree (rowid integer primary key, level integer, left integer64, right integer64, flags integer, prefix text, decl text, parentdecl text);
create table ktype (level integer, left integer64, right integer64, flags integer, decl text);
create table kabitype (rowid integer primary key, level integer, left integer64, right integer64, flags integer, decl text);
.separator ','
.import $datalog ktree
.import $typelog ktype
insert into kabitree (level, left, right, flags, prefix, decl, parentdecl) select * from ktree;
insert into kabitype (level, left, right, flags, decl) select * from ktype;
drop table ktree;
drop table ktype;
EOF

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

