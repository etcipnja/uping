#!/bin/bash

INTERVAL=5
SERVER_PORT=7
UPING="./uping.x"
UPING_OPT="-i.05 -q"
TEMP_DIR=/tmp/uping


#-------------------------------------------------------------------------------
function save_stats() 
{
	if [ $# -eq 4 ]; then 
		SERVER_NAME=$1
		SERVER_IP=$2
		LOSS=$3
		DELAY=$4

        	echo "$SERVER_NAME $SERVER_IP LOSS $LOSS DELAY $DELAY"
	fi
}

#-------------------------------------------------------------------------------
# checks if uping app is runing (arguments passed in $1) and starts if necessary
# uping outputs reults to /tmp/uping/<LOCATION>
function check_client() 
{
	CLIENT_LOC=`echo $1 | cut -f1 -d:`
	CLIENT_FILE=`echo $1 | cut -f2 -d:` 
	CLIENT_NAME=`echo $1 | cut -f3 -d:`
	CLIENT_PID=$(echo $1 | cut -f4 -s -d:)	

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
                echo $CLIENT_LOC is not running, starting it as "[" $CLIENT_NAME "]"
                $CLIENT_NAME >>$TEMP_DIR/$CLIENT_FILE 2>&1 &
                CLIENT_PID=$!
        fi      
}

#-------------------------------------------------------------------------------
function check_clients() 
{
	arraylength=${#SYSTEMS[@]}
	for (( i=1; i<${arraylength}+1; i++ ));
	do 
		check_client "${SYSTEMS[$i-1]}"
		SYSTEMS[$i-1]="$CLIENT_LOC:CLIENT_FILE:$CLIENT_NAME:$CLIENT_PID"
	done
}

#-------------------------------------------------------------------------------
function collect_stats() 
{
	killall -SIGUSR1 uping.x
	for f in $TEMP_DIR/*
	do
		SERVER_NAME=`echo "${f##*/}" | cut -f1 -d@`
        	SERVER_IP=`echo "${f##*/}" | cut -s -f2 -d@`
 		
		if [ "$SERVER_IP" != "" ]; then
			LOSS=`cat $f | grep loss | awk '{ ret=$6 } END {print ret}' | sed -e 's/%//'`
			DELAY=`cat $f | grep rtt | awk 'BEGIN { FS="/"} { ret=$4 } END {print ret}'`

			save_stats $SERVER_NAME $SERVER_IP $LOSS $DELAY
		fi 
	done
}

#-------------------------------------------------------------------------------
function initialize() 
{
	mkdir -p $TEMP_DIR
	rm -rf $TEMP_DIR/*

	IFS=$'\r\n' command eval 'SITES=($(curl http://ow-zabbix.int/z/custom_pages/data 2> /dev/null))'

	SYSTEMS[0]="SELF:server:${UPING} -d -p $SERVER_PORT"
	arraylength=${#SITES[@]}
        for (( i=1; i<${arraylength}+1; i++ ));
        do
		SITE=(${SITES[$i-1]})		
		SYSTEMS[$i]="${SITE[0]}:${SITE[3]}@${SITE[1]}:./uping.x ${SITE[1]} -p $SERVER_PORT $UPING_OPT" 
	done
}

#-------------------------------------------------------------------------------
initialize
while true
do
	for (( v=0; v<$INTERVAL; v++ ));
        do
		check_clients
		sleep 1
	done 
	collect_stats
done
 
