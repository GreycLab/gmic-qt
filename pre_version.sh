#!/bin/bash
# workaround for MSYS2 call of date by qmake.
# Honors SOURCE_DATE_EPOCH (reproducible builds) when it is set.
if [ -n "${SOURCE_DATE_EPOCH:-}" ]; then
  date -u -d "@${SOURCE_DATE_EPOCH}" "+%y%m%d%H" 2>/dev/null \
    || date -u -r "${SOURCE_DATE_EPOCH}" "+%y%m%d%H" 2>/dev/null \
    || date "+%y%m%d%H"
else
  date "+%y%m%d%H"
fi
