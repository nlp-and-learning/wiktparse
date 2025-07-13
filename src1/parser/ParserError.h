// parser/ParserError.h
#ifndef PARSER_ERROR_H
#define PARSER_ERROR_H

#include <stdexcept>
#include <string>

namespace parser {

    class ParserError : public std::runtime_error {
    public:
        // Constructor
        explicit ParserError(const std::string& message);
    };

}  // namespace parser

#endif  // PARSER_ERROR_H