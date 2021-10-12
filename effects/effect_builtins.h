/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2014 Martin Gräßlin <mgraesslin@kde.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/
#ifndef KWIN_EFFECT_BUILTINS_H
#define KWIN_EFFECT_BUILTINS_H

#define KWIN_IMPORT_BUILTIN_EFFECTS                                                                \
    Q_IMPORT_PLUGIN(BlurEffectFactory)                                                             \
    Q_IMPORT_PLUGIN(ColorPickerEffectFactory)                                                      \
    Q_IMPORT_PLUGIN(ContrastEffectFactory)                                                         \
    Q_IMPORT_PLUGIN(CoverSwitchEffectFactory)                                                      \
    Q_IMPORT_PLUGIN(CubeEffectFactory)                                                             \
    Q_IMPORT_PLUGIN(CubeSlideEffectFactory)                                                        \
    Q_IMPORT_PLUGIN(DesktopGridEffectFactory)                                                      \
    Q_IMPORT_PLUGIN(DimInactiveEffectFactory)                                                      \
    Q_IMPORT_PLUGIN(FallApartEffectFactory)                                                        \
    Q_IMPORT_PLUGIN(FlipSwitchEffectFactory)                                                       \
    Q_IMPORT_PLUGIN(GlideEffectFactory)                                                            \
    Q_IMPORT_PLUGIN(HighlightWindowEffectFactory)                                                  \
    Q_IMPORT_PLUGIN(InvertEffectFactory)                                                           \
    Q_IMPORT_PLUGIN(KscreenEffectFactory)                                                          \
    Q_IMPORT_PLUGIN(LookingGlassEffectFactory)                                                     \
    Q_IMPORT_PLUGIN(MagicLampEffectFactory)                                                        \
    Q_IMPORT_PLUGIN(MagnifierEffectFactory)                                                        \
    Q_IMPORT_PLUGIN(MouseClickEffectFactory)                                                       \
    Q_IMPORT_PLUGIN(MouseMarkEffectFactory)                                                        \
    Q_IMPORT_PLUGIN(PresentWindowsEffectFactory)                                                   \
    Q_IMPORT_PLUGIN(ResizeEffectFactory)                                                           \
    Q_IMPORT_PLUGIN(ScreenEdgeEffectFactory)                                                       \
    Q_IMPORT_PLUGIN(ScreenShotEffectFactory)                                                       \
    Q_IMPORT_PLUGIN(SheetEffectFactory)                                                            \
    Q_IMPORT_PLUGIN(ShowFpsEffectFactory)                                                          \
    Q_IMPORT_PLUGIN(ShowPaintEffectFactory)                                                        \
    Q_IMPORT_PLUGIN(SlideEffectFactory)                                                            \
    Q_IMPORT_PLUGIN(SlideBackEffectFactory)                                                        \
    Q_IMPORT_PLUGIN(SlidingPopupsEffectFactory)                                                    \
    Q_IMPORT_PLUGIN(SnapHelperEffectFactory)                                                       \
    Q_IMPORT_PLUGIN(StartupFeedbackEffectFactory)                                                  \
    Q_IMPORT_PLUGIN(ThumbnailAsideEffectFactory)                                                   \
    Q_IMPORT_PLUGIN(TouchPointsEffectFactory)                                                      \
    Q_IMPORT_PLUGIN(TrackMouseEffectFactory)                                                       \
    Q_IMPORT_PLUGIN(WindowGeometryFactory)                                                         \
    Q_IMPORT_PLUGIN(WobblyWindowsEffectFactory)                                                    \
    Q_IMPORT_PLUGIN(ZoomEffectFactory)

#endif
