
mtn_setup()

function chk()
  check(mtn("automate", "get_current_revision_id"), 0, true, false)
  rename("stdout", "current")
  check(mtn("automate", "get_current_revision"), 0, true)
  check(mtn("identify"), 0, true, nil, {"stdout"})
  check(trim(readfile("current")) == trim(readfile("stdout")))
end

-- check pending changes against an empty base

addfile("foo", "this is file foo")

chk()

commit()

-- check changes against a non-empty base

addfile("bar", "this is file bar")

chk()
