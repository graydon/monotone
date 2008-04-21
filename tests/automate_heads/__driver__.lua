
mtn_setup()
revs = {}

addfile("foo", "blah")
commit()
revs.base = base_revision()

for i = 1,4 do
  revert_to(revs.base)
  addfile(tostring(i), tostring(i))
  commit()
  revs[i] = base_revision()
end
table.sort(revs)
for _,x in ipairs(revs) do append("wanted_heads", x.."\n") end
canonicalize("wanted_heads")


check(mtn("automate", "heads", "testbranch"), 0, true, false)
canonicalize("stdout")
check(samefile("wanted_heads", "stdout"))

check(mtn("automate", "heads", "nosuchbranch"), 0, true, false)
writefile("empty")
check(samefile("empty", "stdout"))

-- In mtn 0.40 and earlier, this was broken, because automate stdio
-- did not re-read the workspace options for each command, so the
-- branch was null.
check(mtn("automate", "stdio"), 0, true, false, "l5:headse")
canonicalize("stdout")
check(("0:0:l:164:" .. readfile("wanted_heads")) == readfile("stdout"))

-- end of file
