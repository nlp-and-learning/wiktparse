#pragma once
#include <map>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

// Reads data modules of Wiktionary written as Lua tables:
//   m["pl"] = { "Polish", 809, "zlw-lch", "Latn", ancestors = "zlw-mpl", ... }
// Values become JSON: strings, numbers, booleans, null (nil); a table becomes an
// object with positional values in "#" (array) and named fields as keys.
// A call with a table argument (process_ranges{...}) gives the table. Other
// expressions (calls, "a" .. b concatenations) become {"expr": "<source text>"}.
namespace LuaData {

// All m["code"] = <value> assignments of a module, by code
std::map<std::string, nlohmann::json> parseModule(std::string_view source);

// Parses one Lua value (for tests)
nlohmann::json parseValue(std::string_view source);

// Positional value i of a parsed table, or null
const nlohmann::json &positional(const nlohmann::json &table, size_t i);

} // namespace LuaData
