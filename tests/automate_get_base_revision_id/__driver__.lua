
mtn_setup()

-- note: do not compare to base_revision() here, because nothing says that
-- that can't be implemented using this

-- check an empty base revision id

check(mtn("automate", "get_base_revision_id"), 0, true, false)
check(trim(readfile("stdout")) == trim(readfile("_MTN/revision")))

addfile("foo", "this is file foo")

-- check a non-empty base reivision id

commit()

check(mtn("automate", "get_base_revision_id"), 0, true, false)
check(trim(readfile("stdout")) == trim(readfile("_MTN/revision")))

-- check that pending changes don't affect the base revision id

addfile("foo", "this is foo")

check(mtn("automate", "get_base_revision_id"), 0, true, false)
check(trim(readfile("stdout")) == trim(readfile("_MTN/revision")))
