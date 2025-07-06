#pragma once
#include "Tag.h"

struct TagHandler {
    virtual ~TagHandler() = default;
    [[nodiscard]] virtual std::string name() const = 0;
    virtual void handle(const Tag& tag) const = 0;
};

struct NowikiHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "nowiki"; }
    void handle(const Tag& tag) const override {}
};

struct AbbrHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "abbr"; }
    void handle(const Tag& tag) const override {}
};

struct BHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "b"; }
    void handle(const Tag& tag) const override {}
};

struct BdiHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "bdi"; }
    void handle(const Tag& tag) const override {}
};

struct BdoHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "bdo"; }
    void handle(const Tag& tag) const override {}
};

struct BigHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "big"; }
    void handle(const Tag& tag) const override {}
};

struct BlockquoteHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "blockquote"; }
    void handle(const Tag& tag) const override {}
};

struct BrHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "br"; }
    void handle(const Tag& tag) const override {}
};

struct CaptionHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "caption"; }
    void handle(const Tag& tag) const override {}
};

struct CenterHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "center"; }
    void handle(const Tag& tag) const override {}
};

struct CiteHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "cite"; }
    void handle(const Tag& tag) const override {}
};

struct CodeHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "code"; }
    void handle(const Tag& tag) const override {}
};

struct ColHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "col"; }
    void handle(const Tag& tag) const override {}
};

struct ColgroupHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "colgroup"; }
    void handle(const Tag& tag) const override {}
};

struct DataHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "data"; }
    void handle(const Tag& tag) const override {}
};

struct DdHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "dd"; }
    void handle(const Tag& tag) const override {}
};

struct DelHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "del"; }
    void handle(const Tag& tag) const override {}
};

struct DfnHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "dfn"; }
    void handle(const Tag& tag) const override {}
};

struct DivHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "div"; }
    void handle(const Tag& tag) const override {}
};

struct DlHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "dl"; }
    void handle(const Tag& tag) const override {}
};

struct DtHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "dt"; }
    void handle(const Tag& tag) const override {}
};

struct EmHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "em"; }
    void handle(const Tag& tag) const override {}
};

struct FontHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "font"; }
    void handle(const Tag& tag) const override {}
};

struct H1Handler : TagHandler {
    [[nodiscard]] std::string name() const override { return "h1"; }
    void handle(const Tag& tag) const override {}
};

struct H2Handler : TagHandler {
    [[nodiscard]] std::string name() const override { return "h2"; }
    void handle(const Tag& tag) const override {}
};

struct H3Handler : TagHandler {
    [[nodiscard]] std::string name() const override { return "h3"; }
    void handle(const Tag& tag) const override {}
};

struct H4Handler : TagHandler {
    [[nodiscard]] std::string name() const override { return "h4"; }
    void handle(const Tag& tag) const override {}
};

struct H5Handler : TagHandler {
    [[nodiscard]] std::string name() const override { return "h5"; }
    void handle(const Tag& tag) const override {}
};

struct H6Handler : TagHandler {
    [[nodiscard]] std::string name() const override { return "h6"; }
    void handle(const Tag& tag) const override {}
};

struct HrHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "hr"; }
    void handle(const Tag& tag) const override {}
};

struct IHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "i"; }
    void handle(const Tag& tag) const override {}
};

struct InsHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "ins"; }
    void handle(const Tag& tag) const override {}
};

struct KbdHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "kbd"; }
    void handle(const Tag& tag) const override {}
};

struct LiHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "li"; }
    void handle(const Tag& tag) const override {}
};

struct LinkHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "link"; }
    void handle(const Tag& tag) const override {}
};

struct MarkHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "mark"; }
    void handle(const Tag& tag) const override {}
};

struct MetaHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "meta"; }
    void handle(const Tag& tag) const override {}
};

struct OlHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "ol"; }
    void handle(const Tag& tag) const override {}
};

struct PHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "p"; }
    void handle(const Tag& tag) const override {}
};

struct PreHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "pre"; }
    void handle(const Tag& tag) const override {}
};

struct QHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "q"; }
    void handle(const Tag& tag) const override {}
};

struct RbHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "rb"; }
    void handle(const Tag& tag) const override {}
};

struct RpHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "rp"; }
    void handle(const Tag& tag) const override {}
};

struct RtHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "rt"; }
    void handle(const Tag& tag) const override {}
};

struct RubyHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "ruby"; }
    void handle(const Tag& tag) const override {}
};

struct SHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "s"; }
    void handle(const Tag& tag) const override {}
};

struct SampHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "samp"; }
    void handle(const Tag& tag) const override {}
};

struct SmallHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "small"; }
    void handle(const Tag& tag) const override {}
};

struct SpanHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "span"; }
    void handle(const Tag& tag) const override {}
};

struct StrikeHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "strike"; }
    void handle(const Tag& tag) const override {}
};

struct StrongHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "strong"; }
    void handle(const Tag& tag) const override {}
};

struct SubHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "sub"; }
    void handle(const Tag& tag) const override {}
};

struct SupHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "sup"; }
    void handle(const Tag& tag) const override {}
};

struct TableHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "table"; }
    void handle(const Tag& tag) const override {}
};

struct TdHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "td"; }
    void handle(const Tag& tag) const override {}
};

struct ThHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "th"; }
    void handle(const Tag& tag) const override {}
};

struct TimeHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "time"; }
    void handle(const Tag& tag) const override {}
};

struct TrHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "tr"; }
    void handle(const Tag& tag) const override {}
};

struct TtHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "tt"; }
    void handle(const Tag& tag) const override {}
};

struct UHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "u"; }
    void handle(const Tag& tag) const override {}
};

struct UlHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "ul"; }
    void handle(const Tag& tag) const override {}
};

struct VarHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "var"; }
    void handle(const Tag& tag) const override {}
};

struct WbrHandler : TagHandler {
    [[nodiscard]] std::string name() const override { return "wbr"; }
    void handle(const Tag& tag) const override {}
};

