#!/bin/sh
# This is my main script
# First need to source the config file
# if we can not read the cfg then exit!
if [ -r /home/francesco/Desktop/ARP/config.cfg ]
then
# getting config file
. /home/francesco/Desktop/ARP/config.cfg
else
echo "Can not read config.cfg"
exit 99
fi

gcc processG.c -o Gproc
gcc assignment.c -o main
./main "$logfile" "$IP_ADDRESS_PREV" "$IP_ADDRESS_MY" "$IP_ADDRESS_NEXT" "$RF" "$process_G_path" "$portno" "$DEFAULT_TOKEN" "$counter" "$waiting_time"
