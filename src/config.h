#pragma once

#include "hotkey.h"

// Reads RobloxShadeHost.ini beside the exe, creating it with defaults on first run.
// Shows an error and throws when the hotkey cannot be parsed.
Hotkey LoadInputHotkey();
