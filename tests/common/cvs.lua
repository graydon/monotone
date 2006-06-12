skip_if(not existsonpath("cvs"))

function cvs(...)
  if os.getenv("OSTYPE") == "msys" then
    return {"cvs", "-d", cvsroot_nix, unpack(arg)}
  else
    return {"cvs", "-d", cvsroot, unpack(arg)}
  end
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
