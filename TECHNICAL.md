<div align="center">

# AkiTilt — Technical Notes

**技术文档**

Version 2.0.0

</div>

---

## 1　Signal Chain Overview

```
input ──► [FormantShifterEngine] ──► [PitchShifter] ──► [TiltEQ] ──┐
   │            STFT spectral-envelope shift   grains   shelves     │
   │                                                                ├─► dry/wet ─► Out ─► tanh ─► output
   └── DelayLine (FFT size) ─────────────────── [StereoWidth] ──────┘    crossfade   gain   limiter
                                              mid/side width    ▲
                                                          NoiseBed (into the wet path)
   mono sum ──► [PitchDetector] ──(Auto mode only)──► formant cents fed to the engine
```

- The engine advances **sample by sample**; the STFT frame loop is triggered by a sample counter.
- The wet (STFT) path lags the input by the current FFT size (128…4096 samples, default 2048). The processor reports it via `setLatencySamples()` and delays the dry path by the same amount through a `DelayLine`, so the dry/wet crossfade cannot comb-filter. When the FFT size changes the delay is re-pointed at the block boundary.
- All parameter smoothing (`juce::SmoothedValue`) lives inside the modules: formant/bands/dry-wet 20 ms, pitch/tilt/width 30 ms, air/tone 50 ms.
- The audio thread performs no heap allocation after initialization; every per-frame scratch buffer is a preallocated member — including one `juce::dsp::FFT` and one window table per supported FFT size.

## 2　Formant (±1200 ct) — STFT Spectral-Envelope Shift

The goal: **move the spectral envelope (formant structure) of a sound while leaving the fundamental and harmonics in place.**

STFT parameters (`FormantShifterEngine`):

| Parameter | Value | Notes |
|---|---|---|
| FFT size | 128…4096, selectable (`minFftOrder = 7 … maxFftOrder = 12`), default 2048 | 2048 points ≈ 23.4 Hz resolution @48 kHz |
| Window | Hann ×2 | analysis + synthesis (double windowing) |
| Overlap | 4× (hop = size/4) | JUCE's inverse transform already carries the 1/N scaling; the overlap-add normalises by the squared-window sum |
| Bins | size/2 + 1 (`activeBins`) | DC and Nyquist are real-valued and handled separately |

Every supported size owns a prebuilt `juce::dsp::FFT` and Hann table (fixed-size, non-copyable objects), built once in `prepare()`; all FIFOs and scratch vectors are allocated at the 4096 maximum. `setFftOrder()` therefore only re-points the active members, clears the FIFOs (the wet stream restarts from silence for a moment) and updates the published bin count — realtime-safe, no allocation. The processor applies the parameter at a block boundary and re-aligns the dry-path delay and the reported latency with it.

Processing steps (once per hop, in `processFrame`):

1. **Window & forward transform** — the input frame is copied into a 2N buffer, multiplied by Hann, and transformed with `performRealOnlyForwardTransform`.
2. **Magnitude spectrum** — `|re + im|`; DC/Nyquist read `data[0]/data[1]`; everything gets a `1e-9` floor.
3. **Peak-region envelope** (see section 3).
4. **Envelope warp** — for each bin k:
   - `ratio = 2^(cents/1200)`, with cents internally clamped to ±4800 (±48 semitones, beyond the UI range);
   - the envelope is read at `k / ratio` with linear interpolation → `shifted`;
   - gain `= clamp(shifted / original, 0.05, 20)` (≈ ±26 dB); the complex spectrum is scaled bin-wise;
   - `original` reads `interpolated[min(k, 1023)]`; a `1e-9` floor guards the division.
5. **Inverse transform & overlap-add** — iFFT → Hann again → `output[(outputIndex+i) % N] += data[i] / 6`.

**Gain structure**: the normalized Hann table has a mean of 1, so the squared-window sum across the 4 overlapping grains is `Σw² = 6`; the overlap-add divides by 6 and reconstructs the input exactly at ratio 1 — a default insert is transparent (asserted offline in `Tests/EngineTests.cpp`). **Neutral bypass**: at |shift| < 0.005 ct the per-bin gain loop is skipped entirely (the warped path would clamp envelope-gap gains to 0.05), so the frame passes through bit-exactly.

