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
This macro will take the embedded json metadata filename as the second argument.

### Buildsystem
To build the effect, you can use the `kcoreaddons_add_plugin` CMake macro which
takes care of creating the library and installing it.
The first parameter is the name of the library, this is the same as the id of the plugin.
If our effect's source is in `cooleffect.cpp`, we'd use following:
```
kcoreaddons_add_plugin(cooleffect SOURCES cooleffect.cpp INSTALL_NAMESPACE "kwin/effects/plugins")
```

### Effect's .json file for embedded metadata
The format follows the one of the KPluginMetaData class.
Example `cooleffect.json` file:
```
{
    "KPlugin": {
        "Authors": [
            {
                "Email": "my@email.here",
                "Name": "My Name"
            }
        ],
        "Category": "Misc",
        "Description": "The coolest effect you've ever seen",
        "Icon": "preferences-system-windows-effect-cooleffect",
        "Name": "Cool Effect"
    }
}
```

## Accessing windows and workspace
Effects can gain access to the properties of windows and workspace via
EffectWindow and EffectsHandler classes.

There is one global EffectsHandler object which you can access using the `effects` pointer.
For each window, there is an EffectWindow object which can be used to read
window properties such as position and also to change them.

For more information about this, see the documentation of the corresponding classes.
