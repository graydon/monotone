-- This test ensures that 'mtn workspace_migrate' can take any old-format
-- workspace and move it forward to the current time; it is basically a
-- compatibility test (see also the 'schema_migration' test).
--
-- This means that every time the workspace format is changed, you need to add
-- a new piece to this test, for the new format.  The way you do this is to
-- run this test with the -d option, like so:
--   $ ./testsuite.lua -d workspace_migration
-- this will cause the test to leave behind its temporary files.
-- Copy all the directories named 
--   tester_dir/workspace_migration/<thing>-current/
-- to this directory, rename them <thing>-<old format version>, and update
-- the 'current_workspace_format' variable at the top of this file.

-- If the workspace format change added possibilities to what the
-- workspace can represent, you may need to write a new workspace set,
-- which goes in its own little lua file.  Pick a <thing>, name the
-- file <thing>.lua, and add <thing> to the workspace_sets array below.
-- The file should return a table with three entries.  
--
-- 'creator' is a function of no arguments that creates a live
-- workspace named '<thing>-current' which makes use of the new
-- feature.  If it needs to make commits, it should do so only on
-- branches whose name begins with <thing>.
--
-- 'min_format' is the minimum workspace format version that can
-- represent the workspace you construct in 'creator'.
--
-- 'checker' is a function that takes two workspace directories as an
-- argument, verifies that the first one still has any interesting
-- properties given it by 'creator', and that (to the maximum extent
-- possible) those properties are consistent with the same properties
-- held by the second directory.
--
-- It should not be necessary to modify the creator function after it
-- is written; the checker function may well need updating for future
-- workspace structures.  Note that the code in this file does a
-- certain amount of common work setting up and checking the
-- workspaces; do not duplicate that work in creators or checkers.

local current_workspace_format = 2

local workspace_sets = { 
   "basic",
   "inodeprints",
   "twoparent"
}

function check_workspace_matches_current(dir, refdir)
   check(samefile("nonsense-options", dir.."/_MTN/options"))
   check(indir(refdir, mtn("automate", "get_current_revision")), 0, true, false)
   rename("stdout", "current-rev")
   check(indir(dir, mtn("automate", "get_current_revision")), 0, true, false)
   check(samefile("stdout", "current-rev"))
   -- and the log file
   check(samefile(dir .. "/_MTN/log", refdir .. "/_MTN/log"))
   -- we'd like to check that the hook file is being read, but we can't,
   -- because we can't tell monotone to read _MTN/monotonerc without also
   -- telling it to read ~/.monotone/monotonerc, and that would be bad in a
   -- test environment.  So we content ourselves with just checking the file
   -- came through and is in the right place.
   --check(indir(dir, mtn("status")), 0, true, false)
   --check(qgrep("bandersnatch", "stderr"))
   check(samefile(dir .. "/_MTN/monotonerc", refdir .. "/_MTN/monotonerc"))
end

function check_migrate_from(thing, version, checker)
   L(locheader(),
     "checking migration of ", thing, "workspace from format version ", 
     version, "\n")
   local ws = thing .. "-" .. version
   get(ws, ws)
   if (exists(ws .. "/_MTN/format")) then
      check(readfile(ws .. "/_MTN/format") == (version .. "\n"))
   else
      check(1 == version)
   end
   if current_workspace_format ~= version then
      -- monotone notices and refuses to work
      check(indir(ws, mtn("status")), 1, false, true)
      -- and the error message mentions the command they should run
      check(qgrep("migrate_workspace", "stderr"))
   end
   -- use raw_mtn here so it's not getting any help from the command line
   check(indir(ws, raw_mtn("migrate_workspace")), 0, false, false)
   -- now we should be the current version, and things should work
   check(readfile(ws .. "/_MTN/format") == (current_workspace_format .. "\n"))
   check_workspace_matches_current(ws, thing .. "-current")
   checker(ws, thing .. "-current")
   check(indir(ws, mtn("status")), 0, false, false)
end

mtn_setup()

-- we set all the options, by hand, to complete nonsense, because
-- (a) the migration operation is not supposed to need any information
-- from this file, and (b) monotone should not clobber the options
-- file, even if the corresponding command line options are given, when
-- it doesn't understand the bookkeeping format.  we save the nonsense
-- separately from current/_MTN/options to ensure that that, too, isn't
-- getting clobbered.

writefile("nonsense-options",
          'database "/twas/brillig/and/the/slithy/toves.mtn"\n'..
	  '  branch "did.gyre.and.gimble.in.the.wabe"\n'..
	  '     key "all.mimsy.were@the.borogoves"\n'..
	  '  keydir "/and/the/mome/raths/outgrabe"\n')

for _, thing in pairs(workspace_sets) do
   -- tester.lua is not very helpful here
   local tbl = dofile(testdir .. "/" .. test.name .. "/" .. thing .. ".lua")

   tbl.creator()
   writefile(thing.."-current/_MTN/log", "oh frabjous patch, calloo callay\n")
   writefile(thing.."-current/_MTN/monotonerc",
	     '-- io.stderr:write("warning: bandersnatch '..
	     'insufficiently frumious\\n")\n')
   copy("nonsense-options", thing.."-current/_MTN/options")

   for i = tbl.min_format, current_workspace_format do
      check_migrate_from(thing, i, tbl.checker)
   end
end
