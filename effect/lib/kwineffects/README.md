# Effects library 

Contains necessary classes for creating new KWin compositing effects.

## Creating new effects
This example will demonstrate the basics of creating an effect. We'll use
CoolEffect as the class name, cooleffect as internal name and
"Cool Effect" as user-visible name of the effect.

This example doesn't demonstrate how to write the effect's code. For that,
see the documentation of the Effect class.

### CoolEffect class
First you need to create CoolEffect class which has to be a subclass of
`KWin::Effect`. In that class you can reimplement various virtual
methods to control how and where the windows are drawn.

### `KWIN_EFFECT_FACTOR` macro
This library provides a specialized KPluginFactory subclass and macros to
create a sub class. This subclass of KPluginFactory has to be used, otherwise
KWin won't load the plugin. Use the `KWIN_EFFECT_FACTORY` macro to create the
plugin factory.

### Buildsystem
To build the effect, you can use the KWIN_ADD_EFFECT() cmake macro which
can be found in effects/CMakeLists.txt file in KWin's source. First
argument of the macro is the name of the library that will contain
your effect. Although not strictly required, it is usually a good idea to
use the same name as your effect's internal name there. Following arguments
to the macro are the files containing your effect's source. If our effect's
source is in cooleffect.cpp, we'd use following:

```
 KWIN_ADD_EFFECT(cooleffect cooleffect.cpp)
```

This macro takes care of compiling your effect. You'll also need to install
 your effect's .desktop file, so the example CMakeLists.txt file would be
 as follows:
```
 KWIN_ADD_EFFECT(cooleffect cooleffect.cpp)
 install( FILES cooleffect.desktop DESTINATION ${SERVICES_INSTALL_DIR}/kwin )
```

### Effect's .desktop file
You will also need to create .desktop file to set name, description, icon
and other properties of your effect. Important fields of the .desktop file
are:
* *Name*, user-visible name of your effect
* *Icon*, name of the icon of the effect
* *Comment*, short description of the effect
* *Type*, must be "Service"
* *X-KDE-ServiceTypes*, must be "KWin/Effect" for scripted effects
* *X-KDE-PluginInfo-Name*, effect's internal name as passed to the `KWIN_EFFECT` macro plus
  `kwin4_effect_` prefix
* *X-KDE-PluginInfo-Category*, effect's category. Should be one of Appearance, Accessibility,
  Window Management, Demos, Tests, Misc
* *X-KDE-PluginInfo-EnabledByDefault*, whether the effect should be enabled by default (use
  sparingly). Default is false
* *X-KDE-Library*, name of the library containing the effect. This is the first argument passed
  to the KWIN_ADD_EFFECT macro in cmake file plus "kwin4_effect_" prefix.

Example `cooleffect.desktop` file follows:
```
[Desktop Entry]
Name=Cool Effect
Comment=The coolest effect you've ever seen
Icon=preferences-system-windows-effect-cooleffect

Type=Service
X-KDE-ServiceTypes=KWin/Effect
X-KDE-PluginInfo-Author=My Name
X-KDE-PluginInfo-Email=my@email.here
X-KDE-PluginInfo-Name=kwin4_effect_cooleffect
X-KDE-PluginInfo-Category=Misc
X-KDE-Library=kwin4_effect_cooleffect
```

## Accessing windows and workspace
Effects can gain access to the properties of windows and workspace via
EffectWindow and EffectsHandler classes.

There is one global EffectsHandler object which you can access using the `effects` pointer.
For each window, there is an EffectWindow object which can be used to read
window properties such as position and also to change them.

For more information about this, see the documentation of the corresponding classes.
