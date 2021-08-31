# KWinFT

KWinFT (KWin Fast Track) is a robust, fast and versatile yet
easy to use composited window manager for the
[Wayland](https://wayland.freedesktop.org/) and
[X11](https://en.wikipedia.org/wiki/X_Window_System)
windowing systems on Linux.

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

## Usage
You can use KWinFT for now as a drop-in replacement for classic KWin. All configuration, plugins
and shell interaction should continue as usual. Besides all current dependencies of KWin the
Wrapland library is needed.
> :warning: The capability of being a drop-in replacement might get lost in a future release. If
this happens possible conflicts will be announced in the changelog.

> :warning: Although KWinFT uses autotests to check for issues and it is in daily use from the
master branch so problems can be noticed early and likely before release, being fast still means
that the probability for regressions increases. Make sure to have a plan B in case your graphical
session becomes unusable because of that. If you don't, better stay with classic KWin.

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
