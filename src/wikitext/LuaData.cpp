#include "LuaData.h"

#include <cctype>
#include <stdexcept>

using json = nlohmann::json;

namespace {

class Parser {
    std::string_view s;
    size_t pos = 0;

    bool at(std::string_view t) const { return s.compare(pos, t.size(), t) == 0; }

    // Long bracket [[...]] or [==[...]==] starting at pos; returns level or -1
    int longBracketLevel(size_t p) const {
        if (p >= s.size() || s[p] != '[')
            return -1;
        size_t q = p + 1;
        int level = 0;
        while (q < s.size() && s[q] == '=') {
            level++;
            q++;
        }
        return q < s.size() && s[q] == '[' ? level : -1;
    }

    std::string readLongString(int level) {
        size_t open = pos + 2 + level;
        std::string close = "]" + std::string(level, '=') + "]";
        size_t end = s.find(close, open);
        if (end == std::string_view::npos)
            end = s.size();
        std::string text(s.substr(open, end - open));
        if (!text.empty() && text[0] == '\n') // first newline is skipped
            text.erase(0, 1);
        pos = std::min(s.size(), end + close.size());
        return text;
    }

public:
    explicit Parser(std::string_view source, size_t start = 0) : s(source), pos(start) {}
    size_t position() const { return pos; }
    bool done() const { return pos >= s.size(); }

    void skipSpace() {
        while (pos < s.size()) {
            if (std::isspace(static_cast<unsigned char>(s[pos]))) {
                pos++;
            } else if (at("--")) {
                int level = longBracketLevel(pos + 2);
                if (level >= 0) {
                    pos += 2;
                    readLongString(level);
                } else {
                    auto end = s.find('\n', pos);
                    pos = end == std::string_view::npos ? s.size() : end + 1;
                }
            } else
                break;
        }
    }

