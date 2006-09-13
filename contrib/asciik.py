#!/usr/bin/python

# BUGS:
#
# 1)
#
# | | | | | o |     | |
# | | | | | | |    / / 
# | | | | o | | .-.-|
# | | | | |/ / / /
#
# ^^ seems to be jumping two columns?  It is not supposed to do that.
#
#
# 2)
# 
# | | | | | |/ / /
# | | | o | | | |    9d60709de95de51104c80696aeee771b5e3bba69
# | | |  \ / / / 
# | | o-. | | |    0b96de216534d45b353bd94199ce7055de2d3048
# | | | |\| | |
#
# should be:
#
# | | | | | |/ / /
# | | | o | | | |    9d60709de95de51104c80696aeee771b5e3bba69
# | | |  X / / / 
# | | o-. | | |    0b96de216534d45b353bd94199ce7055de2d3048
# | | | |\| | |
#
# or:
#
# | | | | | |/ / /
# | | | o-. | | |    9d60709de95de51104c80696aeee771b5e3bba69
# | | |   |\| | | 
# | | o---. | | |    0b96de216534d45b353bd94199ce7055de2d3048
# | | |   |\| | |
# | | |   | | o |
# | | |  / / / /
#
# 3)
#
# | | | | | | |\ \ \ \ 
# | | | | | | o | | | |    145c71fb56cff358dd711773586ae6b5219b0cfc
# | | | | | | |\ \ \ \ \ 
#
# should be
# 
# | | | | | | |\ \ \ \ 
# | | | | | | o \ \ \ \    145c71fb56cff358dd711773586ae6b5219b0cfc
# | | | | | | |\ \ \ \ \
#
# need some sort "inertia", if we moved sideways before and are moving
# sideways now...


# How this works:
#   This is completely iterative; we have no lookahead whatsoever.  We output
#     each line before even looking at the next.  (This means the layout is
#     much less clever than it could be, because there is no global
#     optimization; but it also means we can calculate these things in zero
#     time, incrementally while running log.)
#
#   Output comes in two-line chunks -- a "line", which contains exactly one
#   node, and then an "interline", which contains edges that will link us to
#   the next line.
#
#   A design goal of the system is that you can always trivially the space
#   between two "lines", by adding another "| | | |"-type interline after the
#   real interline.  This allows us to put arbitrarily long annotations in the
#   space to the right of the graph, for each revision.
#   
# Loop:
#   We start knowing, for each logical column, what thing has to go there
#     (because this was determined last time)
#   We use this to first determine what thing has to go in each column next
#     time (though we will not draw them yet).
#   This is somewhat tricky, because we do want to squish things towards the
#   left when possible.  However, we have very limited drawing options -- we
#   can slide several things 1 space to the left or right and do no other long
#   sideways edges; or, we can draw 1 or 2 long sideways edges, but then
#   everything else must go straight.  So, we try a few different layouts.
#   The options are, remove a "ghost" if one exists, don't remove a ghost, and
#   insert a ghost.  (A "ghost" is a blank space left by a line that has
#   terminated or merged back into another line, but we haven't shifted things
#   over sideways yet to fill in the space.)
#
#   Having found a layout that works, we draw lines connecting things!  Yay.

import sys

# returns a dict {node: (parents,)}
def parsegraph(f):
    g = {}
    for line in f:
        pieces = line.strip().split()
        g[pieces[0]] = tuple(pieces[1:])
    return g

# returns a list from bottom to top
def parseorder(f):
    order = []
    for line in f:
        order.append(line.strip())
    order.reverse()
    return order

# takes two files:
#   one is output of 'automate graph'
#   other is output of 'automate select i: | automate toposort -@-'
def main(name, args):
    assert len(args) == 2
    graph = parsegraph(open(args[0]))
    order = parseorder(open(args[1]))

    row = []
    for rev in order:
        row = print_row(row, rev, graph[rev])
    
def print_row(curr_row, curr_rev, parents):
    if curr_rev not in curr_row:
        curr_row.append(curr_rev)
    curr_loc = curr_row.index(curr_rev)

    new_revs = []
    for p in parents:
        if p not in curr_row:
            new_revs.append(p)
    next_row = list(curr_row)
    next_row[curr_loc:curr_loc + 1] = new_revs
    
    # now next_row contains exactly the revisions it needs to, except that no
    # ghost handling has been done.

    no_ghost = without_a_ghost(next_row)
    if try_draw(curr_row, no_ghost, curr_loc, parents):
        return no_ghost
    if try_draw(curr_row, next_row, curr_loc, parents):
        return next_row
    if not new_revs: # this line has disappeared
        extra_ghost = with_a_ghost_added(next_row, curr_loc)
        if try_draw(curr_row, extra_ghost, curr_loc, parents):
            return extra_ghost
    assert False

def without_a_ghost(next_row):
    wo = list(next_row)
    if None in next_row:
        wo.pop(next_row.index(None))
    return wo

def with_a_ghost_added(next_row, loc):
    w = list(next_row)
    w.insert(loc, None)
    return w

def try_draw(curr_row, next_row, curr_loc, parents):
    curr_items = len(curr_row)
    next_items = len(next_row)
    curr_ghosts = []
    for i in xrange(curr_items):
        if curr_row[i] is None:
            curr_ghosts.append(i)
    links = []
    have_shift = False
    for rev in curr_row:
        if rev is not None and rev in next_row:
            i = curr_row.index(rev)
            j = next_row.index(rev)
            if i != j:
                have_shift = True
            links.append((i, j))
    for p in parents:
        i = curr_loc
        j = next_row.index(p)
        if abs(i - j) > 1 and have_shift:
            return False
        links.append((i, j))

    draw(curr_items, next_items, curr_loc, links, curr_ghosts, curr_row[curr_loc])
    return True

def draw(curr_items, next_items, curr_loc, links, curr_ghosts, annotation):
    line = [" "] * (curr_items * 2 - 1)
    interline = [" "] * (max(curr_items, next_items) * 2 - 1)

    # first draw the flow-through bars in the line
    for i in xrange(curr_items):
        line[i * 2] = "|"
    # but then erase it for ghosts
    for i in curr_ghosts:
        line[i * 2] = " "
    # then the links
    for i, j in links:
        if i == j:
            interline[2 * i] = "|"
        else:
            if j < i:
                # | .---o
                # |/| | |
                # 0 1 2 3
                # j     i
                # 0123456
                #    s  e
                start = 2*j + 3
                end = 2*i
                dot = start - 1
                interline[dot - 1] = "/"
            else: # i < j
                # o---.
                # | | |\|
                # 0 1 2 3
                # i     j
                # 0123456
                #  s  e
                start = 2*i + 1
                end = 2*j - 2
                dot = end
                interline[dot + 1] = "\\"
            if end - start >= 1:
                line[dot] = "."
            line[start:end] = "-" * (end - start)
    # and add the main attraction (may overwrite a ".")
    line[curr_loc * 2] = "o"

    print "".join(line) + "    " + annotation
    print "".join(interline)

if __name__ == "__main__":
    main(sys.argv[0], sys.argv[1:])
