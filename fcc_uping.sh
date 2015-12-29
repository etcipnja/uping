#!/bin/bash

function check_client() 
{ 
	CLIENT_NAME=`echo $1 | cut -f1 -d:`
	CLIENT_PID=$(echo $1 | cut -f2 -s -d:)	
	CLIENT_NAME=$(eval "echo $CLIENT_NAME")
        CLIENT_PID=$(eval "echo $CLIENT_PID")

	if [ "$CLIENT_PID" != "" ]; then
                if [ -f /proc/$CLIENT_PID/cmdline ]; then
                        NAME1=`cat /proc/$CLIENT_PID/cmdline | tr -d '[[:space:]]'` 
                        NAME2=`echo $CLIENT_NAME | tr -d '[[:space:]]'`
			if [ "$NAME1" != "$NAME2" ]; then
                                CLIENT_PID="";
                        fi
                else 
                        CLIENT_PID="";
                fi
        fi

        if [ "$CLIENT_PID" == "" ]; then
                echo client is not running, starting it as "[" $CLIENT_NAME "]"
                $CLIENT_NAME & 
                CLIENT_PID=$!
        fi      
}


function check_clients() 
{
	arraylength=${#SYSTEMS[@]}
	for (( i=1; i<${arraylength}+1; i++ ));
	do 
		check_client "${SYSTEMS[$i-1]}"
		SYSTEMS[$i-1]="$CLIENT_NAME : $CLIENT_PID"
	done
}

declare -a SYSTEMS=(
        './uping.x 127.0.0.1 -p $SERVER_PORT -q -i.1'
	'./uping.x -d -p $SERVER_PORT'
	)

while true
do
	check_clients
	sleep 5 
done
 
