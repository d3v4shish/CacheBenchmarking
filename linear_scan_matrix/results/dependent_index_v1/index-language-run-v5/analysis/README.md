# Corrected local derivation

`summary_index_language.csv` is regenerated locally from the immutable
`../raw/samples.csv` by
`../../../../scripts/analyze_index_language_results.sh`.

The DUT-produced `../raw/summary_index_language.csv` is retained unchanged as
part of its signed result artifact, but it has a derived-data defect: its
`median_ref_cycles_per_operation` column is zero because the original analyser
printed an unset variable.  The raw rows contain nonzero exact reference-cycle
counts and are unchanged.  The corrected analyser uses the sorted median of
field 12 divided by field 6, just as it does for core cycles and instructions.

`derivation_sha256.txt` binds this corrected summary to the raw input used to
produce it.  Publication must use this `analysis/` summary, never the known
bad derived column in `raw/`.
