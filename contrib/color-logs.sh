#!/bin/sh

./monotone log --diffs --no-merges $@    \
  | ./contrib/colorize -c contrib/color-logs.conf  \
  | less -r -p -----------------------------------------------------------------
