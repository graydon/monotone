skip_if(not existsonpath("cvs"))

function cvs(...)
  local what
  if os.getenv("OSTYPE") == "msys" then
    what = {"cvs", "-d", cvsroot_nix, unpack(arg)}
  else
    what = {"cvs", "-d", cvsroot, unpack(arg)}
  end
  local function do_cvs()
    local ok, res = pcall(execute, unpack(what))
    if not ok then err(res) end
    sleep(1)
    return res
  end
  local ll = cmd_as_str(what)
  return {do_cvs, local_redirect = false, logline = ll}
end

function cvs_setup()
  cvsroot = test.root .. "/cvs-repository"
  -- if we're mingw/msys, we need to replace the Windows drive
  -- <letter>: with /<letter> , or CVS will think it's a remote
  -- repository
  if os.getenv("OSTYPE") == "msys" then
    cvsroot_nix = string.gsub(cvsroot, "^(%a):", "/%1")
  end
  check(cvs("-q", "init"), 0, false, false)
  check(exists(cvsroot))
  check(exists(cvsroot .. "/CVSROOT"))
  check(exists(cvsroot .. "/CVSROOT/modules"))
end
