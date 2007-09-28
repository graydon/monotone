-- -*-lua-*-

mtn_setup()
revs = {}

writefile("foo1", "z\na\nb\nx\n")
writefile("bar1" ,"0\n11\n2\n")
writefile("bar2", "0\n11\n22\n")

addfile("foo0", "a\nb\nc\n")
addfile("unchanged", "static\nfile\ncontents\n")
commit()
revs[0] = base_revision()

addfile("bar0", "0\n1\n2\n")
copy("foo1", "foo0")
commit()
revs[1] = base_revision()

copy("bar1", "bar0")
commit()
revs[2] = base_revision()

copy("bar2", "bar0")
commit()
revs[3] = base_revision()

-- annotate on a non-existent file gives an error
check(mtn("annotate", "nonexistent"), 1, false, false)
-- annotate on dirs gives an error
check(mtn("annotate", "."), 1, false, false)

--
-- annotate foo0 should now be
-- REV1: z
-- REV0: a
-- REV0: b
-- REV1: x

check(mtn("annotate", "--revs-only", "foo0"), 0, true, false)
check(greplines("stdout", {revs[1], revs[0], revs[0], revs[1]}))


--
-- unchanged should have all (3) lines from REV0 
--
check(mtn("annotate", "--revs-only", "unchanged"), 0, true, false)
check(greplines("stdout", {revs[0], revs[0], revs[0]}))

--
-- annotate bar0 should now be
-- REV1: 0
-- REV2: 11
-- REV3: 22

check(mtn("annotate", "--revs-only", "bar0"), 0, true, false)
check(greplines("stdout", {revs[1], revs[2], revs[3]}))


--
-- OK, now try some renames
--
check(mtn("rename", "--bookkeep-only", "foo0", "tmp"), 0, false, false)
check(mtn("rename", "--bookkeep-only", "bar0", "foo0"), 0, false, false)
check(mtn("rename", "--bookkeep-only", "tmp", "bar0"), 0, false, false)
rename("foo0", "tmp")
rename("bar0", "foo0")
rename("tmp", "bar0")
commit()
revs[4] = base_revision()

--
-- Now the previous annotate results should be reversed
--
check(mtn("annotate", "--revs-only", "bar0"), 0, true, false)
check(greplines("stdout", {revs[1], revs[0], revs[0], revs[1]}))

check(mtn("annotate", "--revs-only", "foo0"), 0, true, false)
check(greplines("stdout", {revs[1], revs[2], revs[3]}))

--
-- Try add of file on one side of branch
--

-- Making left side of fork, we won't add the file here
writefile("foo0", "foo\nnow has other data\n")
commit()
revs[5] = base_revision()

writefile("foo0", "foo\nstill has other data\n")
commit()
revs[6] = base_revision()

-- Now make right side
revert_to(revs[4])
writefile("bar0", "bar\non right side of fork\n")
commit()
revs[7] = base_revision()
addfile("forkfile", "a file\non the\nright fork\n")
commit()
revs[8] = base_revision()
writefile("forkfile", "a file\nchanged on the\nright fork\n")
commit()
revs[9] = base_revision()

check(mtn("merge"), 0, false, false)
--revs[10] = base_revision() -- how does commit create this?

-- 
-- ok, so annotate forkfile should be
-- REV8: a file
-- REV9: changed on the
-- REV8: right fork

check(mtn("annotate", "--revs-only", "forkfile"), 0, true, false)
check(greplines("stdout", {revs[8], revs[9], revs[8]}))
