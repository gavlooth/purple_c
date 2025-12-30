# Agent Notes

- **No stop-the-world mechanisms**: do not introduce global pauses, whole-heap scans, or world-stop GC. Keep all memory work local, incremental, or bounded.
- **ASAP first**: the baseline memory engine is ASAP (compile-time free insertion). Any optimizations must layer on top of ASAP without weakening its safety.
