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
Please refer to [hacking documentation](HACKING.md) for how to build and start KWinFT. Further
information about KWinFT's test suite can be found in [TESTING.md](TESTING.md). When you want to
contribute to the project read [CONTRIBUTING.md](CONTRIBUTING.md) on how to do that.

> :warning: There is for the foreseeable future no API/ABI-stability guarantee. You need to align
your releases with KWinFT release schedule and track breaking changes (announced in the changelog).

[kwinft]: https://gitlab.com/kwinft/kwinft
[wrapland]: https://gitlab.com/kwinft/wrapland
