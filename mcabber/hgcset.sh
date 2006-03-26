#! /bin/sh

if [ ! -f logprint.h ]; then
  echo "You are not in the src directory" >&2
  exit 1
fi

if which hg > /dev/null 2>&1; then
  cs=$(hg id 2> /dev/null | cut -d' ' -f1)
  if test $? -eq 0; then
    grep -q "$cs" hgcset.h > /dev/null 2>&1 || \
      echo "#define HGCSET \"$cs\"" > hgcset.h
    exit 0
  fi
fi

echo > hgcset.h
