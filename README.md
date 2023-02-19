<!--
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
-->

# KWinFT

KWinFT (KWin Fast Track) is a robust, fast and versatile yet
easy to use composited window manager for the
[Wayland](https://wayland.freedesktop.org/) and
[X11](https://en.wikipedia.org/wiki/X_Window_System)
windowing systems on Linux.

<p>
  <div align="center">
    <img style="float:left;box-shadow: 7px 7px 7px #666666;" src="docs/assets/desktop-screenshot.png"
         alt="KWinFT Plasma Wayland session"
         width="800">
  </div>
</p>
<br>

KWinFT is intended to be used as part of a
[KDE Plasma Desktop](https://kde.org/plasma-desktop/).
The KWinFT project is a reboot of KDE's
[KWin](https://en.wikipedia.org/wiki/KWin).
KWinFT differentates itself from KWin in some important aspects:
* KWinFT values stability and robustness.
  This is achieved through upholding strict development standards
  and deploying modern development methods to prevent regressions and code smell.
* KWinFT values collaboration with competitors and and upstream partners.
  We want to overcome antiquated notions on community divisions
  and work together on the best possible Linux graphics platform.
* KWinFT values the knowledge of experts but also the curiosity of beginners.
  Well defined and transparent decision processes enable expert knowledge to proliferate
  and new contributors to easily find help on their first steps.

## Installation
Your distribution might provide KWinFT already as a package:
  * Arch(AUR): [kwinft](https://aur.archlinux.org/packages/kwinft)
  * Manjaro: `sudo pacman -S kwinft`

Alternatively KWinFT can be compiled from source.
If you do that manually you have to check for your specific distribution
how to best get the required dependencies.
You can also make use of the FDBuild tool to automate that process as described
[here](CONTRIBUTING.md#compiling).

## Usage
KWinFT can be used as a drop-in replacement for KWin inside a KDE Plasma Desktop session.
After installation and a system restart the KWinFT binary will execute.
All configuration, plugins and shell interaction transfer over.

## Development
The [CONTRIBUTING.md](CONTRIBUTING.md) document contains all information
on how to get started with:
* providing useful debug information,
* building KWinFT
* and doing your first code submission to the project.

If you want to write an effect or script for KWinFT
you can do this as usual with the respective APIs that KWinFT provides.
We try to keep these APIs compatible with the ones of KWin
but there is no guarantee on that.
If there are incompatible changes or API-breaking changes in general,
this will be announced in the changelog.
