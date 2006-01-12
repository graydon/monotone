#!/bin/sh

./monotone log --diffs $@    \
  | ./contrib/colorize -c contrib/color-logs.conf  \
  | less -r -p -----------------------------------------------------------------
