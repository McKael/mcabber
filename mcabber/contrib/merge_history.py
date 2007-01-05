#!/usr/bin/env python
# This script is provided under the terms of the GNU General Public License,
# see the file COPYING in the root mcabber source directory.
#
# Frank Zschockelt, 05.01.2007

import sys

if(len(sys.argv) != 3):
  print "usage:",sys.argv[0],"histA histB > histA+B"
  sys.exit(0)
file=open(sys.argv[1], "r")
linesA=file.readlines()
file.close()
file=open(sys.argv[2], "r")
linesB=file.readlines()
file.close()

i=j=0
while(i<len(linesA) and j < len(linesB)):
  if(linesA[i][3:20] <= linesB[j][3:20]):
    l=int(linesA[i][22:25])
    for s in linesA[i:i+l+1]:
      print s,
    if(linesA[i]==linesB[j]):
      j+=l+1
    i+=l+1
  else:
    l=int(linesB[j][22:25])
    for s in linesB[j:j+l+1]:
      print s,
    j+=l+1

for s in linesA[i:len(linesA)]:
  print s,
for s in linesB[j:len(linesB)]:
  print s,

