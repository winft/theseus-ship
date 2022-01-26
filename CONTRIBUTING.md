# Contributing to KWinFT

 - [Logging and Debugging](#logging-and-debugging)
   - [General information about the running instance](#general-information-about-the-running-instance)
   - [Debug console](#debug-console)
   - [Runtime logging](#runtime-logging)
   - [Debugging with GDB](#debugging-with-gdb)
 - [Developing](#developing)
   - [Compiling](#compiling)
   - [Running Tests](#running-tests)
   - [Learning Material](#learning-material)
 - [Submission Guideline](#submission-guideline)
 - [Commit Message Guideline](#commit-message-guideline)
 - [Contact](#contact)

## Logging and Debugging
The first step in contributing to the project by either providing meaningful feedback or by directly
sending in patches is always the analysis of KWinFT's runtime.
For KWinFT that means querying general information about its internal state and studying its debug
log while running and afterwards.

### General information about the running instance
Some general information about the running KWinFT instance can be queried via D-Bus by the following
command (the qdbus tool must be installed):

    qdbus org.kde.KWin /KWin supportInformation

### Debug console
KWinFT comes with an integrated debug console. You can launch it with:

    qdbus org.kde.KWin /KWin org.kde.KWin.showDebugConsole

Note that the debug console provides much more information when KWinFT is running as a Wayland
compositor.

### Runtime logging
#### Preparations
To show more debug information in the log as a first step the following lines should be added to the
file
`$HOME/.config/QtProject/qtlogging.ini`
(create the file if it does not exist already):

    [Rules]
    kwin_core*=true
    kwin_platform*=true
    kwineffects*=true
    kwin_wayland*=true
    kwin_decorations*=true
    org.kde.kwindowsystem*=true
    kwin_tabbox*=true
    kwin_qpa*=true
    kwin_wl*=true
    kwin_xwl*=true
    kwin_perf*=true
    wrapland*=true
    kwin_libinput.info=true
    kwin_libinput.warning=true
    kwin_libinput.critical=true
    kwin_libinput.debug=false

The above list specifies `kwin_libinput.debug=false` because otherwise the log gets spammed with
lines whenever a mouse button is pressed.
In the same way other logging categories above can be switched on and off by changing the respective
boolean value in this file. The change will become active after a restart of KWinFT.

#### Simple session logging
If you start KWinFT through SDDM as part of a full Plasma session
you find its log output in the systemd journal.

You can retrieve its output specifically with:

    journalctl --user -u plasma-kwin_x11
    journalctl --user -u plasma-kwin_wayland

You can get live updates with the `-f` flag.
Note also that in an X11 session we have the possibility to restart KWinFT.
In this case only the first execution will log to the journal.

#### Live logging in a terminal
##### X11: In-session logging
In an X11 session it is very easy to log KWinFT from a terminal. Just execute the following command
to restart KWinFT:

    kwin_x11 --replace

This is of course not possible in a Wayland session because the session would immediately die with
the Wayland server KWinFT being restarted.

##### Wayland: Nested session logging
KWinFT as a Wayland compositor can be started nested in another Wayland or X11 session
what will print its debug log directly to the terminal emulator it was started from.
For that issue following command from the terminal emulator:

    dbus-run-session kwin_wayland --width=1920 --height=1080 --xwayland --exit-with-session konsole

This will start a nested KWinFT Wayland session with a default output size of 1080p, having
Xwayland enabled and the application Konsole already running in it.
This nested session will also go down automatically when the Konsole window *in the session* is
closed.

You can even start a full nested Plasma session by issuing the following command in the terminal
emulator:

    dbus-run-session startplasma-wayland

##### Wayland: VT session logging
Nested session logging is often not sufficient. The behavior of KWinFT as a Wayland compositor on
real hardware can only be tested when KWinFT is started from a pristine non-graphical state.

To do this switch to a free different virtual terminal (VT) with the key combination `CTRL+ALT+F<x>`
where `<x>` is a number from 1 onward.

For example `CTRL+ALT+F1` is always the SDDM session or an X11 session launched by SDDM
(this session then reuses the Xserver which was internally started by SDDM for SDDM's own graphical
output).
A Wayland session started from SDDM is normally put onto the next free VT
reachable with `CTRL+ALT+F2` since SDDM requires its Xserver to stay active on the first VT .

After we found a free VT and we logged in to it a Wayland KWinFT session can be launched with the
command:

    dbus-run-session kwin_wayland --xwayland --exit-with-session konsole 2>&1 | tee my-kwinft-output

This is similar to above command for running it in a nested session but without the parameters
defining the resolution
(instead the best resolution is selected by the hardware driver automatically).

Additionally the error output is redirected to the standard output (by `2>&1`)
and then all output copied with *tee* into the file "my-kwinft-output" in the current working
directory.
The log can then be read from this file either after the session ended or live-updating with the
command `tail -f` again.

As above we can start a full Plasma session as well from terminal. For that issue the command:

    dbus-run-session startplasma-wayland 2>&1 | tee my-kwinft-output

Again the log output is copied with tee into the file "my-kwinft-output" in the current working
directory.

Note that by default when one is running KWinFT through any of the startplasma
methods, it is invoked using a wrapper that will automatically relaunch KWinFT
when it crashes. In order to disable this behavior you can define the
environment variable `KWIN_DISABLE_RELAUNCH`.

    export KWIN_DISABLE_RELAUNCH=1

#### Logging a KWinFT Wayland session through SSH
Starting KWinFT from a free VT as shown above is sufficient for quickly debugging singular issues
but for rapid prototyping it is not enough since it requires a VT switch and later on reading the
debug output from a separate file.
Additionally if the session crashes the VT might be stuck what can potentially even lead to an
unusable device until after a hard reset (reboot via hardware key).

It would be better if we could start a KWinFT session on a VT from a separate device and then seeing
the log live on this secondary device and in case of stuck session kill the session on the VT from
this second device.

With Secure Shell (SSH) this is possible.
For that you need to [create a SSH session][ssh-intro] from your secondary device that connects to
the testing device.
That means your secondary device – where you will watch the log output at – is the *SSH client* and
your testing device – where KWinFT will be executed on – is the *SSH server*.

Once this done on your testing device you need to go to a free VT again and login. You have now
multiple options to launch KWinFT from your secondary device *on this VT* of the testing device:
* Start a terminal multiplexer like *GNU Screen* or *tmux* in the VT and attach to its session in
  the SSH session.
  * For Screen issue on the VT `screen -S tty` and then `screen -x tty` in the SSH session.
  * For tmux issue on the VT `tmux` and then `tmux -a` in the SSH session.
* As an alternative you can simply set the XDG_SESSION_ID in the SSH session to the one in the VT
  session.
  For that issue first `echo $XDG_SESSION_ID` on the VT what gives you some integer value x.
  Then set the same environment variable to that value in the SSH session
  by issuing `XDG_SESSION_ID=x`.

The variant with XDG_SESSION_ID has the advantage
that the terminal the SSH session is running in behaves as usual.
If you are not used to the way Screen or tmux change the terminal input
you might feel more comfortable this way.

A downside of the variant is that depending on your systemd version
you might have to add a separate polkit rule to allow that.
To do that as described [here][polkit-rule] create a new file

     /etc/polkit-1/rules.d/10-allow-inactive-chvt.rules

with the following content:

     /* Allow users with inactive sessions to switch the console */
     polkit.addRule(function(action, subject) {
         if (action.id == "org.freedesktop.login1.chvt" &&
             subject.local && subject.session) {
             return polkit.Result.YES;
         }
     });

In any case you should be now able to start KWinFT directly in the SSH session with:

    dbus-run-session kwin_wayland --xwayland --exit-with-session konsole

Or as part of a full Plasma session with:

    dbus-run-session startplasma-wayland

This is very similar to starting KWinFT from the VT directly.
The only difference is that we do not redirect the output or copy it with tee to a file
since we can now easily follow it on the screen of our second device.

#### DRM logging
In a Wayland session we talk through wlroots directly to the
[Direct Rendering Manager (DRM)](https://en.wikipedia.org/wiki/Direct_Rendering_Manager)
subsystem of the Linux kernel
for showing graphical buffers and configuring outputs.

Debugging issues with it directly can be difficult.
A first step is to priunt out the DRM logs to dmesg what usually isn't done by default.
How to enable such DRM logging is described in the
[wlroots wiki](https://gitlab.freedesktop.org/wlroots/wlroots/-/wikis/DRM-Debugging).

You can also use the following script
to have a convenient way of enabling it temporarily
from the command line:

    #!/usr/bin/env bash

    # Enable verbose DRM logging
    echo 0xFE | sudo tee /sys/module/drm/parameters/debug > /dev/null
    # Clear kernel logs
    sudo dmesg -C
    # Continuously write DRM logs to a file, in the background
    sudo dmesg -w > $HOME/dmesg.log &

    echo "DRM logging activated. Waiting for Ctrl+C..."
    ( trap exit SIGINT ; read -r -d '' _ </dev/tty )

    # Disable DRM logging
    echo 0x00 | sudo tee /sys/module/drm/parameters/debug > /dev/null
    echo
    echo "Ctrl+C received. Disabled DRM logging and exit."

Note that the DRM log output is very verbose.
So only enable it shortly before triggering the faulty behavior
and disable it directly afterwards again.
You then find the dmesg log in `$HOME/dmesg.log`.

### Debugging with GDB
If the KWinFT process crashes the GNU Debugger (GDB) can often provide valuable information
about the cause of the crash by reading out a backtrace leading to the crash.

#### Access backtrace of past crashes
Enable the [recording of core dumps][arch-core-dump] and after a crash issue `coredumpctl` to see a
list of all previous backtraces. Then read the backtrace with

    coredumpctl gdb kwin_wayland
    bt

or

    coredumpctl gdb <pid>
    bt

where the first variant can be used
when you want to analyse the most recent backtrace generated for KWinFT
and in the secondary command `<pid>` is the PID of one past KWinFT process
in the list you read before with `coredumpctl`
and that you want to analyse now with GDB.

#### Live backtraces
Running gdb on the executing program is often a faster work flow.
A first variant for that is to start one of the `kwin_wayland` commands listed above directly
through gdb like with the command

    dbus-run-session gdb --ex r --args kwin_wayland --xwayland --exit-with-session konsole

with which a gdb-infused KWinFT Wayland session is either started as a nested session or on a VT
through SSH.

It is not recommended to run above command directly from a VT since on a crash you will not be able
to interact with GDB and even without a crash you will not be able to exit the process anymore.

Another option is to attach GDB to an already running KWinFT process with the following command:

    sudo gdb --ex c --pid `pidof kwin_wayland`

Again it is recommended to only do this for a nested session or from a secondary device
since otherwise we would not be able to regain control after a crash or when the process exits.


## Developing

### Compiling
To start writing code for KWinFT first the project needs to be compiled.
You usually want to compile KWinFT from its
[master branch](https://gitlab.com/kwinft/kwinft/-/commits/master/)
as it reflects the most recent state of development.

#### Using FDBuild
Since some of KWinFT's dependencies are moving targets in KDE
that do not offer backwards compatibility guarantees,
it is often required to build these KDE dependencies also from their master branches
and rebuild them regularly from the most recent state of the master branch.
The most convenient way for that is to use the
[FDBuild](https://gitlab.com/kwinft/fdbuild)
tool.
It comes with a template mechanism
that creates a subdirectory structure with all required KWinFT and KDE projects to build.
For that issue the command:
```
fdbuild --init-with-template kwinft-plasma-meta
```
After the project templating has finished,
go into the toplevel directory of the just created subdirectory structure.
FDBuild uses fdbuild.yaml files in directories it is supposed to work on
to remember settings about the projects inside these directories.

Important is the setting specifying the installation location of the projects.
This is set in the fdbuild.yaml file inside the toplevel directory.
Adjust the setting to your liking. Recommended is setting it to a subdirectory inside `/opt`,
for example `/opt/kwinft`.

Then simply run FDBuild without any arguments from the toplevel directory
and FDBuild will try to compile and install all projects one after the other.

Note that this will likely fail for several projects on the first run
since you require additional dependencies.
Check the FDBuild log output to find out what dependencies are missing.
A complete list of required dependencies with drifting correctness is also listed
[in the KDE Community Wiki](https://community.kde.org/Guidelines_and_HOWTOs/Build_from_source/Install_the_dependencies).

Once you have installed additional dependencies and want to continue building the projects
from where it failed command:
```
fdbuild --resume-from <project-that-failed>
```

#### Plasma Desktop Session Integration
With this setup KWinFT can be run already as a standalone binary for example from a VT.
In case you did not install into your `/usr` directory,
as is recommended,
additional steps are required
to run a full Plasma Desktop session together with your self-compiled KWinFT.

The Plasma Desktop session requires
sourcing of some environment variables
pointing to the install location.
If you run the `dbus-run-session startplasma-wayland` command from a terminal,
you can source the following script to achieve that:
```
#!/bin/bash

export XDG_CURRENT_DESKTOP=KDE
source <path-to-projects-toplevel-directory>/kde/plasma-desktop/build/prefix.sh
```


Additionally SDDM session scripts can be installed
and the session started directly from the drop-down menu inside SDDM. For that run:
```
<path-to-projects-toplevel-directory>/kde/plasma-workspace/login-sessions/install-sessions.sh
```

### Running Tests
KWinFT comes with over 100 integration tests
which check the expected behavior of different parts of the application.

To run all relevant tests go to the build directory of KWinFT and issue:
```
dbus-run-session ctest -E 'testLockScreen|testModifierOnlyShortcut'
```

This command is composited from two commands. Let's quickly explain the different parts:
* `dbus-run-session`: starts a new DBus session for the tests, so your current session is unimpaired.
* `ctest`: the CMake testing utility running binaries, that have been marked as tests in the CMake files.
* `-E 'testLockScreen|testModifierOnlyShortcut'`: exclude two tests that are currently also not run on the CI.

You can also run a single test.
All tests are separate binaries in the `bin` directory inside the build directory.
That means in order to test e.g. pointer input run from the build directory:
```
dbus-run-session bin/testPointerInput
```

You can also run a specific test function inside such a test.
For example to run the
[`testPopup` function](https://gitlab.com/kwinft/kwinft/-/blob/0435b1d/autotests/integration/pointer_input.cpp#L1229-1344)
in the pointer input test run:
```
dbus-run-session bin/testPointerInput testPopup
```

### Learning Material
The KWinFT source code is vast and complex.
Understanding it requires time and practice.
For the beginning there are still few available resources to get an overview:
* [Xplain](https://magcius.github.io/xplain/article/), introduction and explanations for X11.
* [How X Window Managers Work](https://jichu4n.com/posts/how-x-window-managers-work-and-how-to-write-one-part-i/),
  series on how to write an X window manager.
* [The Wayland Book](https://wayland-book.com/), explains fundamental concepts of Wayland.
* [KWin now and tomorrow at XDC 2019](https://www.youtube.com/watch?v=vj70xmG_5Bs),
  gives an overview about the internal structure of KWinFT.


## Submission Guideline
Code contributions to KWinFT are very welcome but follow a strict process that is laid out in
detail in Wrapland's [contributing document][wrapland-submissions].

*Summarizing the main points:*

* Use [merge requests][merge-request] directly for smaller contributions, but create
  [issue tickets][issue] *beforehand* for [larger changes][wrapland-large-changes].
* Adhere to the [KDE Frameworks Coding Style][frameworks-style].
* Merge requests have to be posted against master or a feature branch. Commits to the stable branch
  are only cherry-picked from the master branch after some testing on the master branch.

Also make sure to increase the default pipeline timeout to 2h in `Settings > CI/CD > General Pipelines > Timeout`.

## Commit Message Guideline
The [Conventional Commits 1.0.0][conventional-commits] specification is applied with the following
amendments:

* Only the following types are allowed:
  * build: changes to the CMake build system, dependencies or other build-related tooling
  * ci: changes to CI configuration files and scripts
  * docs: documentation only changes to overall project or code
  * feat: a new feature is added or a previously provided one explicitly removed
  * fix: bug fix
  * perf: performance improvement
  * refactor: rewrite of code logic that neither fixes a bug nor adds a feature
  * style: improvements to code style without logic change
  * test: addition of a new test or correction of an existing one
* Only the following optional scopes are allowed:
  * debug: internal debug tools
  * deco: window decorations
  * effect: libkwineffects and internal effects handling
  * input: libinput integration and input redirection
  * hw: platform integration (drm, virtual, wayland,...)
  * qpa: internal Qt Platform Abstraction plugin of KWinFT
  * scene: composition of the overall scene
  * script: API for KWinFT scripting
  * space: virtual desktops and activities, workspace organisation and window placement
  * xwl: XWayland integration
* Any line of the message must be 90 characters or shorter.
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
[arch-core-dump]: https://wiki.archlinux.org/index.php/Core_dump
[conventional-commits]: https://www.conventionalcommits.org/en/v1.0.0/#specification
[frameworks-style]: https://community.kde.org/Policies/Frameworks_Coding_Style
[issue]: https://gitlab.com/kwinft/kwinft/-/issues
[merge-request]: https://gitlab.com/kwinft/kwinft/-/merge_requests
[plasma-schedule]: https://community.kde.org/Schedules/Plasma_5
[polkit-rule]: https://github.com/swaywm/wlroots/issues/2236#issuecomment-635934081
[ssh-intro]: https://www.digitalocean.com/community/tutorials/ssh-essentials-working-with-ssh-servers-clients-and-keys
[wrapland-contact]: https://gitlab.com/kwinft/wrapland/-/blob/master/CONTRIBUTING.md#contact
[wrapland-large-changes]: https://gitlab.com/kwinft/wrapland/-/blob/master/CONTRIBUTING.md#issues-for-large-changes
[wrapland-submissions]: https://gitlab.com/kwinft/wrapland/-/blob/master/CONTRIBUTING.md#submission-guideline
[wrapland-tooling]: https://gitlab.com/kwinft/wrapland/-/blob/master/CONTRIBUTING.md#tooling