**Per-sample contract**: every channel's input is written first, then every channel's output is read, then `advance()` runs once — the frame loop fires after all channels have been written for the current sample, keeping stereo frames aligned (the processor and the offline tests both follow this order).

**Parameter smoothing**: cents and band width each smooth over 20 ms; frames read the current smoothed values, so dragging the shift morphs the timbre continuously instead of stepping.

**Auto Formant — pitch tracking** (`Source/DSP/PitchDetector.h`, processor-level): the mono input sum is decimated to ≈12 kHz (box filter) and analysed every ≈10.7 ms with a YIN-style cumulative-mean-normalised difference function over a 60 Hz–1 kHz lag range; a normalised dip below 0.15 marks a voiced frame (parabolic interpolation sharpens the period). The detected cents relative to the anchor A3 = 220 Hz are smoothed (80 ms voiced, ≈1.5 s release back to 0 ct when unvoiced) and combined with the knob as `effective = clamp(detected + knob, ±1200)` (`autoFormantCents`) — higher input pitch ⇒ smaller/brighter formants, the knob becomes a fine offset, and at 220 Hz input the shift equals the knob value. Manual mode is bit-identical to previous versions: the detector only *feeds* `setTargets()`, the STFT algorithm itself is untouched.

## 3　Bands (0–32) — Envelope Analysis Resolution

This control does not touch gain directly; it sets the analysis granularity of the peak-region envelope extractor:

- Region width `width = clamp(round(value), 2, envelopeBins / 2)` bins — defined in bins like the reference algorithm, so the Bands knob keeps its full sweep at every FFT size (at smaller sizes the same width simply covers proportionally more Hz);
- The `envelopeBins` envelope bins are split into `ceil(envelopeBins/width)` regions;
- Per region: compute the region-average magnitude, then take the **maximum that exceeds the average** as the peak (weak local peaks cannot lift noise regions);
- Anchors are fixed at bin 0 and bin `envelopeBins − 1` (deliberately not the Nyquist bin);
- Adjacent peaks are linearly interpolated into an `envelopeBins`-point envelope curve.

Low values → more regions → the envelope hugs spectral detail (harder, more artificial morphs). High values → smoother envelopes (formants move as a whole, sounding more natural). Peak and anchor arrays are preallocated at the 4096-point maximum (2050 entries).

## 4　Pitch (±12 st) — Dual-Window Grain Shifter

A time-domain stage after the STFT (`PitchShifter`); the STFT pipeline is untouched:

- **Delay ring**: a 4096-sample ring buffer (2× grain window), written per sample;
- **Two grains**: read pointers offset by half a grain window (1024), each weighted by a precomputed Hann² power-complementary envelope table (2048 entries, linearly interpolated) — the two grain envelopes always sum to 1, so there is no amplitude ripple;
- **Phase accumulator**: `phase += (ratio − 1)` with `ratio = 2^(st/12)`; the read offset is `windowSize − phase`, keeping reads strictly behind the write position (0…2048 samples);
- **Bypass**: below |0.005| semitones the input is returned untouched (bit-exact passthrough on the default path);
- 30 ms smoothing; the read offset slides continuously with the parameter — no phase resets, no clicks.

## 5　Tilt (±12 dB) — Mirrored Shelf Tilt

`TiltEQ`, a zero-latency IIR stage:

- Fixed split at **650 Hz**; two cascaded second-order shelves (Q = 0.707):
  - LowShelf gain = `−tilt` dB, HighShelf gain = `+tilt` dB (positive = brighter);
- **Loudness compensation**: an extra `−0.15 × tilt` dB trims the perceived loudness rise;
- Coefficients update once per audio block (`update(numSamples)` advances the smoother with `skip(n−1)` first), 30 ms smoothing;
- Below |0.05| dB the stage is bit-exact bypassed.

## 6　Width (0–200 %) — Mid/Side Widening

`StereoWidth`, a per-sample M/S matrix:

```
M = (L+R)/2,  S = (L−R)/2
L' = M + S·w,  R' = M − S·w      (w = Width/100; exact unity at 100 %)
```

Applied to the wet path only; skipped automatically on mono buses. 30 ms smoothing.

## 7　Air + Tone — Noise Bed

`NoiseBed`, mixed into the wet path (and therefore scaled by Dry/Wet):

