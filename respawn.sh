#!/bin/bash

# This script is a simple respawn deamon for those of us who dont want
# to deal with the /etc/event.d, monit etc...
# Usage: sh respawn.sh [program] [sleep time]

while [ true ]
do
	sleep $2
	if ps ax | grep -v grep | $1 > /dev/null
	then
		echo $1": Stopped. Restarting in "$2" seconds."
	else
		$1 &
	fi
done
