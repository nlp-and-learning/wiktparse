// parser/ParserError.cpp
#include "ParserError.h"

namespace parser {

    // Constructor
    ParserError::ParserError(const std::string& message) : std::runtime_error(message) {}

}  // namespace parser