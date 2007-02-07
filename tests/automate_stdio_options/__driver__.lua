mtn_setup()

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

addfile("file", "contents")
commit("testbranch")
writefile("file", "modified")

diffcmd = "o1:r12:h:testbranche l12:content_diffe"
check(mtn("automate", "stdio"), 0, true, false, string.rep(diffcmd, 2))
dat = readfile("stdout")
check(parse_stdio(dat, 0) == parse_stdio(dat, 1))
