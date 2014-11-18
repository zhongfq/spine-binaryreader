#!/bin/bash

WORKPATH=`pwd`

for file in `find . -name "*.json"`
do
    if test -f $file
    then
        echo "Spine binary:" $file
        $WORKPATH/spine-cli/spine $file
        rm $file
    fi
done