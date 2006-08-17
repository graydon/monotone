
function mtn2(...)
  return mtn("--db=test2.db", "--keydir=keys2", unpack(arg))
end

function mtn3(...)
  return mtn("--db=test3.db", "--keydir=keys3", unpack(arg))
end

netsync = {}
netsync.internal = {}

function netsync.setup()
  check(copy("test.db", "test2.db"))
  check(copy("keys", "keys2"))
  check(copy("test.db", "test3.db"))
  check(copy("keys", "keys3"))
  check(getstd("common/netsync-hooks.lua", "netsync.lua"))
  math.randomseed(os.time())
end

function netsync.setup_with_notes()
  netsync.setup()
  check(getstd("common/netsync-hooks_with_notes.lua", "netsync.lua"))
end

function netsync.internal.client(srv, oper, pat, n, res)
  if n == nil then n = 2 end
  args = {"--rcfile=netsync.lua", "--keydir=keys"..n,
          "--db=test"..n..".db", oper, srv.address}
  if type(pat) == "string" then
    table.insert(args, pat)
  elseif type(pat) == "table" then
    for k, v in pairs(pat) do
      table.insert(args, v)
    end
  elseif pat ~= nil then
    err("Bad pattern type "..type(pat))
  end
  check(mtn(unpack(args)), res, false, false)
end
function netsync.internal.pull(srv, pat, n, res) srv:client("pull", pat, n, res) end
function netsync.internal.push(srv, pat, n, res) srv:client("push", pat, n, res) end
function netsync.internal.sync(srv, pat, n, res) srv:client("sync", pat, n, res) end

function netsync.start(pat, n, min)
  if pat == "" or pat == nil then pat = "{*}" end
  local args = {}
  local fn = mtn
  local addr = "localhost:" .. math.random(20000, 50000)
  table.insert(args, "--dump=_MTN/server_dump")
  table.insert(args, "--bind="..addr)
  if min then
    fn = minhooks_mtn
  else
    table.insert(args, "--rcfile=netsync.lua")
  end
  if n ~= nil then
    table.insert(args, "--keydir=keys"..n)
    table.insert(args, "--db=test"..n..".db")
  end
  table.insert(args, "serve")
  if type(pat) == "string" then
    table.insert(args, pat)
  elseif type(pat) == "table" then
    for k, v in pairs(pat) do
      table.insert(args, v)
    end
  else
    err("Bad pattern type "..type(pat))
  end
  local out = bg(fn(unpack(args)), false, false, false)
  out.address = addr
  local mt = getmetatable(out)
  mt.client = netsync.internal.client
  mt.pull = netsync.internal.pull
  mt.push = netsync.internal.push
  mt.sync = netsync.internal.sync
  local mt_wait = mt.wait
  mt.check = function(obj) return not mt_wait(obj, 0) end
  mt.wait = nil -- using this would hang; don't allow it
  -- wait for "beginning service..."
  while fsize(out.prefix .. "stderr") == 0 do
    sleep(1)
    check(out:check())
  end
  mt.stop = mt.finish
  return out
end

function netsync.internal.run(oper, pat)
  local srv = netsync.start(pat)
  srv:client(oper, pat)
  srv:finish()
end
function netsync.pull(pat) netsync.internal.run("pull", pat) end
function netsync.push(pat) netsync.internal.run("push", pat) end
function netsync.sync(pat) netsync.internal.run("sync", pat) end
