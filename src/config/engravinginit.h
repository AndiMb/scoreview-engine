#pragma once

#include <string>

#include "global/io/path.h"

namespace sve {
//! Registers everything the Qt-free conversion path needs in the global IOC
//! and initializes engraving's statics (fonts, SMuFL, default style, staff
//! types, drumset, figured bass) — the moral equivalent of
//! EngravingModule::onInit, trimmed to a converter.
//!
//! resourceRoot is the directory standing in for the qrc ":/" tree
//! (resources/ in this repo). Returns false when a resource fails to load.
bool initEngraving(const std::string& resourceRoot);

//! Open `path` to the engine's file system — that file, or everything under
//! it when it is a directory.
//!
//! Reads are confined to the resource root and whatever is opened here
//! (src/platform/enginefilesystem.h, resolve()). The two callers are the score
//! loader and the wasm addFont(); a path that reaches the engine any other way
//! is one a score named, and those are exactly the ones to keep out.
//!
//! A no-op before initEngraving() has run.
void allowRead(const muse::io::path_t& path);
}
