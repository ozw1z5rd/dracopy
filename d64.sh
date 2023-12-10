#!/bin/sh
if [ -z "$TYPE" ]
then TYPE=d64
fi

export C1541=/Users/bigfoot/Desktop/vice-x86-64-gtk3-3.7.1/bin/c1541

$C1541 -format "$1" $TYPE $2

cmd="-attach $2"
shift
shift

for i in "$@" ; do
    cmd="$cmd -write $i"
done

$C1541 $cmd
