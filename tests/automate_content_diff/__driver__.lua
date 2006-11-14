
mtn_setup()

-- check output if there are no changes
check(mtn("automate", "content_diff"), 0, true, true)
check(fsize("stdout") == 0 and fsize("stderr") == 0)

-- check non-existing path
check(mtn("automate", "content_diff", "non_existing"), 1, true, true)


-- check existing path against current workspace
addfile("existing", "foo bar")
-- do not restrict here, since '' (the root) has not yet been committed
check(mtn("automate", "content_diff"), 0, true, true)
check(fsize("stdout") ~= 0)

-- add three more revisions and test for correct revid handling
commit()
R1=base_revision()
writefile("existing", "foo foo")
commit()
R2=base_revision()
writefile("existing", "foo foo bar")
commit()
R3=base_revision()

-- one and two revisions should work
check(mtn("automate", "content_diff", "-r", R1), 0, true, true)
check(fsize("stdout") ~= 0)
check(mtn("automate", "content_diff", "-r", R1, "-r", R2), 0, true, true)
check(fsize("stdout") ~= 0)

-- three and more should not
check(mtn("automate", "content_diff", "-r", R1, "-r", R2, "-r", R3), 1, true, true)

