#!/bin/sh

for file in $(ls *cc *hh unix/*cc unix/*hh win32/*cc win32/*hh 2>/dev/null | sort); do
    if ! grep -q 'Local Variables' $file; then
	echo "Adding vars block to $file"
	cat <<EOF >> $file


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

EOF
    fi;
done
