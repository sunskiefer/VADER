/* VADER desktop: where the shapes and "User Presets" live, and the example shapes of a first run.
 *   Windows  %APPDATA%\VADER\Shapes        Linux  ~/.local/share/VADER/Shapes   (XDG_DATA_HOME honoured)
 * The folder is named VADER, never the brand with its apostrophe: an apostrophe in a path breaks shell scripts, installers
 * and some hosts' file dialogs. The path is handed to the engine with forward slashes (the engine splits on '/'), and on
 * Windows as its short 8.3 form when that exists, so a user name with accents still opens with the engine's plain fopen. */
#pragma once
#include <string>

std::string vaderShapesDir();          /* creates it (and the example shapes) when missing; "" when it cannot */
