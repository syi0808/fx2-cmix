# Vocabulary forced-bit experiment

Measures ideal-bit headroom from the global Predictor-stream vocabulary bitmap already stored in the fx2 archive header. At byte-prefix states where only one next bit can lead to any vocabulary byte, the candidate probability is treated as effectively deterministic while the production Predictor is still called and updated normally.
