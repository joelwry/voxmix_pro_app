# Performance

All numbers measured in the build container (Intel Xeon @ 2.8 GHz, one core, `-O3`), not on a
phone. A mid-range phone's big core is roughly comparable to slower; budget accordingly and
re-measure on hardware (see docs/TESTING.md).

## Whole session

| Scenario | Result |
|---|---|
| 60 s, 10 layers × (Pitch Correction + Vocal Sweetener + Reverb + Delay) + mastering, offline export | 41.6 s → 1.4× real time |
| Same session through the real-time callback path (192-frame blocks) | 71 % of one core |

That is a deliberate worst case (four heavy effects on every one of 10 layers, a separate
reverb per layer). Practical guidance until phone measurements exist:

* Put shared reverb/delay on a **bus** instead of on every layer (one reverb instead of ten).
* Pitch correction and the Vocal Sweetener belong on lead layers; doubles and ad-libs usually
  need only EQ + compression.
* Silent layers whose effect tails have finished skip effect processing entirely.
* Mono vocal layers are processed once (dual-mono fast path in the Vocal Sweetener, EQ, de-esser and saturation).

## Per effect (stereo pair from a mono layer, 48 kHz, 192-frame blocks, % of one core)

| Effect | CPU |
|---|---|
| ms_formant_shift | 8.49% |
| ms_choir_mist | 5.85% |
| vocal_sweetener | 3.17% |
| ms_angel_dust | 2.62% |
| ms_broken_radio | 2.57% |
| ms_drowning | 2.54% |
| ms_talkbox_motion | 2.45% |
| ms_deep_sea | 2.36% |
| ms_cloud_nine | 2.32% |
| ms_reverse_dream | 2.25% |
| ms_echo_chamber | 2.13% |
| ms_reverse_suck_in | 2.08% |
| ms_dreamscape | 1.92% |
| ms_endless_hall | 1.89% |
| ms_dark_tunnel | 1.88% |
| ms_vinyl_soul | 1.74% |
| pitch_correction | 1.62% |
| ms_vortex | 1.57% |
| reverb | 1.54% |
| ms_chaos_mode | 1.44% |
| ms_higher_self | 1.40% |
| ms_tape_melt | 1.33% |
| ms_frozen_echo | 1.29% |
| ms_ghost_double | 1.18% |
| ms_retro_chorus | 1.13% |
| ms_delay_spiral | 1.11% |
| mastering | 1.04% |
| ms_octave_shadow | 1.02% |
| ms_rhythm_pulse | 1.00% |
| ms_underwater | 0.94% |
| ms_liquid_phaser | 0.94% |
| ms_auto_wah | 0.91% |
| ms_pitch_dive | 0.82% |
| ms_wah_vocal | 0.82% |
| ms_slapback | 0.81% |
| ms_megaphone | 0.79% |
| ms_telephone_ghost | 0.76% |
| ms_alien_transmission | 0.76% |
| ms_dusty_sample | 0.71% |
| ms_robot_voice | 0.56% |
| ms_ping_pong_galaxy | 0.47% |
| ms_digital_shatter | 0.42% |
| saturation | 0.41% |
| limiter | 0.39% |
| chorus | 0.38% |
| compressor | 0.32% |
| eq | 0.24% |
| deesser | 0.24% |
| delay | 0.16% |
| ms_glitch_machine | 0.15% |
| ms_stutter_burst | 0.15% |
| gate | 0.07% |
| stereo_width | 0.04% |

The remaining effects (EQ, compressor, de-esser, gate, saturation, limiter, delay, chorus,
width and most MoodShift FX) are below 1 %.

## Planned optimisations

1. Measure on 3–4 phones (low/mid/high end) and set a per-device layer/effect budget.
2. A "freeze layer" option (render a layer with its effects to audio).
3. NEON-friendly inner loops for the reverb and the YIN difference function.
4. Disk streaming for very long sessions (currently audio is held in RAM as 16-bit:
   ~17 MB per 3-minute mono take, ~35 MB per 3-minute stereo beat).
