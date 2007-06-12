mtn_setup()

-- a number of broken input strings
check(mtn("automate", "stdio"), 1, false, false, "le")
check(mtn("automate", "stdio"), 1, false, false, "l")
check(mtn("automate", "stdio"), 1, false, false, "l5:a")
check(mtn("automate", "stdio"), 1, false, false, "l5:aaaaaaaa")
check(mtn("automate", "stdio"), 1, false, false, "xl6:leavese")
check(mtn("automate", "stdio"), 1, false, false, "o3:key0:exl6:leavese")
check(mtn("automate", "stdio"), 1, false, false, "o3:ke0:el6:leavese")

-- invalid ('leaves' doesn't take --author)
check(mtn("automate", "stdio"), 1, false, false, "o6:author3:fooe l6:leavese")

-- not broken
check(mtn("automate", "stdio"), 0, false, false, "o3:key0:el6:leavese")
check(mtn("automate", "stdio"), 0, false, false, "o3:key0:e l6:leavese")

function parse_stdio(dat, which)
  local got = {}
  while true do
    local b,e,n,s = string.find(dat, "(%d+):%d+:[lm]:(%d+):")
    if b == nil then break end
    n = n + 0
    if got[n] == nil then got[n] = "" end
    got[n] = got[n] .. string.sub(dat, e+1, e+s)
    dat = string.sub(dat, e+1+s)
  end
  if got[which] == nil then got[which] = "" end
  L("output of command ", which, ":\n")
  L(got[which])
  return got[which]
end

writefile("output", "file contents")

check(mtn("automate", "inventory"), 0, true, false)
canonicalize("stdout")
rename("stdout", "output")
check(mtn("automate", "stdio"), 0, true, false, "l9:inventorye")
check(parse_stdio(readfile("stdout"), 0) == readfile("output"))
