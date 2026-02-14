#!/bin/sh
# Kill the64
until [ `ps | grep -e the64 | wc -l` -eq 1 ]
do
        killall the64
done

mount -o remount,rw /mnt
cd /mnt

./gamepad_map

the64 &
