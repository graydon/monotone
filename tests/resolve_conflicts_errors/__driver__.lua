-- Test that typical user errors with 'conflicts' give good error messages.

mtn_setup()

-- Conflict that is not supported; attribute
addfile("simple_file", "simple\none\ntwo\nthree\n")
commit("testbranch", "base")
base = base_revision()

check(mtn("attr", "set", "simple_file", "foo", "1"), 0, nil, nil)
commit("testbranch", "left")
left = base_revision()

revert_to(base)

check(mtn("attr", "set", "simple_file", "foo", "2"), 0, nil, nil)
commit("testbranch", "right")
right = base_revision()

check(mtn("conflicts", "store", left, right), 0, nil, true)
canonicalize("stderr")
check(samefilestd("conflicts-attr-store-1", "stderr"))

check(mtn("conflicts", "show_remaining"), 0, nil, true)
canonicalize("stderr")
check(samefilestd("conflicts-attr-show-1", "stderr"))

check(mtn("conflicts", "show_first"), 0, nil, true)
canonicalize("stderr")
check(samefilestd("conflicts-attr-show-2", "stderr"))

-- specify conflicts file not in bookkeeping dir
check(mtn("conflicts", "--conflicts-file", "conflicts", "store", left, right), 1, nil, true)
canonicalize("stderr")
check(samefilestd("conflicts-attr-store-2", "stderr"))

-- end of file
