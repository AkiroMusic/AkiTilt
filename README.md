# AkiTilt

[![build](https://github.com/AkiroMusic/AkiTilt/actions/workflows/build.yml/badge.svg)](https://github.com/AkiroMusic/AkiTilt/actions/workflows/build.yml)

**Formant & texture morph for Botanica (petalcore) makers.**
A VST3 / AU / Standalone audio plugin built with JUCE 8, styled with the Aki
Design System "Mint Fresh" theme.

![AkiTilt main interface](Assets/akitilt-ui.png)

## About

AkiTilt grew out of the way producers in the Botanica scene shape voices and
textures: automating formant on guitar loops, morphing voices into new
characters, tuning noise into air. Its formant engine follows the spectral
approach used by tools such as Alexander Panos' *Color Transfer* formant
shifter, and pairs it with the companions those workflows call for — pitch,
tilt EQ, width, and an air bed — in one panel.

## Features

| Module | Range | What it does |
|---|---|---|
| Formant | ±1200 ct | Shifts the spectral envelope without changing pitch |
| Auto Formant | on/off | Tracks the input pitch and shifts formants with it; the knob becomes a fine offset |
| Bands | 0–32 | Spectral-envelope analysis resolution |
| Pitch | ±12 st | Granular pitch shift, decoupled from formant |
| Tilt | ±12 dB | One-knob brightness tilt with loudness compensation |
| Width | 0–200 % | Mid/side stereo widening on the wet path |
| Air + Tone | 0–100 % | Filtered noise bed, warm hiss to open air |
| Out | −24…+12 dB | Output trim into a soft tanh limiter |
| FFT Size | 128–4096 | Analysis window: latency vs. formant smoothness |

Extras: XY pad (formant × pitch) with a live spectrum display, 9 factory
presets plus saveable user presets (plain XML files you can manage in the OS
file manager), A/B compare, in/out level meters, full host automation,
latency reporting.

## Installation

**VST3** — copy `release/AkiTilt.vst3` into your system VST3 folder
(`C:\Program Files\Common Files\VST3` on Windows,
`/Library/Audio/Plug-Ins/VST3` on macOS).

**AU (macOS)** — copy the built `AkiTilt.component` into
`/Library/Audio/Plug-Ins/Components`.

> macOS builds are ad-hoc signed. If a plugin bundle downloaded from CI
> refuses to load (Gatekeeper quarantine), clear the flag once:
> `xattr -dr com.apple.quarantine AkiTilt.component`

**Standalone** — run `release/AkiTilt.exe` (Windows) or the built
`AkiTilt.app` (macOS).

After a build, `release/` at the repository root holds a fresh copy of the
binaries; it is updated automatically on every build.

## Building from source

Requirements: CMake ≥ 3.24, internet access for the first JUCE download, and
Visual Studio 2022 (Windows) or Xcode (macOS).

```bash
git clone --depth 1 --branch 8.0.8 https://github.com/juce-framework/JUCE.git libs/JUCE

# Windows (Visual Studio 2022)
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release

# macOS (Xcode, universal arm64 + x86_64)
cmake -S . -B build -G Xcode -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
cmake --build build --config Release
```

Offline DSP tests:

```bash
cmake --build build --config Release --target AkiTiltTests
build/AkiTiltTests_artefacts/Release/AkiTiltTests
```

Continuous integration builds both platforms and runs the tests on every
push (`.github/workflows/build.yml`).

Documentation: [User Manual](MANUAL.md) · [Technical Notes](TECHNICAL.md).

## Repository layout

```
Source/            Plugin sources (DSP / UI / presets)
Tests/             Offline engine tests
Assets/            Fonts and media assets
release/           Ready-to-test binaries, updated on every build
.github/           CI workflow (Windows + macOS)
MANUAL.md          User manual (English / 中文)
TECHNICAL.md       Technical notes (English / 中文)
libs/JUCE/         JUCE 8 (git-ignored, cloned at setup)
```

Fonts are licensed under the SIL Open Font License. This project is
released under the GNU GPL v3 — see [LICENSE](LICENSE).

---

# AkiTilt（中文）

**为 Botanica（petalcore）制作人打造的变形与质感插件。**
基于 JUCE 8 的 VST3 / AU / Standalone 插件，界面采用 Aki 设计系统的薄荷清新主题。

## 简介

AkiTilt 源自 Botanica 场景里制作人处理人声与质感的习惯：在吉他循环上自动化 formant、把人声变成新的角色声线、把噪声调成空气感。它的 formant 引擎沿用 Alexander Panos《Color Transfer》等工具所代表的频谱处理思路，并把这类工作流需要的伙伴模块——音高、倾斜 EQ、宽度、空气床——整合进同一块面板。

## 功能

| 模块 | 范围 | 作用 |
|---|---|---|
| Formant | ±1200 ct | 只移动频谱包络、不改变音高 |
| Auto Formant | 开/关 | 跟踪输入音高自动移动 formant,旋钮变为微调偏移 |
| Bands | 0–32 | 频谱包络的分析分辨率 |
| Pitch | ±12 st | 颗粒音高移动，与 formant 解耦 |
| Tilt | ±12 dB | 单旋钮明暗倾斜，带响度补偿 |
| Width | 0–200 % | 湿路中侧立体声加宽 |
| Air + Tone | 0–100 % | 滤波噪声底，从暖 hiss 到开阔空气感 |
| Out | −24…+12 dB | 输出电平，后接 tanh 软限制器 |
| FFT Size | 128–4096 | 分析窗口：延迟与 formant 平滑度的取舍 |

另有：XY Pad（formant × pitch，带实时频谱显示）、9 个出厂预设与可保存的用户预设（纯 XML 文件，可在资源管理器中直接管理）、A/B 对比、输入/输出电平表、完整宿主自动化、延迟上报。

## 安装

**VST3** — 将 `release/AkiTilt.vst3` 复制到系统 VST3 目录（Windows 为 `C:\Program Files\Common Files\VST3`,macOS 为 `/Library/Audio/Plug-Ins/VST3`）。

**AU（macOS）** — 将构建出的 `AkiTilt.component` 复制到 `/Library/Audio/Plug-Ins/Components`。

> macOS 构建为 ad-hoc 签名。若从 CI 下载的插件无法加载（Gatekeeper 隔离属性），执行一次以下命令清除:
> `xattr -dr com.apple.quarantine AkiTilt.component`

**Standalone** — 直接运行 `release/AkiTilt.exe`（Windows）或构建出的 `AkiTilt.app`（macOS）。

每次构建完成后，仓库根目录的 `release/` 都会自动更新为最新的二进制文件。

## 从源码构建

环境要求：CMake ≥ 3.24，首次构建需联网拉取 JUCE，Windows 需 Visual Studio 2022,macOS 需 Xcode。

```bash
git clone --depth 1 --branch 8.0.8 https://github.com/juce-framework/JUCE.git libs/JUCE

# Windows（Visual Studio 2022）
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release

# macOS（Xcode,arm64 + x86_64 通用二进制）
cmake -S . -B build -G Xcode -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
cmake --build build --config Release
```

离线 DSP 测试：

```bash
cmake --build build --config Release --target AkiTiltTests
build/AkiTiltTests_artefacts/Release/AkiTiltTests
```

持续集成会在每次推送时构建双平台并运行测试（`.github/workflows/build.yml`）。

文档：[使用说明书](MANUAL.md) · [技术文档](TECHNICAL.md)。

## 目录结构

```
Source/            插件源码（DSP / UI / 预设）
Tests/             离线引擎测试
Assets/            字体与媒体资源
release/           可直接测试的二进制，随构建自动更新
.github/           CI 工作流（Windows + macOS）
MANUAL.md          使用说明书（English / 中文）
TECHNICAL.md       技术文档（English / 中文）
libs/JUCE/         JUCE 8（git 忽略，构建前克隆）
```

字体遵循 SIL Open Font License。本项目以 GNU GPL v3 发布，详见 [LICENSE](LICENSE)。
