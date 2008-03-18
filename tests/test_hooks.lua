
-- this is the "testing" set of lua hooks for monotone
-- it's intended to support self-tests, not for use in
-- production. just defines some of the std hooks.

function get_passphrase(keyid)
	return keyid
end

-- Everything alice signs is trusted, nothing mallory signs is
-- trusted.  For certs signed by other people, everything is
-- trusted except for one particular cert...
-- For use of t_trusted.at.
function get_revision_cert_trust(signers, id, name, val)
   for k, v in pairs(signers) do
      if v == "alice@trusted.com" then return true end
      if v == "mallory@evil.com" then return false end
   end
   if (id == "0000000000000000000000000000000000000000"
       and name == "bad-cert" and val == "bad-val")
   then return false end
   return true             
end

function get_manifest_cert_trust(signers, id, name, val)
   return true
end

function get_file_cert_trust(signers, id, name, val)
   return true
end

function accept_testresult_change(old_results, new_results)
   for test,res in pairs(old_results)
   do
      if res == true and new_results[test] ~= true
      then
	 return false
      end
   end
   return true
end

function persist_phrase_ok()
	return true
end

function get_author(branchname)
	return "tester@test.net"
end

function ignore_file(name)
	if (string.find(name, "ts-std", 1, true)) then return true end
	if (string.find(name, "testsuite.log")) then return true end
	if (string.find(name, "test_hooks.lua")) then return true end
	if (string.find(name, "keys")) then return true end
	return false
end

function merge2(left_path, right_path, merged_path, left, right)
	io.write("running merge2 hook\n") 
	return left
     end

merge3 = nil
edit_comment = nil

if (attr_functions == nil) then
  attr_functions = {}
end
attr_functions["test:test_attr"] =
  function(filename, value)
    io.write(string.format("test:test_attr:%s:%s\n", filename, value))
  end

function get_mtn_command(host)
   return os.getenv("mtn")
end

function get_encloser_pattern(name)
   return "^[[:alnum:]$_]"
end

do
   local std_get_netsync_connect_command
      = get_netsync_connect_command
   function get_netsync_connect_command(uri, args)
      local argv = std_get_netsync_connect_command(uri, args)

      if argv and argv[1] then
	 table.insert(argv, "--confdir="..get_confdir())
	 table.insert(argv, "--rcfile="..get_confdir().."/test_hooks.lua")
      end

      return argv;
   end
end