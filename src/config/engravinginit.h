#pragma once

#include <string>

namespace sve {
//! Registers everything the Qt-free conversion path needs in the global IOC
//! and initializes engraving's statics (fonts, SMuFL, default style, staff
//! types, drumset, figured bass) — the moral equivalent of
//! EngravingModule::onInit, trimmed to a converter.
//!
//! resourceRoot is the directory standing in for the qrc ":/" tree
//! (resources/ in this repo). Returns false when a resource fails to load.
bool initEngraving(const std::string& resourceRoot);
}
