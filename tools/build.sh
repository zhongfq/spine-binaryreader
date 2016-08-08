#!/bin/bash

WORKPATH=`dirname $0`

case "`uname`" in
    CYGWIN*)
        SPINEC=$WORKPATH/spine-cli/spinec.exe
        ;;
        *)
        SPINEC=$WORKPATH/spine-cli/spinec
        ;;
esac

for file in `find . -name "*.json"`
do
    if test -f $file
    then
        echo "Spine binary:" $file
        $SPINEC -o ${file/.json/.skel} $file
        rm $file
    fi
done