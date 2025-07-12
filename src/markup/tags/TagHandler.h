#pragma once
#include "Tag.h"

struct TagHandler {
    virtual ~TagHandler() = default;
    [[nodiscard]] virtual std::string name() const = 0;
    [[nodiscard]] virtual std::string process(const Tag& tag, const std::string& content) const {
        return content;
    }
};

struct NowikiHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "nowiki"; }
};

struct AbbrHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "abbr"; }
};

struct BHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "b"; }
};

struct BdiHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "bdi"; }
};

struct BdoHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "bdo"; }
};

struct BigHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "big"; }
};

struct BlockquoteHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "blockquote"; }
};

struct BrHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "br"; }
    [[nodiscard]] std::string process(const Tag& tag, const std::string& content) const override{
        return content + "\n";
    }
};

struct CaptionHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "caption"; }
};

struct CenterHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "center"; }
};

struct CiteHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "cite"; }
};

struct CodeHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "code"; }
};

struct ColHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "col"; }
};

struct ColgroupHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "colgroup"; }
};

struct DataHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "data"; }
};

struct DdHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "dd"; }
};

struct DelHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "del"; }
};

struct DfnHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "dfn"; }
};

struct DivHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "div"; }
};

struct DlHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "dl"; }
};

struct DtHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "dt"; }
};

struct EmHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "em"; }
};

struct FontHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "font"; }
};

struct H1Handler : TagHandler {
    [[nodiscard]] std::string name() const override { return "h1"; }
};

struct H2Handler : TagHandler {
    [[nodiscard]] std::string name() const override { return "h2"; }
};

struct H3Handler : TagHandler {
    [[nodiscard]] std::string name() const override { return "h3"; }
};

struct H4Handler : TagHandler {
    [[nodiscard]] std::string name() const override { return "h4"; }
};

struct H5Handler : TagHandler {
    [[nodiscard]] std::string name() const override { return "h5"; }
};

struct H6Handler : TagHandler {
    [[nodiscard]] std::string name() const override { return "h6"; }
};

struct HrHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "hr"; }
};

struct IHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "i"; }
};

struct InsHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "ins"; }
};

struct KbdHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "kbd"; }
};

struct LiHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "li"; }
};

struct LinkHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "link"; }
};

struct MarkHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "mark"; }
};

struct MetaHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "meta"; }
};

struct OlHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "ol"; }
};

struct PHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "p"; }
};

struct PreHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "pre"; }
};

struct QHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "q"; }
};

struct RbHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "rb"; }
};

struct RpHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "rp"; }
};

struct RtHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "rt"; }
};

struct RubyHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "ruby"; }
};

struct SHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "s"; }
};

struct SampHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "samp"; }
};

struct SmallHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "small"; }
};

struct SpanHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "span"; }
};

struct StrikeHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "strike"; }
};

struct StrongHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "strong"; }
};

struct SubHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "sub"; }
};

struct SupHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "sup"; }
};

struct TableHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "table"; }
};

struct TdHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "td"; }
};

struct ThHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "th"; }
};

struct TimeHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "time"; }
};

struct TrHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "tr"; }
};

struct TtHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "tt"; }
};

struct UHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "u"; }
};

struct UlHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "ul"; }
};

struct VarHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "var"; }
};

struct WbrHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "wbr"; }
};
