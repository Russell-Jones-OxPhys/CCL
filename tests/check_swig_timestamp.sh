#!/usr/bin/env bash
# Check to see whether SWIG-generated files have been updated after the most 
# recent changes to the CCL interface (or the SWIG interface definition)

# Make sure we run tests from the correct path
parent_path=$( cd "$(dirname "${BASH_SOURCE[0]}")" ; pwd -P )
cd "$parent_path"


# Print this message if the SWIG files are outdated
msg1="Autogenerated SWIG files appear to be outdated."
msg2="Changes have been made to CCL headers or SWIG interface files since SWIG was last run. Please re-run SWIG and commit the updated autogenerated files:"
swig_command="~$ swig -python -threads -o pyccl/ccl_wrap.c pyccl/ccl.i"

# Time of last modification of SWIG-generated files
t1_swig_pylib=`git log --pretty=format:%ct -n 1 ../pyccl/ccllib.py`
t1_swig_cwrap=`git log --pretty=format:%ct -n 1 ../pyccl/ccl_wrap.c`

# Test against CCL C headers, CLASS headers, and CCL interface files
for hdr in `ls ../include/*.h ../class/include/*.h ../pyccl/*.i`;
do
    last_commit=`git log --pretty=format:%ct -n 1 $hdr`
    echo $hdr
    if [ ! -z "$last_commit" ];
    then
	if [ "$last_commit" -gt "$t1_swig_pylib" ] || [ "$last_commit" -gt "$t1_swig_cwrap" ];
	then
	    echo $last_commit
	    echo $t1_swig_pylib
	    echo $t1_swig_cwrap
	    echo $msg1
	    echo ""
	    echo $msg2
	    echo $swig_command
	    exit 1
	fi
    fi
done

# If everything was up to date, tell the user
echo "Autogenerated SWIG files appear to be up to date. Make sure you've committed any changes."
echo "(This is not guaranteed though, so re-run SWIG if you are experiencing issues with the Python wrapper.)"
