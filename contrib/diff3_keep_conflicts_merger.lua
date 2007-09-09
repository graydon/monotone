-- Merger that keeps the conflicts in the file, to be fixed later.
-- This is convenient when there are a lot of files with conflicts,
-- and you don't have time to fix them all at once.

-- To use;
-- 1) copy this to your monotonerc file
-- 2) set the environment variable MTN_MERGE to diff3_keep_conflicts

mergers.diff3_keep_conflicts = {
   cmd = function (tbl)
       local cmd =
          "diff3 --merge " ..
          -- Converting backslashes is necessary on Win32, since sh
          -- string syntax says '\' is an escape..
          string.format("--label \"%s [left]\" ",     tbl.left_path) ..
          string.format("--label \"%s [ancestor]\" ", tbl.anc_path) ..
          string.format("--label \"%s [right]\" ",    tbl.right_path) ..
          " " .. string.gsub (tbl.lfile, "\\", "/") ..
          " " .. string.gsub (tbl.afile, "\\", "/") ..
          " " .. string.gsub (tbl.rfile, "\\", "/") ..
          " > " .. string.gsub (tbl.outfile, "\\", "/")
       local ret = execute("sh", "-c", cmd)
      return tbl.outfile
   end,
   available =
      function ()
          return program_exists_in_path("diff3") and
                 program_exists_in_path("sh");
      end,
   wanted =
      function ()
           return true
      end
}
