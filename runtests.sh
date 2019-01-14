#!/bin/sh
set -e
cd tests
mkdir results || ( printf 'Please delete the old results directory before running again.\n' ; exit 1 )
for i in *.sdyn
do
    nm=${i%.sdyn}
    ../sdyn $i > results/$nm || true
    diff -u correct/$nm results/$nm
done
