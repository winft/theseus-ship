# Contributing to KWinFT

 - [Submission Guideline](#submission-guideline)
 - [Commit Message Guideline](#commit-message-guideline)
 - [Contact](#contact)

## Submission Guideline
The project follows the [KDE Frameworks Coding Style][frameworks-style].

A merge request to the master branch must be opened. Larger changes must be split up into smaller logical commits.

KWinFT releases are aligned with Plasma releases. See the [Plasma schedule][plasma-schedule] for information on when the next new major version is released from master branch or a minor release with changes from one of the bug-fix branches.

## Commit Message Guideline
The [Conventional Commits 1.0.0][conventional-commits] specification is applied with the following amendments:

* Only the following types are allowed:
  * build: changes to the CMake build system, dependencies or other build-related tooling
  * ci: changes to CI configuration files and scripts
  * docs: documentation only changes to overall project or code
  * feat: new feature
  * fix: bug fix
  * perf: performance improvement
  * refactor: rewrite of code logic that neither fixes a bug nor adds a feature
  * style: improvements to code style without logic change
  * test: addition of a new test or correction of an existing one
* Only the following optional scopes are allowed:
  * deco: window decorations
  * effect: libkwineffects and internal effects handling
  * input: libinput integration and input redirection
  * hw: platform integration (drm, virtual, wayland,...)
  * qpa: internal Qt Platform Abstraction plugin of KWinFT
  * scene: composition of the overall scene
  * script: API for KWinFT scripting
  * space: virtual desktops and activities, workspace organisation and window placement
  * xwl: XWayland integration
* Angular's [Revert][angular-revert] and [Subject][angular-subject] policies are applied.

Commits deliberately ignoring this guideline will not be merged.

### Example

    fix(deco): provide correct return value

    For function exampleFunction the return value was incorrect.
    Instead provide the correct value A by changing B to C.

## Contact
Real-time communication about the project happens on the IRC channel `#plasma` on freenode and the bridged Matrix room `#plasma:kde.org`.

[frameworks-style]: https://community.kde.org/Policies/Frameworks_Coding_Style
[plasma-schedule]: https://community.kde.org/Schedules/Plasma_5
[conventional-commits]: https://www.conventionalcommits.org/en/v1.0.0/#specification
[angular-revert]: https://github.com/angular/angular/blob/3cf2005a936bec2058610b0786dd0671dae3d358/CONTRIBUTING.md#revert
[angular-subject]: https://github.com/angular/angular/blob/3cf2005a936bec2058610b0786dd0671dae3d358/CONTRIBUTING.md#subject
