# Contributing to KWinFT

 - [Submission Guideline](#submission-guideline)
 - [Commit Message Guideline](#commit-message-guideline)
 - [Contact](#contact)

## Submission Guideline
Contributions to KWinFT are very welcome but follow a strict process that is layed out in detail in
Wrapland's [contributing document][wrapland-submissions].

*Summarizing the main points:*

* Use [merge requests][merge-request] directly for smaller contributions, but create
  [issue tickets][issue] *beforehand* for [larger changes][wrapland-large-changes].
* Adhere to the [KDE Frameworks Coding Style][frameworks-style].
* Merge requests have to be posted against master or a feature branch. Commits to the stable branch
  are only cherry-picked from the master branch after some testing on the master branch.

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
See [Wrapland's documentation][wrapland-tooling] for available tooling.

## Contact
See [Wrapland's documentation][wrapland-contact] for contact information.

[angular-revert]: https://github.com/angular/angular/blob/3cf2005a936bec2058610b0786dd0671dae3d358/CONTRIBUTING.md#revert
[angular-subject]: https://github.com/angular/angular/blob/3cf2005a936bec2058610b0786dd0671dae3d358/CONTRIBUTING.md#subject
[conventional-commits]: https://www.conventionalcommits.org/en/v1.0.0/#specification
[frameworks-style]: https://community.kde.org/Policies/Frameworks_Coding_Style
[issue]: https://gitlab.com/kwinft/kwinft/-/issues
[merge-request]: https://gitlab.com/kwinft/kwinft/-/merge_requests
[plasma-schedule]: https://community.kde.org/Schedules/Plasma_5
[wrapland-contact]: https://gitlab.com/kwinft/wrapland/-/blob/master/CONTRIBUTING.md#contact
[wrapland-large-changes]: https://gitlab.com/kwinft/wrapland/-/blob/master/CONTRIBUTING.md#issues-for-large-changes
[wrapland-submissions]: https://gitlab.com/kwinft/wrapland/-/blob/master/CONTRIBUTING.md#submission-guideline
[wrapland-tooling]: https://gitlab.com/kwinft/wrapland/-/blob/master/CONTRIBUTING.md#tooling
