function test_one(base)
   local src = base .. ".src"
   local dst = base .. ".dst"
   local ud  = base .. ".ud"
   local cd  = base .. ".cd"
   local udp = base .. ".udp"
   local cdp = base .. ".cdp"
   if not get(src) or not get(dst) then
      error("case '" .. base .. "': mandatory component missing", 2)
      return
   end
   check(mtn("fload"), 0, nil, nil, {src})
   check(mtn("fload"), 0, nil, nil, {dst})
   src = sha1(src)
   dst = sha1(dst)

   if get(ud) then
      check(mtn("fdiff", base, base, src, dst), 0, {ud}, nil, nil)
   end
   if get(cd) then
      check(mtn("fdiff", "-c", base, base, src, dst), 0, {cd}, nil, nil)
   end
   if get(udp) then
      check(mtn("fdiff", "-p", base, base, src, dst), 0, {udp}, nil, nil)
   end
   if get(cdp) then
      check(mtn("fdiff", "-c", "-p", base, base, src, dst), 0, {cdp}, nil, nil)
   end
end

mtn_setup()

test_one("hello")