- **Source**: per-channel xorshift32 white noise (`s^=s<<13; s^=s>>17; s^=s<<5`) with independent seeds, decorrelated across channels;
- **Colour**: a second-order bandpass, Q = 1.0, centre `800 × 15^tone` Hz (800 Hz…12 kHz, logarithmic), logarithmic);
- **Level**: `gain = amount² × 0.12 × track² × levelNorm` — the bed tracks the input envelope continuously (no gate/threshold): the processor's follower (instant attack per block, ~450 ms release) maps to `track = smoothstep(clamp((followerDb + 60) / 48))`, reaching full level by ≈ −12 dBFS input, so the noise reads as a layer wrapped around the sound. `levelNorm = √(2.4 kHz / f)` keeps loudness constant across the Tone sweep; coefficients update per block (50 ms smoothing).

## 8　Output Stage — Alignment, Mix, Limiting

```
out = dryDelayed × (1 − mix) + wet × mix
out × 10^(outGain/20)
if limiter: out = tanh(out)
```

- The dry path is delayed by the current FFT size to align with the wet path, so partial mixes stay comb-free; the delay re-points when the FFT size changes;
- The constant latency is reported to the host (PDC aligns other tracks);
- The limiter parameter defaults to **off**, and a neutral shift (|cents| < 0.005) skips the envelope warp entirely, so inserting the plugin without touching anything stays transparent. The measured latency-aligned null residual is **3.5e-04 (0.035 %, ≈ −69 dB)** worst-sample — the symmetric-Hann COLA ripple inherent to the algorithm's window (asserted in `Tests/EngineTests.cpp`); far below audibility, and any DAW null will additionally depend on the host's PDC alignment;
- The tanh soft limiter sits after the output gain, so **Out** doubles as drive into the limiter;
- Meters: per-block peak statistics with 0.6 dB-per-block decay in the dB domain, handed to the UI through `std::atomic<float>`;
- When the Bypass parameter is on, the (delay-aligned) dry path is output, keeping latency constant so the host never recalculates PDC.

## 9　Presets & State

