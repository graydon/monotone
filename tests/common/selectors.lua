function selmap(sel, revs, sort)
  check(raw_mtn("automate", "select", sel), 0, true, false)
  if sort ~= false then table.sort(revs) end
  check(samelines("stdout", revs))
end

function selmap_xfail(sel, revs, sort)
  check(raw_mtn("automate", "select", sel), 0, true, false)
  if sort ~= false then table.sort(revs) end
  xfail(samelines("stdout", revs))
end
