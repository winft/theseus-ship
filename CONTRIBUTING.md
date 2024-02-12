<!--
SPDX-FileCopyrightText: 2024 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
-->

# Contributing to Theseus' Ship

- [Contributing to Theseus' Ship](#contributing-to-theseus-ship)
  - [Logging and Debugging](#logging-and-debugging)
    - [Runtime logging](#runtime-logging)
      - [Simple session logging](#simple-session-logging)
      - [Live logging in a terminal](#live-logging-in-a-terminal)
        - [X11: In-session logging](#x11-in-session-logging)
        - [Wayland: Nested session logging](#wayland-nested-session-logging)
        - [Wayland: VT session logging](#wayland-vt-session-logging)
      - [Logging a Theseus' Ship Wayland session through SSH](#logging-a-theseus-ship-wayland-session-through-ssh)
      - [Troubleshooting full session logging with systemd](#troubleshooting-full-session-logging-with-systemd)
    - [Debugging with GDB](#debugging-with-gdb)
      - [Access backtrace of past crashes](#access-backtrace-of-past-crashes)
      - [Live backtraces](#live-backtraces)
  - [Developing](#developing)
    - [Compiling](#compiling)
      - [Using FDBuild](#using-fdbuild)
      - [Plasma Desktop Session Integration](#plasma-desktop-session-integration)
  - [Submission Guideline](#submission-guideline)
    - [Tooling](#tooling)
  - [Contact](#contact)

## Logging and Debugging
Also note the [respective section](https://github.com/romangg/como/blob/lib-isolate/CONTRIBUTING.md#logging-and-debugging)
in the contriubing guide of the Compositor Modules.
Everything noted there effects Theseus' Ship as well.

### Runtime logging
#### Simple session logging
If you start Theseus' Ship through SDDM as part of a full Plasma session
you find its log output in the systemd journal.

You can retrieve its output specifically with:

    journalctl --user -u plasma-kwin_x11
    journalctl --user -u plasma-kwin_wayland

You can get live updates with the `-f` flag.
Note also that in an X11 session we have the possibility to restart Theseus' Ship.
In this case only the first execution will log to the journal.

#### Live logging in a terminal
##### X11: In-session logging
In an X11 session it is very easy to log Theseus' Ship from a terminal. Just execute the following command
to restart Theseus' Ship:

    kwin_x11 --replace

This is of course not possible in a Wayland session because the session would immediately die with
the Wayland server Theseus' Ship being restarted.

##### Wayland: Nested session logging
Theseus' Ship as a Wayland compositor can be started nested in another Wayland or X11 session
what will print its debug log directly to the terminal emulator it was started from.
For that issue following command from the terminal emulator:

    dbus-run-session kwin_wayland --width=1920 --height=1080 --xwayland --exit-with-session konsole

This will start a nested Theseus' Ship Wayland session with a default output size of 1080p, having
Xwayland enabled and the application Konsole already running in it.
This nested session will also go down automatically when the Konsole window *in the session* is
closed.

You can even start a full nested Plasma session by issuing the following command in the terminal
emulator:

    dbus-run-session startplasma-wayland

##### Wayland: VT session logging
Nested session logging is often not sufficient. The behavior of Theseus' Ship as a Wayland compositor on
real hardware can only be tested when Theseus' Ship is started from a pristine non-graphical state.

To do this switch to a free different virtual terminal (VT) with the key combination `CTRL+ALT+F<x>`
where `<x>` is a number from 1 onward.

For example `CTRL+ALT+F1` is always the SDDM session or an X11 session launched by SDDM
(this session then reuses the Xserver which was internally started by SDDM for SDDM's own graphical
output).
A Wayland session started from SDDM is normally put onto the next free VT
reachable with `CTRL+ALT+F2` since SDDM requires its Xserver to stay active on the first VT .

After we found a free VT and we logged in to it a Wayland Theseus' Ship session can be launched with the
command:

    dbus-run-session kwin_wayland --xwayland --exit-with-session konsole 2>&1 | tee my-theseus-ship-output

This is similar to above command for running it in a nested session but without the parameters
defining the resolution
(instead the best resolution is selected by the hardware driver automatically).

Additionally the error output is redirected to the standard output (by `2>&1`)
and then all output copied with *tee* into the file "my-theseus-ship-output" in the current working
directory.
The log can then be read from this file either after the session ended or live-updating with the
command `tail -f` again.

As above we can start a full Plasma session as well from terminal. For that issue the command:

    dbus-run-session startplasma-wayland 2>&1 | tee my-theseus-ship-output

Again the log output is copied with tee into the file "my-theseus-ship-output" in the current working
directory.

Note that by default when one is running Theseus' Ship through any of the startplasma
methods, it is invoked using a wrapper that will automatically relaunch Theseus' Ship
when it crashes. In order to disable this behavior you can define the
environment variable `KWIN_DISABLE_RELAUNCH`.

    export KWIN_DISABLE_RELAUNCH=1

#### Logging a Theseus' Ship Wayland session through SSH
Starting Theseus' Ship from a free VT as shown above is sufficient for quickly debugging singular issues
but for rapid prototyping it is not enough since it requires a VT switch and later on reading the
debug output from a separate file.
Additionally if the session crashes the VT might be stuck what can potentially even lead to an
unusable device until after a hard reset (reboot via hardware key).

It would be better if we could start a Theseus' Ship session on a VT from a separate device and then seeing
the log live on this secondary device and in case of stuck session kill the session on the VT from
this second device.

With Secure Shell (SSH) this is possible.
For that you need to [create a SSH session][ssh-intro] from your secondary device that connects to
the testing device.
That means your secondary device – where you will watch the log output at – is the *SSH client* and
your testing device – where Theseus' Ship will be executed on – is the *SSH server*.

Once this done on your testing device you need to go to a free VT again and login. You have now
multiple options to launch Theseus' Ship from your secondary device *on this VT* of the testing device:
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

In any case you should be now able to start Theseus' Ship directly in the SSH session with:

    dbus-run-session kwin_wayland --xwayland --exit-with-session konsole

Or as part of a full Plasma session with:

    dbus-run-session startplasma-wayland

This is very similar to starting Theseus' Ship from the VT directly.
The only difference is that we do not redirect the output or copy it with tee to a file
since we can now easily follow it on the screen of our second device.

#### Troubleshooting full session logging with systemd
As described above we can issue `dbus-run-session startplasma-wayland`
to run Theseus' Ship as part of a full Plasma session.
In this case Theseus' Ship is executed as a D-Bus activated systemd service
and its log should be found in the system journal as described [above](#simple-session-logging).

But there is currently the issue that the logs are not found in the journal
when we launch the Plasma session through the `dbus-run-session` command.
This is a problem in the Wayland session as we can't restart Theseus' Ship from within
and has been [reported upstream](https://github.com/systemd/systemd/issues/22242).

But for now a workaround is available for the Wayland session
to still allow retrieving Theseus' Ship's logs.
For that set the environment variable `KWIN_LOG_PATH`
to specify a file where Theseus' Ship's stderr output should be redirected:

    export KWIN_LOG_PATH="$HOME/theseus-ship-wayland.log"
    dbus-run-session startplasma-wayland

### Debugging with GDB
If the Theseus' Ship process crashes the GNU Debugger (GDB) can often provide valuable information
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
when you want to analyse the most recent backtrace generated for Theseus' Ship
and in the secondary command `<pid>` is the PID of one past Theseus' Ship process
in the list you read before with `coredumpctl`
and that you want to analyse now with GDB.

#### Live backtraces
Running gdb on the executing program is often a faster work flow.
A first variant for that is to start one of the `kwin_wayland` commands listed above directly
through gdb like with the command

    dbus-run-session gdb --ex r --args kwin_wayland --xwayland --exit-with-session konsole

with which a gdb-infused Theseus' Ship Wayland session is either started as a nested session or on a VT
through SSH.

It is not recommended to run above command directly from a VT since on a crash you will not be able
to interact with GDB and even without a crash you will not be able to exit the process anymore.

Another option is to attach GDB to an already running Theseus' Ship process with the following command:

    sudo gdb --ex c --pid `pidof kwin_wayland`

Again it is recommended to only do this for a nested session or from a secondary device
since otherwise we would not be able to regain control after a crash or when the process exits.


## Developing

### Compiling
To start writing code for Theseus' Ship first the project needs to be compiled.
You usually want to compile Theseus' Ship from its
[master branch](https://github.com/winft/theseus-ship/commits/master/)
as it reflects the most recent state of development.

#### Using FDBuild
As with the Compositor Modules it is sometimes necessary to compile against KDE libraries
from master branches when building Theseus' Ship from master.
You can use [FDBuild's](https://gitlab.com/kwinft/fdbuild) template mechanism for that:

```
fdbuild --init-with-template kwinft-plasma-meta
```

More information about this can be found in the
[respective section](https://github.com/romangg/como/blob/lib-isolate/CONTRIBUTING.md#using-fdbuild)
of the Compositor Modules' contributing guide.

#### Plasma Desktop Session Integration
With this setup Theseus' Ship can be run already as a standalone binary for example from a VT.
In case you did not install into your `/usr` directory,
as is recommended,
additional steps are required
to run a full Plasma Desktop session together with your self-compiled Theseus' Ship.

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


## Submission Guideline
Code contributions to Theseus' Ship are very welcome but follow a strict process that is laid out in
detail in Wrapland's [contributing document][wrapland-submissions].

*Summarizing the main points:*

* Use [pull requests][pull-requests] directly for smaller contributions, but create
  [issue tickets][issue] *beforehand* for [larger changes][wrapland-large-changes].
* Adhere to the [KDE Frameworks Coding Style][frameworks-style].
* Merge requests have to be posted against master or a feature branch. Commits to the stable branch
  are only cherry-picked from the master branch after some testing on the master branch.

Also make sure to increase the default pipeline timeout to 2h in `Settings > CI/CD > General Pipelines > Timeout`.

### Tooling
See [Wrapland's documentation][wrapland-tooling] for available tooling.

## Contact
See [Wrapland's documentation][wrapland-contact] for contact information.

[arch-core-dump]: https://wiki.archlinux.org/index.php/Core_dump
[conventional-commits]: https://www.conventionalcommits.org/en/v1.0.0/#specification
[frameworks-style]: https://community.kde.org/Policies/Frameworks_Coding_Style
[issue]: https://github.com/winft/theseus-ship/issues
[pull-requests]: https://github.com/winft/theseus-ship/pulls
[plasma-schedule]: https://community.kde.org/Schedules/Plasma_5
[polkit-rule]: https://github.com/swaywm/wlroots/issues/2236#issuecomment-635934081
[wrapland-contact]: https://github.com/winft/wrapland/blob/master/CONTRIBUTING.md#contact
[wrapland-large-changes]: https://github.com/winft/wrapland/blob/master/CONTRIBUTING.md#issues-for-large-changes
[wrapland-submissions]: https://github.com/winft/wrapland/blob/master/CONTRIBUTING.md#submission-guideline
[wrapland-tooling]: https://github.com/winft/wrapland/blob/master/CONTRIBUTING.md#tooling
