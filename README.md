# KWinFT

KWinFT (KWin Fast Track) is an easy to use, but flexible, composited window manager for X.Org
windowing systems (Wayland, X11) on Linux.

The KWinFT project consists of the window manager [KWinFT][kwinft] and the accompanying but
independent libwayland wrapping Qt/C++ library [Wrapland][wrapland].

The project is a reboot of KWin and KWayland with the explicit goal to be well organized, focused
and using modern techniques and good practice in software development in order to allow the
development team to move faster, serving new fundamental features and code refactors at rapid pace.

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

[kwinft]: https://gitlab.com/kwinft/kwinft
[wrapland]: https://gitlab.com/kwinft/wrapland
