#include "lexer.h"

#include <algorithm>
#include <charconv>
#include <unordered_map>
#include <iostream>

using namespace std;

namespace parse {

    bool operator==(const Token& lhs, const Token& rhs) {
        using namespace token_type;

        if (lhs.index() != rhs.index()) {
            return false;
        }
        if (lhs.Is<Char>()) {
            return lhs.As<Char>().value == rhs.As<Char>().value;
        }
        if (lhs.Is<Number>()) {
            return lhs.As<Number>().value == rhs.As<Number>().value;
        }
        if (lhs.Is<String>()) {
            return lhs.As<String>().value == rhs.As<String>().value;
        }
        if (lhs.Is<Id>()) {
            return lhs.As<Id>().value == rhs.As<Id>().value;
        }
        return true;
    }

    bool operator!=(const Token& lhs, const Token& rhs) {
        return !(lhs == rhs);
    }

    std::ostream& operator<<(std::ostream& os, const Token& rhs) {
        using namespace token_type;

#define VALUED_OUTPUT(type) \
    if (auto p = rhs.TryAs<type>()) return os << #type << '{' << p->value << '}';

        VALUED_OUTPUT(Number);
        VALUED_OUTPUT(Id);
        VALUED_OUTPUT(String);
        VALUED_OUTPUT(Char);

#undef VALUED_OUTPUT

#define UNVALUED_OUTPUT(type) \
    if (rhs.Is<type>()) return os << #type;

        UNVALUED_OUTPUT(Class);
        UNVALUED_OUTPUT(Return);
        UNVALUED_OUTPUT(If);
        UNVALUED_OUTPUT(Else);
        UNVALUED_OUTPUT(Def);
        UNVALUED_OUTPUT(Newline);
        UNVALUED_OUTPUT(Print);
        UNVALUED_OUTPUT(Indent);
        UNVALUED_OUTPUT(Dedent);
        UNVALUED_OUTPUT(And);
        UNVALUED_OUTPUT(Or);
        UNVALUED_OUTPUT(Not);
        UNVALUED_OUTPUT(Eq);
        UNVALUED_OUTPUT(NotEq);
        UNVALUED_OUTPUT(LessOrEq);
        UNVALUED_OUTPUT(GreaterOrEq);
        UNVALUED_OUTPUT(None);
        UNVALUED_OUTPUT(True);
        UNVALUED_OUTPUT(False);
        UNVALUED_OUTPUT(Eof);

#undef UNVALUED_OUTPUT

        return os << "Unknown token :("sv;
    }

    Lexer::Lexer(std::istream& input) : in_(input) {
        ReadToken();
    }

    const Token& Lexer::CurrentToken() const {
        return token_.front();
    }

    Token Lexer::NextToken() {
        if (token_.size() > 1) {
            token_.pop_front();
        }
        return CurrentToken();
    }

    void Lexer::ReadToken() {
        string line;
        while (getline(in_, line)) {
            if (!IsEmptyLine(line)) {
                auto count_space = line.find_first_not_of(' ');
                if (count_space % 2 != 0) {
                    throw LexerError("Invalid indents"s);
                } else {
                    ReadIndents(count_space / 2);
                }
                istringstream ss(line.substr(count_space));
                char c;
                while (ss.get(c)) {
                    if (c == '#') {
                        break;
                    }
                    if (isspace(c)) {
                        continue;
                    }
                    if (IsPunct(c)) {
                        if (delimiter_.count(c) == 1 || ariphmetic_operations_.count(c) == 1) {
                            token_.push_back(token_type::Char{c});
                        }
                        if (logic_operations_.count(c) == 1) {
                            if (ss.peek() == '=') {
                                string parsed;
                                parsed += c;
                                parsed += ss.get();
                                if (key_words_.count(parsed) == 1) {
                                    token_.push_back(key_words_.at(parsed));
                                }
                            } else {
                                token_.push_back(token_type::Char{c});
                            }
                        }
                    }
                    if (IsIdentifiersOrNumber(c)) {
                        token_.push_back(ReadIdentifiersOrNumber(c, ss));
                    }
                    if (IsString(c)) {
                        token_.push_back(ReadStrings(c, ss));
                    }
                }
                token_.push_back(token_type::Newline{});
            }
        }
        if (indents > 0) {
            ReadIndents(0);
        }
        token_.push_back(token_type::Eof{});
    }

    bool Lexer::IsPunct(char c) const {
        return delimiter_.count(c) == 1 || logic_operations_.count(c) == 1 || ariphmetic_operations_.count(c) == 1;
    }

    bool Lexer::IsEmptyLine(const string& line) const {
        auto pos = line.find_first_not_of(' ');
        return line.empty() || pos == line.npos || line[pos] == '#';
    }

    bool Lexer::IsString(char c) const {
        return c == '"' || c == '\'';
    }

    Token Lexer::ReadIdentifiersOrNumber(char c, std::istream& ss) {
        if (isdigit(c)) {
            return ReadNumber(c, ss);
        }
        string parsed;
        parsed.push_back(c);
        while (isalnum(ss.peek()) || ss.peek() == '_') {
            parsed += ss.get();
        }
        if (key_words_.count(parsed) == 1) {
            return key_words_.at(parsed);
        }
        return token_type::Id{parsed};
    }

    Token Lexer::ReadStrings(char c, std::istream& ss) {
        string line;
        while (ss.peek() != c) {
            line += ReadChar(ss);
            if (line.back() == '\\') {
                char c = ReadChar(ss);
                line.pop_back();
                switch (c) {
                    case '"':
                        line += '"';
                        break;
                    case 'n':
                        line += '\n';
                        break;
                    case 'r':
                        line += '\r';
                        break;
                    case '\'':
                        line += '\'';
                        break;
                    case 't':
                        line += '\t';
                        break;
                    case '\\':
                        line += '\\';
                        break;
                    default:
                        throw LexerError("Failed string"s);
                }
            }
        }
        ss.ignore();
        return token_type::String{line};
    }

    bool Lexer::IsIdentifiersOrNumber(char c) const {
        return isalnum(c) || c == '_';
    }

    char Lexer::ReadChar(istream& ss) {
        if (!ss) {
            throw LexerError("Failed read"s);
        }
        return static_cast<char>(ss.get());
    }

    Token Lexer::ReadNumber(char c, istream& ss) {
        string parsed_num;
        parsed_num += c;
        auto read_char = [&parsed_num, &ss] {
            parsed_num += static_cast<char>(ss.get());
            if (!ss) {
                throw LexerError("Failed to read number from stream"s);
            }
        };
        auto read_digits = [&ss, read_char] {
            while (std::isdigit(ss.peek())) {
                read_char();
            }
        };

        read_digits();
        try {
            int result = stoi(parsed_num);
            return token_type::Number{result};
        } catch (...) {
            throw LexerError("Number conversion error"s);
        }
    }

    void Lexer::ReadIndents(size_t count_spaces) {
        size_t last_indents = indents;
        if (count_spaces > indents) {
            for (size_t i = last_indents; i < count_spaces; ++i) {
                ++indents;
                token_.push_back(token_type::Indent{});
            }
        }
        if (count_spaces < indents) {
            for (size_t i = count_spaces; i < last_indents; ++i) {
                --indents;
                token_.push_back(token_type::Dedent{});
            }
        }
    }
}  // namespace parse