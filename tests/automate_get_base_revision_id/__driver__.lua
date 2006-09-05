-- note: do not compare to base_revision() here, because nothing says that
-- that can't be implemented using automate.
function extract_base_revision()
  local workrev = readfile("_MTN/revision")
  local extract = string.gsub(workrev, "^.*old_revision %[(%x*)%].*$", "%1")
  if extract == workrev then
    err("failed to extract base revision from _MTN/revision")
  end
  return extract
end

mtn_setup()

-- check an empty base revision id

check(mtn("automate", "get_base_revision_id"), 0, true, false)
check(trim(readfile("stdout")) == extract_base_revision())

addfile("foo", "this is file foo")

-- check a non-empty base reivision id

commit()

check(mtn("automate", "get_base_revision_id"), 0, true, false)
check(trim(readfile("stdout")) == extract_base_revision())

-- check that pending changes don't affect the base revision id

addfile("foo", "this is foo")

check(mtn("automate", "get_base_revision_id"), 0, true, false)
check(trim(readfile("stdout")) == extract_base_revision())
