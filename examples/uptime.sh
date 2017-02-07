#!/bin/sh

#
# this will unconditionally cause a reboot if the uptime increases 2 days
# some hard disks might need this 
#

#
# get uptime info
#
upt=`cat /proc/uptime | tr -d . | cut -d' ' -f1`

#
# calculated days uptime, note that this number will never be greater than 2
#
days=`expr $upt / 8640000`

if [ $days -ge 2 ]
then
#	
#	return code 255 (-1 as unsigned 8-bit) means reboot
#
	exit 255
fi

exit 0
