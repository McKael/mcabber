#! /bin/sh

srcdir="$1"
builddir="$2"

if [ ! -f "$srcdir/logprint.h" ]; then
  echo "Unable to find mcabber sources!" >&2
  exit 1
fi

cd "$srcdir"

if which hg > /dev/null 2>&1; then
  cs=$(hg id 2> /dev/null | cut -d' ' -f1)
  if test $? -eq 0; then
    if [ x"$cs" != x ]; then
      grep -q "$cs" "$builddir/hgcset.h" > /dev/null 2>&1 || \
        echo "#define HGCSET \"$cs\"" > "$builddir/hgcset.h"
      exit 0
    fi
  fi
fi

echo > "$builddir/hgcset.h"
