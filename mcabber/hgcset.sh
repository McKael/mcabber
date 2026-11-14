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

if which git > /dev/null 2>&1 && [ "$(git rev-parse --is-inside-work-tree)" = "true" ]; then
  v=$(git rev-parse --short HEAD 2>/dev/null || echo unknown)
  if [ "$v" != unknown ] && [ -n "$(git status --porcelain 2>/dev/null)" ]; then
      v="$v-dirty"
  else
      v=$(git describe --tags --exact-match 2>/dev/null || echo "$v")
  fi
  echo "#define HGCSET \"g$v\"" > "$builddir/hgcset.h"
  exit
fi

echo > "$builddir/hgcset.h"