    std::string readQuoted() {
        char quote = s[pos++];
        std::string out;
        while (pos < s.size() && s[pos] != quote) {
            char c = s[pos++];
            if (c == '\\' && pos < s.size()) {
                char e = s[pos++];
                switch (e) {
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'r': out += '\r'; break;
                    case '\n': out += '\n'; break;
                    default:
                        if (std::isdigit(static_cast<unsigned char>(e))) { // \ddd decimal byte
                            int v = e - '0';
                            for (int k = 0; k < 2 && pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos])); k++)
                                v = v * 10 + (s[pos++] - '0');
                            out += static_cast<char>(v);
                        } else
                            out += e;
                }
            } else
                out += c;
        }
        pos++; // closing quote
        return out;
    }

    // Skips an expression up to ',' ';' or '}' at depth 0; returns its text
    std::string skipExpression(size_t from) {
        int depth = 0;
        while (pos < s.size()) {
            skipSpace();
            if (pos >= s.size())
                break;
            char c = s[pos];
            if (depth == 0 && (c == ',' || c == ';' || c == '}'))
                break;
            if (c == '"' || c == '\'') {
                readQuoted();
                continue;
            }
            if (c == '[' && longBracketLevel(pos) >= 0) {
                readLongString(longBracketLevel(pos));
                continue;
            }
            if (c == '{' || c == '(' || c == '[')
                depth++;
            else if (c == '}' || c == ')' || c == ']')
                depth--;
            pos++;
        }
        std::string text(s.substr(from, pos - from));
        while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
            text.pop_back();
        return text;
    }

    static bool isIdentStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
    static bool isIdent(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

    json parseTable() {
        pos++; // '{'
        json table = json::object();
        json positional = json::array();
        while (true) {
            skipSpace();
            if (pos >= s.size())
                break;
            if (s[pos] == '}') {
                pos++;
                break;
            }
            if (s[pos] == ',' || s[pos] == ';') {
                pos++;
                continue;
            }
            // [key] = value
            if (s[pos] == '[' && longBracketLevel(pos) < 0) {
                pos++;
                skipSpace();
                json key = value();
                skipSpace();
                if (pos < s.size() && s[pos] == ']')
                    pos++;
                skipSpace();
                if (pos < s.size() && s[pos] == '=')
                    pos++;
                json v = value();
                table[key.is_string() ? key.get<std::string>() : key.dump()] = std::move(v);
                continue;
            }
            // name = value (not ==)
            if (isIdentStart(s[pos])) {
                size_t start = pos, end = pos;
                while (end < s.size() && isIdent(s[end]))
                    end++;
                size_t after = end;
                while (after < s.size() && (s[after] == ' ' || s[after] == '\t'))
                    after++;
                if (after < s.size() && s[after] == '=' && (after + 1 >= s.size() || s[after + 1] != '=')) {
                    std::string name(s.substr(start, end - start));
                    pos = after + 1;
                    table[name] = value();
                    continue;
                }
            }
            positional.push_back(value());
        }
        if (!positional.empty())
            table["#"] = std::move(positional);
        return table;
    }

    json value() {
        skipSpace();
        if (pos >= s.size())
            return nullptr;
        size_t start = pos;
        json v;
        char c = s[pos];
        if (c == '"' || c == '\'')
            v = readQuoted();
        else if (c == '[' && longBracketLevel(pos) >= 0)
            v = readLongString(longBracketLevel(pos));
        else if (c == '{')
            v = parseTable();
        else if (std::isdigit(static_cast<unsigned char>(c)) ||
                 (c == '-' && pos + 1 < s.size() && std::isdigit(static_cast<unsigned char>(s[pos + 1])))) {
            size_t end = pos + 1;
            while (end < s.size() && (std::isalnum(static_cast<unsigned char>(s[end])) || s[end] == '.'))
                end++;
            std::string number(s.substr(pos, end - pos));
            pos = end;
            try {
                v = number.find_first_of(".eE") == std::string::npos ? json(std::stoll(number, nullptr, 0))
                                                                       : json(std::stod(number));
            } catch (const std::exception &) {
                v = json{{"expr", number}};
            }
        } else if (at("nil") && (pos + 3 >= s.size() || !isIdent(s[pos + 3]))) {
            pos += 3;
            v = nullptr;
        } else if (at("true") && (pos + 4 >= s.size() || !isIdent(s[pos + 4]))) {
            pos += 4;
            v = true;
        } else if (at("false") && (pos + 5 >= s.size() || !isIdent(s[pos + 5]))) {
            pos += 5;
            v = false;
        } else if (isIdentStart(c)) {
            // Call with a table argument: process_ranges{ ... } gives the table
            size_t p = pos;
            while (p < s.size() && (isIdent(s[p]) || s[p] == '.'))
                p++;
            while (p < s.size() && (s[p] == ' ' || s[p] == '\t'))
                p++;
            if (p < s.size() && s[p] == '{') {
                pos = p;
                v = parseTable();
            } else
                return json{{"expr", skipExpression(start)}};
        } else
            return json{{"expr", skipExpression(start)}};
        // A value continued by an operator ("a" .. c.punc) is an expression;
        // anything else (',', '}', next statement) ends the value
        size_t afterValue = pos;
        skipSpace();
        static const std::string operators = "+-*/%^<>=~([.:";
        if (pos < s.size() && operators.find(s[pos]) != std::string::npos && !(s[pos] == '=' && !at("==")))
            return json{{"expr", skipExpression(start)}};
        pos = afterValue;
        return v;
    }

    // Moves to the next m["code"] = assignment; returns the code or empty at end
    std::string nextAssignment() {
        while (pos < s.size()) {
            skipSpace();
            if (pos >= s.size())
                break;
            if (s[pos] == '"' || s[pos] == '\'') {
                readQuoted();
                continue;
            }
            if (s[pos] == '[' && longBracketLevel(pos) >= 0) {
                readLongString(longBracketLevel(pos));
                continue;
            }
            if (at("m[") && (pos == 0 || !isIdent(s[pos - 1]))) {
                Parser key(s, pos + 2);
                key.skipSpace();
                if (key.pos < s.size() && (s[key.pos] == '"' || s[key.pos] == '\'')) {
                    std::string code = key.readQuoted();
                    key.skipSpace();
                    if (key.pos < s.size() && s[key.pos] == ']') {
                        key.pos++;
                        key.skipSpace();
                        if (key.pos < s.size() && s[key.pos] == '=' && (key.pos + 1 >= s.size() || s[key.pos + 1] != '=')) {
                            pos = key.pos + 1;
                            return code;
                        }
                    }
                }
            }
            pos++;
        }
        return "";
    }
};

} // namespace

namespace LuaData {

std::map<std::string, json> parseModule(std::string_view source) {
    std::map<std::string, json> result;
    Parser parser(source);
    while (true) {
        auto code = parser.nextAssignment();
        if (code.empty())
            break;
        parser.skipSpace();
        // m["x"] = process{ ... } : a call with a table argument
        json v = parser.value();
        result[code] = std::move(v);
    }
    return result;
}

json parseValue(std::string_view source) {
    Parser parser(source);
    return parser.value();
}

const json &positional(const json &table, size_t i) {
    static const json null;
    if (!table.is_object())
        return null;
    auto it = table.find("#");
    if (it == table.end() || !it->is_array() || i >= it->size())
        return null;
    return (*it)[i];
}

} // namespace LuaData
