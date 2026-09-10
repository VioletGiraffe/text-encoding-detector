@echo off
SetLocal EnableDelayedExpansion
(set PATH=E:\Development\Tools\Qt\6.9.3\msvc2022_64\bin;!PATH!)
if defined QT_PLUGIN_PATH (
    set QT_PLUGIN_PATH=E:\Development\Tools\Qt\6.9.3\msvc2022_64\plugins;!QT_PLUGIN_PATH!
) else (
    set QT_PLUGIN_PATH=E:\Development\Tools\Qt\6.9.3\msvc2022_64\plugins
)
%*
EndLocal
