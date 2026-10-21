#include <gtest/gtest.h>

#include "wikitext/LuaData.h"

using json = nlohmann::json;

TEST(LuaDataTest, LanguageEntries) {
    auto module = LuaData::parseModule(R"lua(
local m = {}
local c = require("Module:languages/data").chars
local p = require("Module:languages/data").puaChars

m["pl"] = {
	"Polish",
	809,
	"zlw-lch",
	"Latn",
	ancestors = "zlw-mpl",
	sort_key = {
		from = {"ą", "ć"},
		to = {"a" .. p[1], "c" .. p[1]}
	},
	standard_chars = "AaĄą" .. c.punc,
}

-- comment with m["xx"] = { "not an entry" }
m["ppa"] = {
	"Pao",
	7132069,
}

m["la-med"] = {
	"Medieval Latin", 1163234, "la", ancestors = {"la-lat", "la-ecc"}, -- trailing comment
}
--[[ block
m["yy"] = {}
]]
return m
)lua");
    ASSERT_EQ(module.size(), 3u);
    const auto &pl = module["pl"];
    EXPECT_EQ(LuaData::positional(pl, 0), "Polish");
    EXPECT_EQ(LuaData::positional(pl, 1), 809);
    EXPECT_EQ(LuaData::positional(pl, 2), "zlw-lch");
    EXPECT_EQ(LuaData::positional(pl, 3), "Latn");
    EXPECT_EQ(pl["ancestors"], "zlw-mpl");
    EXPECT_TRUE(pl["standard_chars"].contains("expr"));
    EXPECT_EQ(pl["sort_key"]["from"]["#"].size(), 2u);
    EXPECT_TRUE(LuaData::positional(module["ppa"], 2).is_null());
    EXPECT_EQ(module["la-med"]["ancestors"]["#"], (json{"la-lat", "la-ecc"}));
}

TEST(LuaDataTest, ExtraEntriesAndValues) {
    auto module = LuaData::parseModule(R"lua(
m["ps"] = {
	aliases = {"Pashtun", "Pushto"},
	varieties = {"Central Pashto", {"Pukhto", "Pakhto"}},
}
m["rm"] = {
	["key with space"] = 'single \'quoted\'',
	flag = true, other = nil, n = -1.5,
	text = [[long
string]],
}
)lua");
    EXPECT_EQ(module["ps"]["aliases"]["#"], (json{"Pashtun", "Pushto"}));
    EXPECT_EQ(module["ps"]["varieties"]["#"][1]["#"], (json{"Pukhto", "Pakhto"}));
    EXPECT_EQ(module["rm"]["key with space"], "single 'quoted'");
    EXPECT_EQ(module["rm"]["flag"], true);
    EXPECT_TRUE(module["rm"]["other"].is_null());
    EXPECT_DOUBLE_EQ(module["rm"]["n"].get<double>(), -1.5);
    EXPECT_EQ(module["rm"]["text"], "long\nstring");
}

TEST(LuaDataTest, CallWithTableArgument) {
    auto module = LuaData::parseModule(R"lua(
m["Latn"] = process_ranges{
	"Latin", 8229, "alphabet",
	aliases = {"Roman"},
	ranges = { 0x0041, 0x005A },
}
m["Cyrl"] = util.process_ranges {
	"Cyrillic", 8209,
}
)lua");
    ASSERT_EQ(module.size(), 2u);
    EXPECT_EQ(LuaData::positional(module["Latn"], 2), "alphabet");
    EXPECT_EQ(module["Latn"]["ranges"]["#"][0], 0x41);
    EXPECT_EQ(LuaData::positional(module["Cyrl"], 1), 8209);
}
