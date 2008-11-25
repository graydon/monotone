-- show that we called the merge3 hook

function merge3 (anc_path, left_path, right_path, merged_path, ancestor, left, right)
   io.write("running merge3 hook\n")
   return "interactive_file merged"
end

-- end of file
