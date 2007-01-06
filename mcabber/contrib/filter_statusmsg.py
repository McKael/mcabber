#!/usr/bin/env python
# This script can be used to delete status messages from history files.
#
# If you want to clean all histories from status messages:
# $ for i in ~/.mcabber/histo/*; do ./filter_statusmsg.py $i > foo; mv foo $i; done
#
# Frank Zschockelt, 05.01.2007
import sys

if(len(sys.argv) != 2):
  print "usage:",sys.argv[0],"history > history_without_status"
  sys.exit(0)
file=open(sys.argv[1], "r")
lines=file.readlines()
file.close()

i=0
while(i<len(lines)):
  l=int(lines[i][22:25])
  if(lines[i][0] != 'S'):
    for s in lines[i:i+l+1]:
      print s,
  i+=l+1
