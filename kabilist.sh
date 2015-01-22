#!/bin/bash
#
# kabilist
#

usagestr=$(
cat <<EOF

$0 [OPTIONS]

  - Creates a list of all exported functions and their arguments
    in ./redhat/kabi/kabi-parser.log. Destroys the contents of
    any previous log, so mv or cp it if you want to save it.

  -d directory - Required. Directory at top of tree to be checked
  -o outfile   - Required. The output file. Contents of existing
                 file will be detroyed.
  -v           - Vverbose output
  -h           - This help message

\0
EOF
)

usage() {
	echo -e "$usagestr"
	exit
}

nodir() {
	echo -e "\n\tPlease specify a directory at the top of the kernel\n\
		tree\n\\n\
		$ kabilist -d <directory> -o <output file>\n\n"
	exit
}

nofil() {
	echo -e "\n\tPlease specify an ouput file to contain the output.\n\n\
		$ kabilist -d <directory> -o <output file>\n\n"
	exit
}

verbose=false
directory=""
outfile=""

while getopts "vd:o:" OPTION; do
    case "$OPTION" in

        d ) 	directory="$OPTARG"
		[ "$directory" ] || nodir
		;;
        o )	outfile="$OPTARG"
		[ "$outfile" ] || nofil
		;;
        v )	verbose=true
		;;
	h )	usage
		;;
        * ) 	echo -e "\n\tunrecognized option: "$OPTARG"\n"
            	usage
            	exit 1
	    	;;
    esac
done

[ "$directory" ] || nodir
[ "$outfile" ] || nofil

cat /dev/null > $outfile

# One file at a time method.

if  $verbose ; then
	find $directory -name \*.i -exec sh -c \
		'grep -qm1 "__ksymtab_" $2; \
		if [ $? -eq 0 ]; then \
			$1/redhat/kabi/kabi-parser $2; \
		fi' \
		sh $directory '{}'  \; | tee -a "$outfile"
else
	find $directory -name \*.i -exec sh -c \
		'grep -qm1 "__ksymtab_" $2; \
		if [ $? -eq 0 ]; then \
			echo $2; \
			$1/redhat/kabi/kabi-parser $2 >> $3; \
		fi' \
		sh $directory '{}' $outfile \;
fi

#files=$(find ./ -type f -name \*.i)
#./redhat/kabi/kabi-parser $files | tee -a ./redhat/kabi/kabi-parser.log

