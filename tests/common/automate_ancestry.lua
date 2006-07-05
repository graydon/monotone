
function make_graph()
  local revs = {}
  addfile("testfile", "A")
  commit()
  revs.a = base_revision()

  writefile("testfile", "B")
  commit()
  revs.b = base_revision()

  revert_to(revs.a)

  writefile("testfile", "C")
  commit()
  revs.c = base_revision()

  writefile("testfile", "D")
  commit()
  revs.d = base_revision()

  revert_to(revs.c)

  addfile("otherfile", "E")
  commit()
  revs.e = base_revision()

  check(mtn("explicit_merge", revs.d, revs.e, "testbranch"), 0, false, false)
  check(mtn("update"), 0, false, false)
  revs.f = base_revision()

  check(revs.f ~= revs.d and revs.f ~= revs.e)

  return revs
end

function revmap(name, from, to, dosort)
  if dosort == nil then dosort = true end
  check(mtn("automate", name, unpack(from)), 0, true, false)
  if dosort then table.sort(to) end
  check(samelines("stdout", to))
end
