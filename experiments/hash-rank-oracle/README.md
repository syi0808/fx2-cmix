# Hash-rank oracle

Exact next-byte probability-rank experiment for the fx2-cmix Predictor.

The runner preprocesses `prof_input/input2` with the production dictionary path,
pretrains an identical Predictor, then uses `fork()` copy-on-write snapshots to
score every counterfactual next-byte continuation allowed by the vocabulary
bitmap already shared in the fx2 archive header. Candidate scoring follows the
same quantized bit probabilities as `Encoder::Encode`; the final candidate bit
is not committed because no later probability is needed to compute the finite
next-byte likelihood.

The experiment reports the actual byte's surprisal, exact probability rank, the
optimistic `log2(rank)` identification lower bound, and the minimum prefix of a
fixed 64-bit fingerprint for which the actual byte is the first matching
candidate in probability order.

`min_first_match_hash_bits` excludes framing/length overhead and therefore is an
oracle payload measurement, not a complete compression rate.
