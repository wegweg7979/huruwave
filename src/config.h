#pragma once

#include <QString>

// huruwave's own settings: ~/.config/huruwave/config.toml, Omarchy-style flat
// `key = value` pairs with optional quotes.
//
// Read-only on purpose. The one setting it holds today is `cookies_from`, and the
// values are credentials-adjacent, so nothing here writes: an earlier version also
// persisted a window-opacity setting, which was removed once it turned out this
// compositor ignores the call (see the README on opacity).
namespace Config {

// ~/.config/huruwave/config.toml
QString path();

// The value for `key`, with surrounding quotes removed and any trailing comment
// dropped. Empty when the file or the key is missing. Never logs the value: this
// same file holds cookie settings.
QString read(const QString &key);

// The same read, against an arbitrary file -- used for cliamp's config, which is
// where a cookie setting is inherited from when this app has none of its own.
QString readFrom(const QString &filePath, const QString &key);

}  // namespace Config
