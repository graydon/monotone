#!/usr/bin/python

# feed this file opreport -gcf on stdin, and it will spit out
# calltree on stdout

# http://kcachegrind.sourceforge.net/cgi-bin/show.cgi/KcacheGrindCalltreeFormat

import sys
import os.path

def main():
    convert(sys.stdin, sys.stdout)

# need to store:
#   bunch of function data
#   for each function, its file/line location, plus name
#     plus total samples directly in it
#     plus list of functions called
#       -- call count (which we don't have)
#       -- the file/line number/name of the function called
#       -- inclusive cost of those calls
def convert(op, ct):
    for line in op:
        if line.startswith("Counted "):
            ct.write("events: %s" % line.split(" ", 3)[1])
            continue
        if line.startswith("-" * 70):
            # found our first stanza
            break
    stanza = []
    for line in op:
        if line.startswith("-" * 70):
            process_stanza(stanza, ct)
            stanza = []
        else:
            stanza.append(line)

# some sample lines:
# 601802    9.9042  botan/sha160.cpp:54         /home/njs/src/monotone/vlogs-test/mtn-client _ZN5Botan7SHA_1604hashEPKh
# 1585865  26.0995  (no location information)   /usr/lib/libstdc++.so.6.0.7 (no symbols)
# we return the tuple: (count, filename, line, object, symbol)
def parse_line(line):
    rest = line.strip()
    count, percent, rest = rest.split(None, 2)
    if rest.startswith("(no location information)"):
        filename = "(unknown)"
        line = "0"
        blah, blah, blah, rest = rest.split(None, 3)
    else:
        filename_line, rest = rest.split(None, 1)
        assert ":" in filename_line
        filename, line = filename_line.split(":")
    object, rest = rest.split(None, 1)
    symbol = rest.strip()
    return (count, filename, line, object, symbol)

def process_stanza(stanza, ct):
    ct.write("\n")
    it = iter(stanza)
    # skip over the parent call info
    for line in it:
        # skip past the caller lines
        if line[0] not in "0123456789":
            continue
        # process the direct cost line
        count, filename, line, object, symbol = parse_line(line)
        ct.write("fl=%s\n" % filename)
        ct.write("ob=%s\n" % object)
        ct.write("fn=%s\n" % symbol)
        ct.write("%s %s\n" % (line, count))
        # save for later
        caller_line = line
        # start processing next set of lines
        break
    # process the cumulative child cost lines
    for line in it:
        if "[self]" in line:
            continue
        count, filename, line, object, symbol = parse_line(line)
        ct.write("cfi=%s\n" % filename)
        ct.write("cob=%s\n" % object)
        ct.write("cfn=%s\n" % symbol)
        # we don't know how many calls were made, so just hard-code to "1"
        # and we don't know the line number the calls were made from, so just
        # pretend everything came from the same line our function started on
        ct.write("calls=%s %s\n" % (1, line))
        ct.write("%s %s\n" % (caller_line, count))

if __name__ == "__main__":
    main()
