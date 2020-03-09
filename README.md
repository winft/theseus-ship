# KWinFT

[![pipeline status][pipe-status-img]][pipe-status-link]

KWinFT (KWin Fast Track) is an easy to use, but flexible, composited Window Manager for X.Org windowing systems (Wayland, X11) on Linux. KWinFT is a fork of KWin with the goal to move faster, serving new fundamental features and code refactors at rapid pace.

# Usage
You can use KWinFT for now as a drop-in replacement for classic KWin. All configuration, plugins and shell interaction should continue as usual. Besides all current dependencies of KWin the Wrapland library is needed.
> :warning: The capability of being a drop-in replacment might get lost in a future release. If this happens possible conflicts will be announced in the changelog.

> :warning: Being fast means that the probability for regressions increases. Make sure to have a plan B in case your graphical session becomes unusable because of that.

# Development
Please refer to [hacking documentation](HACKING.md) for how to build and start KWinFT. Further information about KWinFT's test suite can be found in [TESTING.md](TESTING.md). When you want to contribute to the project read [CONTRIBUTING.md](CONTRIBUTING.md) on how to do that.
> :warning: There is for the forseeable future no API/ABI-stability guarantee. You need to align your releases with KWinFT release schedule and track breaking changes (announced in the changelog).

[pipe-status-img]: https://gitlab.com/kwinft/kwinft/badges/master/pipeline.svg
[pipe-status-link]: https://gitlab.com/kwinft/kwinft/-/commits/master
