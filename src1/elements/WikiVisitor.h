// elements/WikiVisitor.h
#ifndef WIKI_VISITOR_H
#define WIKI_VISITOR_H

namespace parser::elements {

    // Forward declarations for all WikiElement classes
    class WikiElement;
    class TextElement;
    class Tag;
    class TaggedContent;
    class Header;
    class Template;
    class WikiLink;
    class ExternalLink;

    class WikiVisitor {
    public:
        virtual ~WikiVisitor() = default;

        // visit methods for each derived class
        virtual void visit(const WikiElement& element) = 0;
        virtual void visit(const TextElement& fragment) = 0;
        virtual void visit(const Tag& tag) = 0;
        virtual void visit(const TaggedContent& content) = 0;
        virtual void visit(const Header& header) = 0;
        virtual void visit(const Template& templ) = 0;
        virtual void visit(const WikiLink& link) = 0;
        virtual void visit(const ExternalLink& ext_link) = 0;
    };

}  // namespace parser::elements

#endif  // WIKI_VISITOR_H