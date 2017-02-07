#!/bin/sh

#
# check if ORACLE database instance is still reacting to ping requests
#

#
# name of the instance (must be defined in tnsnames.ora)
#
DATABASE=$1

#
# check the state:
# tnsping returns 0 on success, otherise the return value is 1
#
tnsping $DATABASE > /dev/null 2>&1
result=$?

if [ $result -ne 0 ]
then
#
#	obviously there is no system error code for this
#	so we have to create our own (system codes are > 0): -2.
#	note that 255 (-1 as unsigned 8-bit) means reboot
#	and 254 (-2) means hard reset.
#
	result=254
fi

exit $result
