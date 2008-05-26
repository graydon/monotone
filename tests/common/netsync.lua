-- Conveniently, all network tests load this file, so this skip_if
-- suffices to skip all network tests if the network is unavailable
-- (see lua-testsuite.lua and [platform]/tester-check-net.c).
skip_if(no_network_tests)

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
  math.randomseed(get_pid())
end

function netsync.setup_with_notes()
  netsync.setup()
  check(getstd("common/netsync-hooks_with_notes.lua", "netsync.lua"))
end

function netsync.internal.client(srv, oper, pat, n, res)
  if n == nil then n = 2 end
  if n == 1 then
  args = {"--rcfile=netsync.lua", "--keydir=keys",
          "--db=test.db", oper, srv.address}
  else
  args = {"--rcfile=netsync.lua", "--keydir=keys"..n,
          "--db=test"..n..".db", oper, srv.address}
  end
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

function netsync.start(opts, n, min)
  if type(opts) == "number" then
    min = n
    n = opts
    opts = nil
  end
  local args = {}
  local fn = mtn
  local addr = "localhost:" .. math.random(1024, 65535)
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
  if type(opts) == "table" then
    for k, v in pairs(opts) do
      table.insert(args, v)
    end
  elseif type(opts) ~= "nil" then
    err("netsync.start wants a table, not a "..type(opts).." as a first argument")
  end
  local argv = fn(unpack(args))
  local out = bg(argv, false, false, false)
  out.address = addr
  out.argv = argv
  local mt = getmetatable(out)
  mt.client = netsync.internal.client
  mt.pull = netsync.internal.pull
  mt.push = netsync.internal.push
  mt.sync = netsync.internal.sync
  mt.restart = function(obj)
		  local newobj = bg(obj.argv, false, false, false)
		  for x,y in pairs(newobj) do
		     obj[x] = y
		  end
		  -- wait for "beginning service..."
		  while fsize(obj.prefix .. "stderr") == 0 do
		     sleep(1)
		     check(out:check())
		  end
	       end
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

function netsync.internal.run(oper, pat, opts)
  local srv = netsync.start(opts)
  if type(opts) == "table" then
    if type(pat) ~= "table" then
       err("first argument to netsync."..oper.." should be a table when second argument is present")
    end
    for k, v in pairs(opts) do
      table.insert(pat, v)
    end
  elseif type(opts) ~= "nil" then
    err("second argument to netsync."..oper.." should be a table")
  end
  srv:client(oper, pat)
  srv:finish()
end

function netsync.pull(pat, opts) netsync.internal.run("pull", pat, opts) end
function netsync.push(pat, opts) netsync.internal.run("push", pat, opts) end
function netsync.sync(pat, opts) netsync.internal.run("sync", pat, opts) end
