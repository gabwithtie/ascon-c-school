# Pilot only

These are preliminary measurements from the first calibration method, not the
dataset used for the paper. An interrupted or unusually slow calibration batch
could cause the harness to select too few repetitions: 3,537 of 24,480
recorded batches lasted less than 2 ms, with a minimum of 0.0285 ms. The
corrected harness calibrates using the shortest of three trial batches.

The independent-test XML in this directory records the separate correctness
check; that check is not affected by the timing calibration issue.