- All parameters live in a single `AudioProcessorValueTreeState` (IDs in `Source/Parameters.h`); state is a plain ValueTree, compatible with DAW session save/restore;
- Factory presets (`Source/PresetManager.h`) are a compile-time table applied via `setValueNotifyingHost` per parameter; they are ordered subtle → extreme and touch the effect parameters plus a character-matched FFT size (smooth timbres 4096, vocal morphs 2048, snappy leads 512 and below) — Pitch, Out, Limiter and Auto Formant always stay at the user's values;
- **User presets** are plain state XML files in the user application-data directory (`%APPDATA%\AkiTilt\Presets` on Windows, `~/Library/Application Support/AkiTilt/Presets` on macOS), named after the preset and carrying the full parameter state plus `presetName`/`presetVersion` attributes. `scanPresets()` lists factory entries first, then user files sorted naturally; `saveUserPreset()` writes `copyState()`, `applyFile()` restores with `replaceState()`. Because the files are ordinary XML, users can rename, delete or share them from the OS file manager (the editor's folder button opens the directory);
- A/B compare lives in the editor: switching stores the current APVTS state with `copyState()` into the active slot and restores the other slot with `replaceState()`.

## 10　UI Implementation Notes

- **Spectrum display**: the **post-effect** frame magnitudes (measured after the envelope warp) *and* the **shifted envelope** are published through triple buffers plus `std::atomic` (audio thread writes, message thread reads — lock-free, no tearing), together with a frame counter, the band width and the bin count used by that frame. The editor copies a frame only when the counter advances, and the pad skips repaints entirely while the spectrum is silent (energy gate) — idle cost is near zero. Paths are decimated to ≤ 420 points, the cursor glow is a radial gradient rather than a blur shadow, and overlay readouts only repaint when a value actually changes. X axis is 20 Hz–20 kHz logarithmic, Y axis −78…+60 dB; the bin-to-frequency mapping reads the frame's own FFT size, so the spectrum stays on the grid at every resolution;
- **Panel annotations**: the overlay lists BANDS / TILT / WIDTH / AIR top-left and OUT top-right, with a spectrum/envelope legend. The warm curve is the shifted envelope — the exact curve Bands and Formant reshape together. The tilt response is drawn as a full-width curve (`tanh((u − 0.504) × 6)` around the 650 Hz split, amplitude ∝ tilt, rising to the right for positive values), and tall band-boundary ticks along the bottom edge show the analysis regions (density capped at ~64 columns). The grid follows market-EQ conventions: bold labelled columns at 100 Hz / 1 kHz / 10 kHz with faint octave columns between, and dB rows every 20 dB with the 0 dB line emphasised. The whole panel is tone-mapped: green at the default Tone (50 %), washing brown as low frequencies dominate and grass-green as highs do (diverging tint, up to 22 % alpha at the extremes);
- **XY pad**: X = formant (±1200 ct), Y = pitch (±12 st, up is higher). Dragging writes the parameters (`setValueNotifyingHost`); double-click recentres. The cursor position is read back from the parameter atomics by the timer, suppressed while dragging to avoid jitter. In Auto Formant the cursor shows the effective (detector-driven) cents and X dragging is pinned;
- **Pad locks & modifiers**: the A / X / Y pill toggles sit at the pad's bottom-right — **A** engages Auto Formant, **X** and **Y** lock an axis at its current value for the drag; holding Alt inverts the locks for that gesture. Shift gives fine adjustment (drag deltas scaled to one eighth around the drag anchor), Ctrl snaps to a musical grid (100 ct / 1 semitone). A locked axis also disables its knob below (Formant for X, Pitch for Y — mouse-proof, marked with a padlock badge that releases the lock when clicked) until the lock is released, keeping the pad and the knob row in a single shared state. While Auto is on the X lock and the Formant badge are dimmed (unavailable, not hidden) and the Formant knob keeps working as the offset;
- **FFT size selector & status**: a combo box in the right card attaches to the non-automatable `fftSize` choice parameter; a status label under it shows the live size and round-trip milliseconds, refreshed by the timer only when the text changes;
- **Mint theme**: `Source/UI/Theme.h` translates the Aki Design System §3.4 / Appendix B tokens into `juce::Colour` constants (bg #E8F0E5, contrast #22362A, accent #3D624C, ramp #8FA89A/#A3C4A9/#B5D7C3, …); `AkiLookAndFeel` implements the rotary (ramp-gradient arc + ink pointer), pill toggles, combo box and double-bezel cards. Fonts are Plus Jakarta Sans / Fraunces 72pt / IBM Plex Mono (OFL, static weights, embedded via BinaryData);
- **Scaling**: the editor lays out in a 780×540 design space mapped by a single `scaled()` helper, resizing with a locked aspect ratio (minimum 620×429).

## 11　Tests

`Tests/EngineTests.cpp` renders offline assertions (the `AkiTiltTests` target):

- Sine input; at neutral settings the wet/dry RMS ratio ≈ 1 (unity reconstruction);
- **Per FFT size (128…4096)**: the reported size/bins/latency match, the latency-aligned null at neutral stays inside a per-size COLA-ripple threshold (residuals halve per size doubling, ≈ 5.6e-3 at 128 → 1.8e-4 at 4096), and ±1200 ct stays finite;
- A mid-stream FFT-size switch keeps the stream finite and applies the new size;
- Identical stereo input produces identical output through both the formant engine and the pitch shifter (per-sample write/read/advance order);
- The published frame counter advances per STFT frame, and the envelope snapshot changes with the Bands width;
- **Pitch detector**: 110/220/440/880 Hz tones are tracked within 30 cents of their anchor offset, broadband noise is rejected as unvoiced; the Auto mapping adds the knob offset and clamps to ±1200 ct;
- ±1200 ct extremes and +7 st shifting stay finite with sane energy;
- Tilt at 0 dB is a bit-exact passthrough; +12 dB boosts highs;
- Width at 200 % doubles side energy; the air bed tracks the input and fades to silence;
- User preset files round-trip through write → scan → read.

---

<div align="center">

# AkiTilt — 技术文档

版本 2.0.0

</div>

---

## 1　信号链总览

```
input ──► [FormantShifterEngine] ──► [PitchShifter] ──► [TiltEQ] ──┐
   │              STFT 频谱包络移动          颗粒移调        镜像搁架  │
   │                                                                    ├─► dry/wet ─► Out ─► tanh ─► output
   └── DelayLine (FFT 尺寸) ────────────────────────── [StereoWidth] ──┘        交叉淡化    增益   软限制
                                                         中侧加宽      ▲
                                                            NoiseBed（空气床，混入湿路）
   单声道求和 ──► [PitchDetector] ──（仅 Auto 模式）──► 写入引擎的 formant cents
```

- 引擎按**逐采样**推进，STFT 帧循环由采样计数触发；
- 湿路（STFT）滞后量为当前 FFT 尺寸（128…4096 采样,默认 2048），处理器通过 `setLatencySamples()` 向宿主上报，并用同长 `DelayLine` 延迟干路，保证干湿交叉淡化不产生梳状滤波；FFT 尺寸变化时在块边界重新对位；
- 各模块的参数平滑（`juce::SmoothedValue`）都在模块内部完成：formant/bands/干湿 20 ms，pitch/tilt/width 30 ms，air/tone 50 ms；
- 除初始化外音频线程零堆分配，逐帧 scratch 缓冲全部为成员预分配——包括每个受支持 FFT 尺寸各一份的 `juce::dsp::FFT` 与 Hann 表。

## 2　Formant Shift（±1200 ct）——STFT 频谱包络移动

核心目标：**只移动声音的频谱包络（共振峰结构），不移动基频与泛音位置**。

STFT 参数（`FormantShifterEngine`）：

| 参数 | 值 | 说明 |
|---|---|---|
| FFT 点数 | 128…4096 可选（`minFftOrder = 7 … maxFftOrder = 12`）,默认 2048 | 2048 点频率分辨率 ≈ 23.4 Hz @48k |
| 窗 | Hann ×2 | 分析窗 + 合成窗（双重加窗） |
| 重叠 | 4×（hop = size/4） | JUCE 逆变换自带 1/N 缩放，OLA 按窗平方和归一化 |
| 频点数 | size/2 + 1（`activeBins`） | DC 与 Nyquist 为实数，单独处理 |

每个受支持的尺寸都持有一份预构建的 `juce::dsp::FFT` 与 Hann 表（定长、不可拷贝对象），在 `prepare()` 一次性建齐；全部 FIFO 与 scratch 向量按 4096 上限预分配。因此 `setFftOrder()` 只需重新对位活动成员、清空 FIFO（湿路瞬间从静音重启）并更新发布的频点数——实时安全、零分配。处理器在块边界应用该参数,并同步重对干路延迟与上报延迟。

处理流程（每个 hop 触发一次 `processFrame`）：

1. **加窗与正变换**：输入帧复制到 2N 缓冲，乘 Hann，`performRealOnlyForwardTransform`；
2. **幅度谱**：`|re+im|`，DC/Nyquist 取 `data[0]/data[1]`，全部加 `1e-9` 防零；
3. **峰值区域包络**（见下一节 Bands 控件）；
4. **包络扭曲**：对每个频点 k
   - `ratio = 2^(cents/1200)`，cents 内部限幅 ±4800（对应 ±48 半音，大于 UI 量程）；
   - 以 `k / ratio` 为源坐标线性插值读取包络 → `shifted`；
   - 增益 `gain = clamp(shifted / original, 0.05, 20)`（≈ ±26 dB），复数频谱逐点乘以 gain；
   - `original` 取 `interpolated[min(k, 1023)]`，除法前加 `1e-9` 下限；
5. **逆变换与叠加**：iFFT → 再乘一次 Hann → `output[(outputIndex+i) % N] += data[i] / 6`。

**增益结构**：归一化后的 Hann 表均值为 1，4 层重叠颗粒的窗平方和 `Σw² = 6`，OLA 除以 6，ratio = 1 时精确重构输入——默认插入即透明（`Tests/EngineTests.cpp` 有离线断言）。**中性旁路**：|shift| < 0.005 ct 时完全跳过逐频点增益循环（否则包络间隙的增益会被钳到 0.05），帧位精确通过。

**逐采样调用约定**：先写入所有声道的输入，再读取所有声道的输出，最后 `advance()` 一次——帧循环在当前采样所有声道写完之后触发，保证立体声帧对齐（处理器与离线测试均遵循该顺序）。

**参数平滑**：cents 与频带宽度（Bands）各自 20 ms 平滑，帧内取当前平滑值，拖动时包络连续变化而非跳变。

**Auto Formant——音高跟踪**（`Source/DSP/PitchDetector.h`，位于处理器层）：单声道求和经均值滤波抽取到 ≈12 kHz,每 ≈10.7 ms 以 YIN 风格的累计均值归一化差函数在 60 Hz–1 kHz 的 lag 范围内分析一次；归一化凹陷低于 0.15 判为有声（抛物线插值锐化周期估计）。检出的相对锚点 A3 = 220 Hz 的 cents 经平滑（有声 80 ms,无声约 1.5 s 回落到 0 ct）后与旋钮合成：`effective = clamp(detected + knob, ±1200)`（`autoFormantCents`）——输入音高越高 formant 越小越亮,旋钮变为微调偏移,输入 220 Hz 时偏移恰为旋钮值。Manual 模式与此前版本位等价：检测器只是**喂** `setTargets()`,STFT 算法本身一行未动。

## 3　Bands（0–32）——包络分析分辨率

该控件不直接作用于声音增益，而是决定**峰值区域包络提取的分析粒度**：

- 区域宽 `width = clamp(round(value), 2, envelopeBins / 2)` 个频点——与参考算法一致按 bin 数定义，任何 FFT 尺寸下 Bands 旋钮都保持全量程（更小尺寸下同样宽度天然覆盖更宽的 Hz）；
- 把 `envelopeBins` 个包络频点切成 `ceil(envelopeBins/width)` 个区域；
- 每个区域：先求区域平均幅度，再取**大于平均值的最大者**作为峰（防止弱峰抬高噪声区）；
- 锚点固定为 bin 0 与 bin `envelopeBins − 1`（注意不是 Nyquist 频点）；
- 相邻峰之间线性插值，得到 `envelopeBins` 点的包络曲线 `interpolated`。

数值越小 → 区域越多 → 包络贴合频谱细节（移动后音色更"扁"）；数值越大 → 包络越平滑（共振峰整体搬移，质感更自然）。区域峰与锚点数组均按 4096 点上限预分配（2050 项）。

## 4　Pitch（±12 st）——双窗颗粒移调

位于 STFT 之后的时域处理（`PitchShifter`），不改动 STFT 管线：

- **延迟环**：4096 采样环形缓冲（2× 粒窗），逐采样写入；
- **双颗粒**：两个读取点相位相差半个粒窗（1024），每个以预计算的 Hann² 功率互补窗查表加权（2048 项、线性插值）——两颗粒包络之和恒为 1，无振幅纹波；
- **相位累加**：`phase += (ratio − 1)`，`ratio = 2^(st/12)`；读偏移 = `windowSize − phase`，保证读取点永远在写入点之后（0…2048 采样）；
- **旁路**：|semitones| < 0.005 时直接返回输入（默认路径位精确直通）；
- 平滑 30 ms；内部读偏移随参数连续滑动，无相位重置爆音。

## 5　Tilt（±12 dB）——镜像搁架倾斜

`TiltEQ`，零延迟 IIR 实现：

- 固定分频点 **650 Hz**，两只二阶搁架（Q = 0.707）串联：
  - LowShelf 增益 = `−tilt` dB，HighShelf 增益 = `+tilt` dB（正值 = 更亮）；
- **响度补偿**：额外增益 `−0.15 × tilt` dB，抵消提亮带来的响度感；
- 系数按音频块更新（`update(numSamples)` 内先 `skip(n−1)` 推进平滑，再读值），参数平滑 30 ms；
- |tilt| < 0.05 dB 时位精确旁路。

## 6　Width（0–200 %）——中侧加宽

`StereoWidth`，逐采样 M/S 矩阵：

```
M = (L+R)/2,  S = (L−R)/2
L' = M + S·w,  R' = M − S·w      （w = Width/100，100 % 时恒等）
```

仅作用于湿路；单声道总线自动跳过。参数平滑 30 ms。

## 7　Air + Tone——空气噪声床

`NoiseBed`，混入湿路（受 Dry/Wet 控制）：

- **噪声源**：每通道独立种子的 xorshift32 白噪声（`s^=s<<13; s^=s>>17; s^=s<<5`），两声道去相关；
- **音色**：Q = 1.0 的二阶带通，中心频率 `800 × 15^tone` Hz（800 Hz…12 kHz，对数刻度）；
- **音量**：`gain = amount² × 0.12 × track² × levelNorm`——噪声床**连续跟随**输入包络（无门控/阈值）：处理器的跟随器（块峰值瞬时攻击、约 450 ms 释放）映射为 `track = smoothstep(clamp((followerDb + 60) / 48))`，输入约 −12 dBFS 时达到满量，噪声像"包裹"在声音外面的一层，静音中淡出；`levelNorm = √(2.4 kHz / f)` 使 Tone 扫动时响度恒定——Tone 只改变音色、不改变响度；系数按音频块计算（50 ms 平滑）。

## 8　输出链——对齐、混合与限制

```
out = dryDelayed × (1 − mix) + wet × mix
out × 10^(outGain/20)
if limiter: out = tanh(out)
```

- 干路经与当前 FFT 尺寸等长的延迟与湿路对齐，交叉淡化无梳状滤波；FFT 尺寸变化时延迟随之重对位；
- 延迟常量上报宿主（PDC 对齐其它轨道）；
- **Limiter 默认关闭**，且中性位置（|cents| < 0.005）完全跳过包络乘法，默认插入、不改动任何参数时插件保持透明。实测延迟对齐 null 残差为 **3.5e-04（0.035 %，约 −69 dB）** 最差采样——源于算法所用对称 Hann 窗固有的 COLA 微纹波（`Tests/EngineTests.cpp` 有断言）；远低于可闻阈值，DAW 内对消的实际残留还取决于宿主 PDC 对齐精度；
- tanh 软限制作用在输出增益之后，Out 旋钮同时是"进限制器的推动量"；
- 电平表：每块统计峰值，dB 域 0.6 dB/块衰减，经 `std::atomic<float>` 传给 UI；
- Bypass 参数开启时输出对齐后的干路（延迟恒定，宿主 PDC 不需要重算）。

## 9　预设与状态

- 全部参数走单一 `AudioProcessorValueTreeState`（ID 见 `Source/Parameters.h`），状态即 ValueTree，宿主保存/恢复与 DAW 工程兼容；
- 出厂预设（`Source/PresetManager.h`）为编译期常量表，套用时 `setValueNotifyingHost` 逐参数写入；按"从轻到重"排序，调整效果参数并按音色分配匹配的 FFT 尺寸（柔缓音色 4096、人声换声 2048、跳跃主奏 512 及以下）——Pitch、Out、Limiter 与 Auto Formant 始终保留用户自己的设置；
- **用户预设**是用户应用数据目录下的纯状态 XML 文件（Windows 为 `%APPDATA%\AkiTilt\Presets`，macOS 为 `~/Library/Application Support/AkiTilt/Presets`），以预设名命名，携带完整参数状态及 `presetName`/`presetVersion` 属性。`scanPresets()` 先列出工厂条目，再按自然顺序列出用户文件；`saveUserPreset()` 写入 `copyState()`，`applyFile()` 通过 `replaceState()` 恢复。文件是普通 XML，用户可以在系统文件管理器里直接重命名、删除或分享（编辑器的文件夹按钮会打开该目录）；
- A/B 对比在编辑器层实现：切换时把当前 APVTS 状态 `copyState()` 存入对应槽位，再 `replaceState()` 恢复另一槽。

## 10　UI 实现要点

- **频谱可视化**：**处理后**的帧幅度谱（包络搬移之后测量）与**搬移后的包络**经三重缓冲 + `std::atomic` 发布（音频线程写、消息线程读，无锁无撕裂），并附带帧计数器、该帧使用的频带宽度与频点数。编辑器只在计数器前进时拷贝新帧，频谱静音（能量门控）时完全跳过重绘——空闲开销接近零。路径抽稀到 ≤420 点，光标光晕用径向渐变替代模糊投影，overlay 读数只在数值真正变化时重绘；横轴 20 Hz–20 kHz 对数映射，纵轴 −78…+60 dB；bin→频率的映射读取该帧自身的 FFT 尺寸,任何分辨率下频谱都落在正确刻度上；
- **面板标注**：overlay 左上为 BANDS / TILT / WIDTH / AIR，右上为 OUT，附 spectrum/envelope 图例。暖色曲线即搬移后的包络——Bands 与 Formant 共同塑造的曲线。Tilt 响应以全宽曲线绘制（围绕 650 Hz 分频点的 `tanh((u − 0.504) × 6)`，幅度正比于 tilt，正值向右端上扬），底边高刻度显示分析区域（密度封顶约 64 列）。网格遵循市场 EQ 惯例：100 Hz / 1 kHz / 10 kHz 加粗带标签，其间为淡的倍频列，dB 行每 20 dB 一条并强调 0 dB。整个面板按 Tone 做发散式色调映射：默认（50%）为本色绿，偏低端泛棕、偏高端泛草绿（两端最高 22% 透明度）；
- **XY Pad**：X = Formant（±1200 ct），Y = Pitch（±12 st，上高下低），拖动即写参数（`setValueNotifyingHost`），双击归零；光标位置由定时器从参数原子值回读，拖动中不回写防抖动。Auto Formant 开启时光标显示检测器驱动的有效 cents,X 轴拖动被钉死；
- **轴锁与修饰键**：A / X / Y 药丸开关位于 pad 右下——**A** 开启 Auto Formant,**X** / **Y** 在拖动期间把某一轴锁定在当前值；按住 Alt 拖动会临时反转锁定。Shift 为微调（拖动增量缩至锚点周围的八分之一），Ctrl 吸附到音乐网格（100 ct / 1 半音）。被锁定的轴会同步禁用下方对应旋钮（X→Formant、Y→Pitch——不响应鼠标,右上角出现小锁徽章,点击徽章即解锁）,直到解锁；Pad 与旋钮行共享同一份锁状态。Auto 开启时 X 锁与 Formant 徽章变灰（不可用而非隐藏）,Formant 旋钮继续作为偏移工作；
- **FFT 尺寸选择与状态**：右卡中的下拉框绑定不可自动化的 `fftSize` choice 参数；其下方的状态标签显示当前尺寸与往返毫秒数,由定时器在文本变化时才刷新；
- **Mint 主题**：`Source/UI/Theme.h` 将 Aki-Design-System §3.4/附录 B 的令牌直译为 `juce::Colour` 常量（bg #E8F0E5、contrast #22362A、accent #3D624C、ramp #8FA89A/#A3C4A9/#B5D7C3 等），`AkiLookAndFeel` 实现旋钮（ramp 渐变弧 + 墨绿指针）、药丸开关、下拉框与卡片双 bezel；字体 Plus Jakarta Sans / Fraunces 72pt / IBM Plex Mono（OFL，静态字重，BinaryData 内置）；
- **缩放**：编辑器以 780×540 设计空间布局，`scaled()` 统一映射，锁定宽高比缩放（最小 620×429）。

## 11　测试

`Tests/EngineTests.cpp` 为离线渲染断言（`AkiTiltTests` 目标）：

- 正弦输入，中性设置下湿路 RMS 比值 ≈ 1（unity 重构）；
- **逐 FFT 尺寸（128…4096）**：上报的尺寸/频点数/延迟一致,中性位置延迟对齐 null 落在各尺寸的 COLA 纹波阈值内（残差随尺寸翻倍减半,128 ≈ 5.6e-3 → 4096 ≈ 1.8e-4）,±1200 ct 输出有限；
- 流中途切换 FFT 尺寸保持输出有限并正确生效；
- 双声道相同输入经 formant 引擎与 pitch 移调后输出保持一致（逐采样 write/read/advance 顺序回归）；
- 发布的帧计数器随 STFT 帧递增，包络快照随 Bands 宽度变化；
- **音高检测器**：110/220/440/880 Hz 纯音跟踪在锚点偏移 ±30 cents 内,宽带噪声判为无声；Auto 映射叠加旋钮偏移并钳制到 ±1200 ct；
- ±1200 ct 极限、+7 st 移调输出有限且能量正常；
- Tilt 0 dB 位精确旁路、+12 dB 提升高频；
- Width 200 % 使侧信号能量翻倍；Air 随输入电平起伏并在静音时淡出；
- 用户预设文件经写入 → 扫描 → 读回往返一致。
