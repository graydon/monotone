function note_commit(new_id, certs)
	local pid
	local ret = -1
	pid = spawn("xcopy", "testfile", "testfile.copied\\")
	if (pid == -1) then
		return nil
	end
	ret, pid = wait(pid)
end
