#pragma once
#include <functional>
#include <unordered_map>
#include <memory>
#include "TagHandler.h"

class TagFactory {
public:
    using Creator = std::function<std::unique_ptr<TagHandler>()>;

    static TagFactory& instance() {
        static TagFactory inst; // lazy-init, thread-safe in C++11+
        return inst;
    }

    bool has(const std::string& tagName) const {
        return map.contains(tagName);
    }

    std::unique_ptr<TagHandler> create(const std::string& tagName) const {
        auto it = map.find(tagName);
        if (it != map.end())
            return it->second();
        return nullptr;
    }

private:
    TagFactory() {
        map = {
            { "nowiki", [] { return std::make_unique<NowikiHandler>(); } },
            { "abbr", [] { return std::make_unique<AbbrHandler>(); } },
            { "b", [] { return std::make_unique<BHandler>(); } },
            { "bdi", [] { return std::make_unique<BdiHandler>(); } },
            { "bdo", [] { return std::make_unique<BdoHandler>(); } },
            { "big", [] { return std::make_unique<BigHandler>(); } },
            { "blockquote", [] { return std::make_unique<BlockquoteHandler>(); } },
            { "br", [] { return std::make_unique<BrHandler>(); } },
            { "caption", [] { return std::make_unique<CaptionHandler>(); } },
            { "center", [] { return std::make_unique<CenterHandler>(); } },
            { "cite", [] { return std::make_unique<CiteHandler>(); } },
            { "code", [] { return std::make_unique<CodeHandler>(); } },
            { "col", [] { return std::make_unique<ColHandler>(); } },
            { "colgroup", [] { return std::make_unique<ColgroupHandler>(); } },
            { "data", [] { return std::make_unique<DataHandler>(); } },
            { "dd", [] { return std::make_unique<DdHandler>(); } },
            { "del", [] { return std::make_unique<DelHandler>(); } },
            { "dfn", [] { return std::make_unique<DfnHandler>(); } },
            { "div", [] { return std::make_unique<DivHandler>(); } },
            { "dl", [] { return std::make_unique<DlHandler>(); } },
            { "dt", [] { return std::make_unique<DtHandler>(); } },
            { "em", [] { return std::make_unique<EmHandler>(); } },
            { "font", [] { return std::make_unique<FontHandler>(); } },
            { "h1", [] { return std::make_unique<H1Handler>(); } },
            { "h2", [] { return std::make_unique<H2Handler>(); } },
            { "h3", [] { return std::make_unique<H3Handler>(); } },
            { "h4", [] { return std::make_unique<H4Handler>(); } },
            { "h5", [] { return std::make_unique<H5Handler>(); } },
            { "h6", [] { return std::make_unique<H6Handler>(); } },
            { "hr", [] { return std::make_unique<HrHandler>(); } },
            { "i", [] { return std::make_unique<IHandler>(); } },
            { "ins", [] { return std::make_unique<InsHandler>(); } },
            { "kbd", [] { return std::make_unique<KbdHandler>(); } },
            { "li", [] { return std::make_unique<LiHandler>(); } },
            { "link", [] { return std::make_unique<LinkHandler>(); } },
            { "mark", [] { return std::make_unique<MarkHandler>(); } },
            { "meta", [] { return std::make_unique<MetaHandler>(); } },
            { "ol", [] { return std::make_unique<OlHandler>(); } },
            { "p", [] { return std::make_unique<PHandler>(); } },
            { "pre", [] { return std::make_unique<PreHandler>(); } },
            { "q", [] { return std::make_unique<QHandler>(); } },
            { "rb", [] { return std::make_unique<RbHandler>(); } },
            { "rp", [] { return std::make_unique<RpHandler>(); } },
            { "rt", [] { return std::make_unique<RtHandler>(); } },
            { "ruby", [] { return std::make_unique<RubyHandler>(); } },
            { "s", [] { return std::make_unique<SHandler>(); } },
            { "samp", [] { return std::make_unique<SampHandler>(); } },
            { "small", [] { return std::make_unique<SmallHandler>(); } },
            { "span", [] { return std::make_unique<SpanHandler>(); } },
            { "strike", [] { return std::make_unique<StrikeHandler>(); } },
            { "strong", [] { return std::make_unique<StrongHandler>(); } },
            { "sub", [] { return std::make_unique<SubHandler>(); } },
            { "sup", [] { return std::make_unique<SupHandler>(); } },
            { "table", [] { return std::make_unique<TableHandler>(); } },
            { "td", [] { return std::make_unique<TdHandler>(); } },
            { "th", [] { return std::make_unique<ThHandler>(); } },
            { "time", [] { return std::make_unique<TimeHandler>(); } },
            { "tr", [] { return std::make_unique<TrHandler>(); } },
            { "tt", [] { return std::make_unique<TtHandler>(); } },
            { "u", [] { return std::make_unique<UHandler>(); } },
            { "ul", [] { return std::make_unique<UlHandler>(); } },
            { "var", [] { return std::make_unique<VarHandler>(); } },
            { "wbr", [] { return std::make_unique<WbrHandler>(); } },
        };
    }

    std::unordered_map<std::string, Creator> map;
};
