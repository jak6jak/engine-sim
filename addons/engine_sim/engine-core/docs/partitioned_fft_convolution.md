# Partitioned FFT Convolution Plan (engine-sim)

This document captures the intended plan for upgrading the current realtime reverb from naive FIR (`ConvolutionFilter::f`) to a partitioned FFT convolver.

## Context

- Current implementation: per-sample time-domain FIR convolution in `ConvolutionFilter::f(float)`.
- Cost: $O(N)$ multiplies per output sample, where $N$ is the impulse response length.
- Profiling (macOS Instruments): `ConvolutionFilter::f` dominates the audio rendering thread.

## Low-risk step (done first)

1. Apply convolution once after mixing channels (master-bus convolution) instead of per input channel.
2. Cap impulse response length to a reasonable maximum for realtime audio.

## Target design: uniform partitioned FFT convolver

### Parameters

- Block size: $B$ samples (typical: 256 / 512 / 1024)
- FFT size: $M = 2B$ (power of two)
- Partitions: $P = \lceil N / B \rceil$

### Precomputation

- Split impulse response into partitions of length $B$: $h_0, h_1, ..., h_{P-1}$
- Precompute frequency-domain partitions: $H_p = FFT([h_p, 0...0])$ for each partition $p$

### Runtime processing

For each input block $x$ of length $B$:

1. Compute $X = FFT([x, 0...0])$
2. For each partition $p$:
   - Multiply: $Y_p = X \cdot H_p$
   - Inverse FFT: $y_p = IFFT(Y_p)$
   - Overlap-add $y_p$ into an output accumulator delayed by $p \cdot B$ samples
3. Output the next $B$ samples from the accumulator.

### Data structures

- Input staging buffer to collect samples until $B$ are available.
- Output accumulator ring buffer (or circular array) large enough to hold delayed contributions and overlap: roughly $(P+2) \cdot B + M$ samples.

## Integration sketch

- Add a block-based convolver type (e.g. `PartitionedConvolver`) to the audio path.
- In `Synthesizer::renderAudio()` (chunked render), compute `vin[]` for the chunk, then convolve `vin[]` to `vout[]` using the block convolver.
- Mix dry/wet with `AudioParameters.convolution`.

## Implementation notes / constraints

- No dynamic allocation in the realtime audio loop.
- Prefer a portable FFT backend:
  - Apple: Accelerate/vDSP
  - Elsewhere: small FFT library (kissfft/pffft) behind an abstraction.
- Ensure deterministic latency; choose $B$ accordingly.

## Validation

- A/B verify sound output vs. naive FIR for short IRs.
- Instruments: verify `ConvolutionFilter::f` no longer dominates; new hotspots should be FFT work and/or simulator step.
- Add or extend unit tests (e.g. impulse response identity and known convolution cases).
