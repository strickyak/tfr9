# /rc/init.tcl is always sourced first

# Per-board settings (e.g. different Config defaults) go in /rc/local.tcl,
# which is not part of basefs.zip, so re-uploading basefs keeps it.
catch {source /rc/local.tcl}
