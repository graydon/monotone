function test_one(base)
   local src = base .. ".src"
   local dst = base .. ".dst"
   local ud  = base .. ".ud"
   local cd  = base .. ".cd"
   if not get(src) or not get(dst) or not get(ud) or not get(cd) then
      error("case '" .. base .. "': missing file", 2)
      return
   end
   check(mtn("fload"), 0, nil, nil, {src})
   check(mtn("fload"), 0, nil, nil, {dst})
   src = sha1(src)
   dst = sha1(dst)

   check(mtn("fdiff", base, base, src, dst), 0, {ud}, nil, nil)
   check(mtn("fdiff", "--context", base, base, src, dst), 0, {cd}, nil, nil)
end

mtn_setup()

test_one("hello")
test_one("A")
test_one("B")
test_one("C")
test_one("D")
test_one("E")
test_one("F")
test_one("G")
test_one("H")
test_one("I")
test_one("J")
