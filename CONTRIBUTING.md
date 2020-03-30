# Contributing to KWinFT

 - [Submission Guideline](#submission-guideline)
 - [Commit Message Guideline](#commit-message-guideline)
 - [Contact](#contact)

## Submission Guideline
Contributions are very welcome but follow a strict process.

### Issues for large changes
For smaller contributions like bug fixes or spelling corrections it is sufficient to open a
[merge request][merge-request].

For larger feature patches or code refactorings opening an [issue ticket][issue] *beforehand* is
mandatory. In such an issue primarily must be outlined:

* The current situation and why there is a problem.
* What kind of solution is intended.

This description is to write in the style of a white paper. That means in prose. It must be concise
but explicative enough, if necessary with graphics, diagrams, examples, such that a maintainer is
able to quickly understand the gist of it and can come to a decision if this is indeed a valid
issue and the proposed solution is sensible as well as if it fits the overall project direction.

While such an issue must be created before a merge request for the final implementation of the
proposed solution is opened, it is permissible to attach a sample, prototype branch implementing the
proposed solution, demonstrating its applicability. There is no guarantee though that this sample
implementation or even the proposed solution at all will be selected for further processing.

Because in general such issues will have the *decide* label applied in the beginning until a
maintainer has screened the issue. And the maintainer will come to one of the following
decisions:

* The issue is not valid and it will be closed.
* The issue is valid but the proposed solution is not adequate or there is no concrete solution yet
specified. Then the issue will keep the decide label until that changes.
* The proposed solution is adequate but too wide for a single issue. In this case child-issues can
be opened with the initial main/overview issue keeping the decide label.
* The proposed solution is sensible and actionable. Then the decide label will be replaced with the
*execute* label. At this point an issue might be assigned to a contributor for executing the
described solution and submitting it in form of a merge request.

The overall idea behind this process is not to make contributions difficult but to allow the
maintainers and project lead to act quickly on contributions and to keep them moving forward at a
consistent pace while staying on top of all relevant changes in the project and keeping the
overall project direction.

If a change is small enough for making an issue optional or not is the maintainer's decision. In
general it is better to assume opening an issue is needed, especially for feature changes and
refactorings.

### Merge requests
The change in a merge request must be split up in small logical partitioned commits.

The project follows the [KDE Frameworks Coding Style][frameworks-style]. In the future it is aimed
for to integrate this into the linters.

KWinFT releases are aligned with Plasma releases. See the [Plasma schedule][plasma-schedule] for
information on when the next new major version is released from master branch or a minor release
with changes from one of the bug-fix branches.

To land the merge request in the next release it must be concluded before the branch off to the new
version. The exception are bug fixes. Such contributions will be merged first to the master branch
and if no issues with the change were found on there will be cherry-picked back to the current
stable branch. The current stable branch is the last branch being branched off with the name
pattern *Plasma/\**.

Code contributions to later branches than the current stable branch are not possible.

## Commit Message Guideline
The [Conventional Commits 1.0.0][conventional-commits] specification is applied with the following
amendments:

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

### Example

    fix(deco): provide correct return value

    For function exampleFunction the return value was incorrect.
    Instead provide the correct value A by changing B to C.

### Tooling
There is a linter on merge requests checking every included commit for being in line with this
specification. This linter can also be used locally before opening a merge request:

    yarn global add commitlint
    yarn add conventional-changelog-conventionalcommits
    commitlint --verbose --config ci/commitlint.config.js -f origin/master

## Contact
Real-time communication about the project happens on the IRC channel `#plasma` on freenode and the
bridged Matrix room `#plasma:kde.org`.

[merge-request]: https://gitlab.com/kwinft/kwinft/-/merge_requests
[issue]: https://gitlab.com/kwinft/kwinft/-/issues
[frameworks-style]: https://community.kde.org/Policies/Frameworks_Coding_Style
[plasma-schedule]: https://community.kde.org/Schedules/Plasma_5
[conventional-commits]: https://www.conventionalcommits.org/en/v1.0.0/#specification
[angular-revert]: https://github.com/angular/angular/blob/3cf2005a936bec2058610b0786dd0671dae3d358/CONTRIBUTING.md#revert
[angular-subject]: https://github.com/angular/angular/blob/3cf2005a936bec2058610b0786dd0671dae3d358/CONTRIBUTING.md#subject
