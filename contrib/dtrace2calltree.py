#!/usr/bin/python

# to get calltree on stdout, feed to this file's stdin, the output of:
#
#----------------
#
# #!/usr/sbin/dtrace -qs
# syscall:::entry
# /pid == $target/
# {
#  self->start = timestamp;
# }
# syscall:::return
# /self->start/
# {
#  now=timestamp;
#  printf("syscall: %d\t%s\n", now - self->start, probefunc);
#  ustack();
#  self->start = 0;
# }
# 
#----------------
#
# It looks like:
#
#----------------
# syscall: 47029 munmap
#               ld.so.1`munmap+0x7
#               ld.so.1`leave+0x83
#               ld.so.1`call_init+0x41
#               ld.so.1`setup+0xe2c
#               ld.so.1`_setup+0x28c
#               ld.so.1`_rt_boot+0x56
#               0x80473dc
#
# syscall: 20017 mmap
#               libc.so.1`mmap+0x15
#               libc.so.1`lmalloc+0x6c
#               libc.so.1`atexit+0x1c
#               libc.so.1`libc_init+0x40
#               ld.so.1`call_init+0x46
#               ld.so.1`setup+0xe2c
#               ld.so.1`_setup+0x28c
#               ld.so.1`_rt_boot+0x56
#               0x80473dc
#
# syscall: 4092  fstat64
#               libc.so.1`fstat64+0x15
#               libc.so.1`opendir+0x3e
#               ls`0x8052605
#               ls`0x8051b2b
#               ls`main+0x637
#               ls`0x8051106
#
# syscall: 1234  fstat64
#               libc.so.1`mmap+0x15
#               libc.so.1`lmalloc+0x6c
#               libc.so.1`atexit+0x1c
#               libc.so.1`libc_init+0x40
#               ld.so.1`call_init+0x46
#               ld.so.1`setup+0xe2c
#               ld.so.1`_setup+0x28c
#               ld.so.1`_rt_boot+0x56
#               0x80473dc
#
#----------------

# See also:
#   http://kcachegrind.sourceforge.net/cgi-bin/show.cgi/KcacheGrindCalltreeFormat

import sys
import os.path

def main():
    data = read(sys.stdin)
    data.writeto(sys.stdout)

# We need to eventually get, for each function:
#   -- time spent directly in it
#   -- list of functions it calls
#      -- call count (which we don't have)
#      -- the file/line number/name of the function called
#      -- inclusive cost of those calls
# To do this, we have to accumulate everything in memory, then write it out.

class FnEntry:
    def __init__(self, obj_symbol):
        (self.obj, self.symbol) = obj_symbol
        self.cost = 0
        self.children = {}

    def charge(self, cost):
        self.cost += cost

    def charge_child(self, obj_symbol, cost):
        self.children.setdefault(obj_symbol, 0)
        self.children[obj_symbol] += cost

    def writeto(self, stream):
        stream.write("ob=%s\n" % self.obj)
        stream.write("fn=%s\n" % self.symbol)
        # <line> <direct cost>
        stream.write("%s %s\n" % (1, self.cost))
        for ((obj, symbol), cost) in self.children.iteritems():
            stream.write("cob=%s\n" % obj)
            stream.write("cfn=%s\n" % symbol)
            # <number calls> <location in file>
            stream.write("calls=1 1\n")
            # <caller line> <cost>
            stream.write("%s %s\n" % (1, cost))
        stream.write("\n")

class Data:
    def __init__(self):
        self.fns = {}

    def get_entry(self, obj_symbol):
        if obj_symbol not in self.fns:
            self.fns[obj_symbol] = FnEntry(obj_symbol)
        return self.fns[obj_symbol]
    
    # stack is like [("libc.so.1", "fstat64"), ("ls", "main")], with outermost
    # function last
    def charge(self, cost, stack):
        assert stack
        prev = None
        for obj_symbol in stack:
            entry = self.get_entry(obj_symbol)
            if prev is None:
                entry.charge(cost)
            else:
                entry.charge_child(prev, cost)
            prev = obj_symbol
                
    def writeto(self, stream):
        stream.write("events: syscall_wallclock\n")
        stream.write("\n")
        for entry in self.fns.itervalues():
            entry.writeto(stream)

def read_stack(stream):
    stack = []
    for line in stream:
        line = line.strip()
        if not line:
            break
        if "`" in line:
            obj, rest = line.split("`", 1)
            # libc.so.1`fstat64+0x15
            if "+" in rest:
                symbol, rest = rest.split("+", 1)
            # ls`0x8052605
            else:
                symbol = rest
            stack.append((obj, symbol))
        else:
            # 0x80473dc
            stack.append(("?", line))
    return stack

def read(stream):
    data = Data()
    for line in stream:
        if not line.startswith("syscall:"):
            # skip over nonsense
            continue
        else:
            # we have the beginning of a sample
            syscall_marker, cost_str, syscall = line.strip().split()
            assert syscall_marker == "syscall:"
            cost = int(cost_str)
            stack = read_stack(stream)
            data.charge(cost, [("kernel", syscall)] + stack)
    return data

if __name__ == "__main__":
    main()
