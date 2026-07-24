// ============================================================================
// SYSTEM SPECIFICATION: SLABFLUX METADATA COMPILER
// ============================================================================
// TOPOLOGICAL INVARIANT MAP:
// 1. Category Theory and Homological Algebra:
//    The AST Lexer operates as a strict monoidal functor, mapping the raw
//    translation unit (the source category) into a discrete homological chain
//    complex of structural inheritance nodes.
// 2. Global Differential Geometry and Gauge Field Theory:
//    The dependency resolution engine acts as a principal fiber bundle,
//    calculating the exact non-vanishing connection forms between derived
//    event posets and their base topologies.
// 3. Symplectic Topologies and Algebraic Posets:
//    The generated output represents the strict lexicographical monoidal
//    gradients required by the SlabFlux deterministic pipeline matrix,
//    enforcing zero-ambiguity parent typelist definitions for all nodes.
// ============================================================================

#include <iostream>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <sstream>

namespace slabflux::compiler {

    // ============================================================================
    // ALGEBRAIC DATA STRUCTURES
    // ============================================================================

    enum class CurvatureToken {
        ClassDef,
        StructDef,
        NamespaceDef,
        Colon,
        Comma,
        LBrace,
        RBrace,
        LParen,
        RParen,
        LAngle,
        RAngle,
        LBracket,
        RBracket,
        Template,
        AlignAs,
        AlignOf,
        RegisterContext,
        SemiColon,
        Identifier,
        Public,
        Private,
        Protected,
        Virtual,
        EndOfStream,
        Unknown,
        ScopeResolution,
        StringLiteral
    };

    struct LexicalTensor {
        CurvatureToken kind;
        std::string_view payload;
    };

    // ============================================================================
    // GAUGE FIELD LEXER (ZERO-ALLOCATION SCANNER)
    // ============================================================================

    class AsymmetricLexer {
        std::string_view source_manifold;
        std::size_t cursor;
        CurvatureToken last_token_kind = CurvatureToken::Unknown;

        void advance_whitespace() {
            while (cursor < source_manifold.size()) {
                if (std::isspace(source_manifold[cursor])) {
                    ++cursor;
                } else if (cursor + 1 < source_manifold.size() && source_manifold[cursor] == '/' && source_manifold[cursor+1] == '/') {
                    while (cursor < source_manifold.size() && source_manifold[cursor] != '\n') {
                        ++cursor;
                    }
                } else if (cursor + 1 < source_manifold.size() && source_manifold[cursor] == '/' && source_manifold[cursor+1] == '*') {
                    cursor += 2;
                    while (cursor + 1 < source_manifold.size() && !(source_manifold[cursor] == '*' && source_manifold[cursor+1] == '/')) {
                        ++cursor;
                    }
                    if (cursor + 1 < source_manifold.size()) { cursor += 2; }
                    else { cursor = source_manifold.size(); }
                } else if (source_manifold[cursor] == '#') {
                    while (cursor < source_manifold.size() && source_manifold[cursor] != '\n') {
                        if (source_manifold[cursor] == '\\') {
                            ++cursor;
                            if (cursor < source_manifold.size() && source_manifold[cursor] == '\r') { ++cursor; }
                            if (cursor < source_manifold.size() && source_manifold[cursor] == '\n') { ++cursor; }
                        } else {
                            ++cursor;
                        }
                    }
                } else {
                    break;
                }
            }
        }

        bool is_identifier_char(char c) const {
            return std::isalnum(c) || c == '_';
        }

    public:
        explicit AsymmetricLexer(std::string_view buffer) : source_manifold(buffer), cursor(0) {}
        
        bool last_token_was(CurvatureToken k) const { return last_token_kind == k; }

        LexicalTensor peek_next_tensor() {
            std::size_t saved_cursor = cursor;
            CurvatureToken saved_last = last_token_kind;
            LexicalTensor t = extract_internal();
            cursor = saved_cursor;
            last_token_kind = saved_last;
            return t;
        }

        LexicalTensor extract_next_tensor() {
            LexicalTensor result = extract_internal();
            last_token_kind = result.kind;
            return result;
        }

    private:
        LexicalTensor extract_internal() {
            advance_whitespace();

            if (cursor >= source_manifold.size()) {
                return {CurvatureToken::EndOfStream, {}};
            }

            char current = source_manifold[cursor];

            if (current == ':') {
                ++cursor;
                if (cursor < source_manifold.size() && source_manifold[cursor] == ':')
                    { ++cursor; return {CurvatureToken::ScopeResolution, source_manifold.substr(cursor - 2, 2)}; }
                return {CurvatureToken::Colon, source_manifold.substr(cursor - 1, 1)};
            }
            if (current == ',') {
                ++cursor;
                return {CurvatureToken::Comma, source_manifold.substr(cursor - 1, 1)};
            }
            if (current == '(') {
                ++cursor;
                return {CurvatureToken::LParen, source_manifold.substr(cursor - 1, 1)};
            }
            if (current == ')') {
                ++cursor;
                return {CurvatureToken::RParen, source_manifold.substr(cursor - 1, 1)};
            }
            if (current == '<') {
                ++cursor;
                return {CurvatureToken::LAngle, source_manifold.substr(cursor - 1, 1)};
            }
            if (current == '>') {
                ++cursor;
                return {CurvatureToken::RAngle, source_manifold.substr(cursor - 1, 1)};
            }
            if (current == ';') {
                ++cursor;
                return {CurvatureToken::SemiColon, source_manifold.substr(cursor - 1, 1)};
            }
            if (current == '{') {
                ++cursor;
                return {CurvatureToken::LBrace, source_manifold.substr(cursor - 1, 1)};
            }
            if (current == '}') {
                ++cursor;
                return {CurvatureToken::RBrace, source_manifold.substr(cursor - 1, 1)};
            }
            if (current == '[') {
                ++cursor;
                return {CurvatureToken::LBracket, source_manifold.substr(cursor - 1, 1)};
            }
            if (current == ']') {
                ++cursor;
                return {CurvatureToken::RBracket, source_manifold.substr(cursor - 1, 1)};
            }
            
            if (current == '"' || current == '\'') {
                std::size_t start = cursor;
                char quote = source_manifold[cursor++];
                while (cursor < source_manifold.size() && source_manifold[cursor] != quote) {
                    if (source_manifold[cursor] == '\\') {
                        cursor += 1;
                        if (cursor < source_manifold.size()) ++cursor;
                    } else {
                        ++cursor;
                    }
                }
                if (cursor < source_manifold.size()) ++cursor;
                return {CurvatureToken::StringLiteral, source_manifold.substr(start, cursor - start)};
            }

            if (is_identifier_char(current)) {
                std::size_t start = cursor;
                while (cursor < source_manifold.size() && is_identifier_char(source_manifold[cursor])) {
                    ++cursor;
                }
                std::string_view token_str = source_manifold.substr(start, cursor - start);

                // C++11 Raw String Literal Exclusivity
                if (cursor < source_manifold.size() && source_manifold[cursor] == '"' && 
                    (token_str == "R" || token_str == "uR" || token_str == "UR" || token_str == "LR" || token_str == "u8R")) {
                    ++cursor; // Skip '"'
                    std::size_t delim_start = cursor;
                    while (cursor < source_manifold.size() && source_manifold[cursor] != '(') ++cursor;
                    std::string_view delim = source_manifold.substr(delim_start, cursor - delim_start);
                    if (cursor < source_manifold.size()) ++cursor; // Skip '('
                    
                    std::string end_marker = ")" + std::string(delim) + "\"";
                    std::size_t end_pos = source_manifold.find(end_marker, cursor);
                    if (end_pos != std::string_view::npos) { cursor = end_pos + end_marker.size(); } 
                    else { cursor = source_manifold.size(); }
                    return {CurvatureToken::StringLiteral, source_manifold.substr(start, cursor - start)};
                }

                if (token_str == "class")     return {CurvatureToken::ClassDef, token_str};
                if (token_str == "struct")    return {CurvatureToken::StructDef, token_str};
                if (token_str == "namespace") return {CurvatureToken::NamespaceDef, token_str};
                if (token_str == "public")    return {CurvatureToken::Public, token_str};
                if (token_str == "private")   return {CurvatureToken::Private, token_str};
                if (token_str == "protected") return {CurvatureToken::Protected, token_str};
                if (token_str == "virtual")   return {CurvatureToken::Virtual, token_str};
                if (token_str == "REGISTER_CONTEXT") return {CurvatureToken::RegisterContext, token_str};
                if (token_str == "template")  return {CurvatureToken::Template, token_str};
                if (token_str == "alignas")   return {CurvatureToken::AlignAs, token_str};
                if (token_str == "alignof")   return {CurvatureToken::AlignOf, token_str};

                return {CurvatureToken::Identifier, token_str};
            }

            ++cursor;
            return {CurvatureToken::Unknown, source_manifold.substr(cursor - 1, 1)};
        }
    }; // end AsymmetricLexer

    // ============================================================================
    // PRINCIPAL FIBER BUNDLE PARSER
    // ============================================================================

    class TopologicalParser {
        struct InheritanceNode {
            bool is_struct;
            std::vector<std::string> bases;
            bool is_nested;
        };

        std::unordered_map<std::string, std::vector<std::string>> context_mappings;
        std::unordered_map<std::string, InheritanceNode> inheritance_mappings;
        std::unordered_set<std::string> primary_type_registry;
        std::unordered_set<std::string> primary_context_registry;
        std::vector<std::string> primary_type_order;
        std::vector<std::string> primary_context_order;
        std::unordered_set<std::string> template_registry;
        std::unordered_set<std::string> nested_type_registry;
        std::unordered_set<std::string> discovered_namespaces;
        std::vector<std::string> namespace_discovery_order;

    public:
        /**
         * @brief PASS 1: Structural Discovery
         * Populates the global registry of namespaces and types without resolving inheritance.
         */
        void discover_symbols(std::string_view source_data) {
            AsymmetricLexer lexer(source_data);
            struct Scope { std::string name; int depth; bool is_type; };
            std::vector<Scope> ns_stack;
            std::vector<int> type_braces;
            CurvatureToken last_token = CurvatureToken::Unknown;
            int brace_depth = 0;

            while (true) {
                LexicalTensor tensor = lexer.extract_next_tensor();
                if (tensor.kind == CurvatureToken::EndOfStream) break;

                if (tensor.kind == CurvatureToken::Template) {
                    LexicalTensor next = lexer.extract_next_tensor();
                    if (next.kind == CurvatureToken::LAngle) {
                        int d = 1, p = 0, b = 0, br = 0;
                        while (d > 0) {
                            LexicalTensor t = lexer.extract_next_tensor();
                            if (t.kind == CurvatureToken::EndOfStream) break;
                            if (t.kind == CurvatureToken::LParen) p++;
                            else if (t.kind == CurvatureToken::RParen) p--;
                            else if (t.kind == CurvatureToken::LBracket) b++;
                            else if (t.kind == CurvatureToken::RBracket) b--;
                            else if (t.kind == CurvatureToken::LBrace) br++;
                            else if (t.kind == CurvatureToken::RBrace) br--;
                            else if (t.kind == CurvatureToken::LAngle) { if (p == 0 && b == 0 && br == 0) d++; }
                            else if (t.kind == CurvatureToken::RAngle) { if (p == 0 && b == 0 && br == 0) d--; }
                        }
                    }
                    last_token = CurvatureToken::Template; continue;
                }

                if (tensor.kind == CurvatureToken::NamespaceDef) {
                    LexicalTensor t;
                    std::vector<std::string> parts;
                    while ((t = lexer.extract_next_tensor()).kind != CurvatureToken::LBrace && t.kind != CurvatureToken::EndOfStream && t.kind != CurvatureToken::SemiColon) {
                        if (t.kind == CurvatureToken::LBracket) {
                            int b = 1; while (b > 0) {
                                LexicalTensor bt = lexer.extract_next_tensor();
                                if (bt.kind == CurvatureToken::LBracket) b++;
                                else if (bt.kind == CurvatureToken::RBracket) b--;
                                else if (bt.kind == CurvatureToken::EndOfStream) break;
                            }
                        } else if (t.kind == CurvatureToken::Identifier) {
                            parts.push_back(std::string(t.payload));
                        }
                    }
                    if (t.kind == CurvatureToken::LBrace) {
                        brace_depth++;
                        std::string path = "::";
                        for (const auto& ns : ns_stack) { if (!ns.name.empty()) path += ns.name + "::"; }
                        if (parts.empty()) ns_stack.push_back({"", brace_depth, false});
                        else {
                            for (const auto& part : parts) {
                                path += part;
                                if (discovered_namespaces.insert(path).second) namespace_discovery_order.push_back(path);
                                path += "::";
                                ns_stack.push_back({part, brace_depth, false});
                            }
                        }
                    }
                    last_token = tensor.kind; continue;
                } else if (tensor.kind == CurvatureToken::ClassDef || tensor.kind == CurvatureToken::StructDef) {
                    bool is_template = (last_token == CurvatureToken::Template);
                    bool is_nested = std::any_of(ns_stack.begin(), ns_stack.end(), [](const auto& s){ return s.is_type; });
                    LexicalTensor name_t;
                    while (true) {
                        name_t = lexer.extract_next_tensor();
                        if (name_t.kind == CurvatureToken::AlignAs || name_t.kind == CurvatureToken::AlignOf) {
                            int d = 0; while (true) { LexicalTensor t = lexer.extract_next_tensor();
                                if (t.kind == CurvatureToken::LParen) d++; else if (t.kind == CurvatureToken::RParen && --d <= 0) break;
                                else if (t.kind == CurvatureToken::EndOfStream) break; }
                        } else if (name_t.kind == CurvatureToken::LBracket) {
                            int d = 1; while (true) { LexicalTensor tt = lexer.extract_next_tensor();
                                if (tt.kind == CurvatureToken::LBracket) d++; else if (tt.kind == CurvatureToken::RBracket && --d == 0) break;
                                else if (tt.kind == CurvatureToken::EndOfStream) break; }
                        } else if (name_t.kind == CurvatureToken::ScopeResolution) {
                            // Consume leading scope
                        } else if (name_t.kind == CurvatureToken::Identifier) {
                            if (name_t.payload == "__attribute__" || name_t.payload == "__declspec") {
                                LexicalTensor nxt = lexer.peek_next_tensor();
                                if (nxt.kind == CurvatureToken::LParen) {
                                    lexer.extract_next_tensor();
                                    int p = 1; while(p > 0) {
                                        LexicalTensor pt = lexer.extract_next_tensor();
                                        if (pt.kind == CurvatureToken::LParen) p++;
                                        else if (pt.kind == CurvatureToken::RParen) p--;
                                        else if (pt.kind == CurvatureToken::EndOfStream) break;
                                    }
                                }
                                continue;
                            }
                            break;
                        } else if (name_t.kind == CurvatureToken::EndOfStream) {
                            break;
                        } else {
                            break;
                        }
                    }
                    if (name_t.kind == CurvatureToken::Identifier) {
                        std::string fq_name = "::";
                        for (const auto& ns : ns_stack) { if (!ns.name.empty()) fq_name += ns.name + "::"; }
                        fq_name += std::string(name_t.payload);
                        // Pre-register for Pass 2 resolution
                        if (!inheritance_mappings.contains(fq_name)) {
                            inheritance_mappings[fq_name] = { (tensor.kind == CurvatureToken::StructDef), {}, is_nested };
                        }
                        if (is_nested) nested_type_registry.insert(fq_name);
                        if (is_template) template_registry.insert(fq_name);

                        // STRICT SCOPE TRACKING: Ensure nested types don't bleed into parent namespaces
                        bool has_body = false;
                        while (true) {
                            LexicalTensor next = lexer.extract_next_tensor();
                            if (next.kind == CurvatureToken::LBrace) {
                                has_body = true; break;
                            } else if (next.kind == CurvatureToken::Colon) {
                                while (true) {
                                    LexicalTensor bt = lexer.extract_next_tensor();
                                    if (bt.kind == CurvatureToken::LBrace) { has_body = true; goto pass1_end_class; }
                                    if (bt.kind == CurvatureToken::SemiColon || bt.kind == CurvatureToken::EndOfStream) goto pass1_end_class;
                                }
                            } else if (next.kind == CurvatureToken::SemiColon || next.kind == CurvatureToken::EndOfStream) {
                                break;
                            }
                        }
                    pass1_end_class:
                        if (has_body) {
                            brace_depth++;
                            type_braces.push_back(brace_depth);
                            ns_stack.push_back({std::string(name_t.payload), brace_depth, true});
                        }
                    }
                } else if (tensor.kind == CurvatureToken::LBrace) {
                    brace_depth++;
                } else if (tensor.kind == CurvatureToken::RBrace) {
                    if (!type_braces.empty() && type_braces.back() == brace_depth) type_braces.pop_back();
                    while (!ns_stack.empty() && ns_stack.back().depth == brace_depth) ns_stack.pop_back();
                    if (brace_depth > 0) brace_depth--;
                }
                last_token = tensor.kind;
            }
        }

        /**
         * @brief PASS 2: Topological Analysis
         * Fully resolves inheritance and context mappings using Pass 1 results.
         */
        void digest_manifold(std::string_view source_data, bool is_primary_target) {
            AsymmetricLexer lexer(source_data);
            struct Scope { std::string name; int depth; bool is_type; };
            std::vector<Scope> ns_stack; 
            std::vector<int> type_braces;
            std::vector<std::string> active_usings;
            CurvatureToken last_token = CurvatureToken::Unknown;
            int brace_depth = 0;

            while (true) {
                LexicalTensor tensor = lexer.extract_next_tensor();
                if (tensor.kind == CurvatureToken::EndOfStream) break;

                if (tensor.kind == CurvatureToken::Template) {
                    // Precise template parameter block skipping to preserve following class name
                    LexicalTensor next = lexer.extract_next_tensor();
                    if (next.kind == CurvatureToken::LAngle) {
                        int d = 1, p = 0, b = 0, br = 0;
                        while (d > 0) {
                            LexicalTensor t = lexer.extract_next_tensor();
                            if (t.kind == CurvatureToken::EndOfStream) break;
                            if (t.kind == CurvatureToken::LParen) p++;
                            else if (t.kind == CurvatureToken::RParen) p--;
                            else if (t.kind == CurvatureToken::LBracket) b++;
                            else if (t.kind == CurvatureToken::RBracket) b--;
                            else if (t.kind == CurvatureToken::LBrace) br++;
                            else if (t.kind == CurvatureToken::RBrace) br--;
                            else if (t.kind == CurvatureToken::LAngle) { if (p == 0 && b == 0 && br == 0) d++; }
                            else if (t.kind == CurvatureToken::RAngle) { if (p == 0 && b == 0 && br == 0) d--; }
                        }
                    }
                    last_token = CurvatureToken::Template; continue;
                }

                if (tensor.kind == CurvatureToken::Identifier && tensor.payload == "extern") {
                    LexicalTensor next = lexer.extract_next_tensor();
                    if (next.kind == CurvatureToken::Identifier || next.kind == CurvatureToken::Unknown) { // "C"
                        LexicalTensor brace = lexer.extract_next_tensor();
                        if (brace.kind == CurvatureToken::LBrace) {
                            // extern "C" blocks are transparent scopes
                        last_token = CurvatureToken::Unknown;
                            brace_depth++;
                            continue; 
                        }
                    }
                }

                if (tensor.kind == CurvatureToken::Identifier && tensor.payload == "using") {
                    LexicalTensor next = lexer.peek_next_tensor();
                    if (next.kind == CurvatureToken::NamespaceDef) { // "namespace"
                        lexer.extract_next_tensor(); // Consume "namespace"
                        std::string target_ns = "::";
                        while (true) {
                            LexicalTensor t = lexer.extract_next_tensor();
                            if (t.kind == CurvatureToken::SemiColon || t.kind == CurvatureToken::EndOfStream) break;
                            if (t.kind == CurvatureToken::Identifier) target_ns += std::string(t.payload);
                            else if (t.kind == CurvatureToken::ScopeResolution) target_ns += "::";
                        }
                        if (target_ns != "::") {
                            if (!target_ns.ends_with("::")) target_ns += "::";
                            active_usings.push_back(target_ns);
                        }
                        last_token = CurvatureToken::Unknown; continue;
                    }
                }

                if (tensor.kind == CurvatureToken::NamespaceDef) {
                    std::string ns_name; LexicalTensor t;
                    std::vector<std::string> nested_parts;
                    while ((t = lexer.extract_next_tensor()).kind != CurvatureToken::LBrace && t.kind != CurvatureToken::EndOfStream) {
                        if (t.kind == CurvatureToken::LBracket) {
                            int b = 1; while (b > 0) {
                                LexicalTensor bt = lexer.extract_next_tensor();
                                if (bt.kind == CurvatureToken::LBracket) b++;
                                else if (bt.kind == CurvatureToken::RBracket) b--;
                                else if (bt.kind == CurvatureToken::EndOfStream) break;
                            }
                        } else if (t.kind == CurvatureToken::Identifier) {
                            nested_parts.push_back(std::string(t.payload));
                        }
                        else if (t.kind == CurvatureToken::SemiColon) break;
                    }
                    if (t.kind == CurvatureToken::LBrace) {
                        brace_depth++;
                        std::string running_path = "::";
                        for (const auto& ns : ns_stack) { if (!ns.name.empty()) running_path += ns.name + "::"; }

                        if (nested_parts.empty()) {
                            ns_stack.push_back({"", brace_depth, false});
                        } else {
                            for (const auto& part : nested_parts) {
                                running_path += part;
                                if (discovered_namespaces.insert(running_path).second)
                                    namespace_discovery_order.push_back(running_path);
                                running_path += "::";
                                ns_stack.push_back({part, brace_depth, false});
                            }
                        }
                    }
                    last_token = tensor.kind; continue;
                }

                if (tensor.kind == CurvatureToken::LBrace) { brace_depth++; last_token = tensor.kind; continue; }
                if (tensor.kind == CurvatureToken::RBrace) {
                    if (!type_braces.empty() && type_braces.back() == brace_depth) type_braces.pop_back();
                    while (!ns_stack.empty() && ns_stack.back().depth == brace_depth) ns_stack.pop_back();
                    if (brace_depth > 0) brace_depth--;
                    last_token = tensor.kind; continue;
                }

                auto qualify = [&](std::string name, bool is_template_ref = false) {
                    if (name.empty()) return name;
                    if (name.size() >= 2 && name.substr(0, 2) == "::") return name;

                    // Numerical literals should not be qualified
                    if (std::isdigit(static_cast<unsigned char>(name[0]))) return name;

                    static const std::unordered_set<std::string_view> lang_prims = {
                        "void", "bool", "char", "int", "long", "float", "double", "size_t", "ssize_t",
                        "uint8_t", "uint16_t", "uint32_t", "uint64_t", "int8_t", "int16_t", "int32_t", "int64_t",
                        "unsigned", "signed", "short", "const", "volatile", "std"
                    };
                    if (lang_prims.contains(name)) return "::" + name;

                     auto lookup = [&](const std::string& fq) {
                        std::string fq_base = fq;
                        size_t a = fq_base.find('<');
                        if (a != std::string::npos) fq_base = fq_base.substr(0, a);
                        
                        bool is_tmpl = template_registry.contains(fq_base);
                        if (is_template_ref && !is_tmpl) return false; // Strict Template Guard
                        
                        return primary_type_registry.contains(fq) || 
                               inheritance_mappings.contains(fq_base) || 
                               is_tmpl ||
                               (!is_template_ref && discovered_namespaces.contains(fq_base));
                    };

                    auto execute_lookup = [&]() -> std::string {
                        for (int i = (int)ns_stack.size(); i >= 0; --i) {
                            std::string path = "::";
                            for (int j = 0; j < i; ++j) { if (!ns_stack[j].name.empty()) path += ns_stack[j].name + "::"; }
                            std::string candidate = path + name;
                            if (lookup(candidate)) return candidate;
                        }
                        for (const auto& using_ns : active_usings) {
                            std::string candidate = using_ns + name;
                            if (lookup(candidate)) return candidate;
                        }
                        if (name.starts_with("std::") || name.starts_with("testing::") || name.starts_with("google::")) return "::" + name;
                        
                        // If unresolved, return exactly as written (unqualified or partially qualified).
                        // Forcing "::" causes catastrophic global namespace pollution for header types.
                        return name;
                    };

                    std::string result = execute_lookup();

                    // POST-RESOLUTION SANITIZER: Immunity against Cross-TU #ifdef brace-drift
                    // If a core framework type was mis-registered into slabflux::core due to unbalanced
                    // preprocessor macros in 150+ headers, we forcibly re-align it to the true topological root.
                    if (result == "::slabflux::core::typelist") return std::string("::slabflux::typelist");
                    if (result == "::slabflux::core::meta_traits") return std::string("::slabflux::meta_traits");
                    if (result == "::slabflux::core::context_association") return std::string("::slabflux::context_association");

                    return result;
                };

                auto extract_qualified_type = [&](CurvatureToken stopper1, CurvatureToken stopper2) {
                    std::string res; std::string current_path; int d = 0; int p = 0; int b = 0;
                    auto flush = [&](bool is_tmpl = false) {
                        if (!current_path.empty()) {
                            res += qualify(current_path, is_tmpl);
                            current_path.clear();
                        }
                    };

                    while (true) {
                        LexicalTensor t = lexer.extract_next_tensor();
                        if (t.kind == CurvatureToken::EndOfStream) break;
                        if (t.kind == CurvatureToken::Public || t.kind == CurvatureToken::Private || 
                            t.kind == CurvatureToken::Protected || t.kind == CurvatureToken::Virtual) continue;

                        if (d == 0 && p == 0 && b == 0 && (t.kind == stopper1 || t.kind == stopper2)) { flush(); break; }

                        if (t.kind == CurvatureToken::LAngle) { flush(true); if (p == 0 && b == 0) d++; res += "<"; }
                        else if (t.kind == CurvatureToken::RAngle) { flush(); if (p == 0 && b == 0) d--; res += ">"; }
                        else if (t.kind == CurvatureToken::LParen) { flush(); p++; res += "("; }
                        else if (t.kind == CurvatureToken::RParen) { flush(); p--; res += ")"; }
                        else if (t.kind == CurvatureToken::LBracket) { flush(); b++; res += "["; }
                        else if (t.kind == CurvatureToken::RBracket) { flush(); b--; res += "]"; }
                        else if (t.kind == CurvatureToken::Identifier) {
                            current_path += t.payload;
                        } else if (t.kind == CurvatureToken::ScopeResolution) {
                            current_path += "::";
                        } else if (t.kind == CurvatureToken::Comma) {
                            flush(); res += ", ";
                        } else {
                            flush(); res += t.payload;
                        }
                    }
                    flush();
                    return res;
                };

                if (tensor.kind == CurvatureToken::ClassDef || tensor.kind == CurvatureToken::StructDef) {
                    bool is_template = (last_token == CurvatureToken::Template);
                    
                    // Detect if we are nested inside a class/struct based on the scope stack
                    bool is_nested = std::any_of(ns_stack.begin(), ns_stack.end(), [](const auto& s){ return s.is_type; });

                    bool is_struct = (tensor.kind == CurvatureToken::StructDef);
                    
                    LexicalTensor name_t;
                    while (true) {
                        name_t = lexer.extract_next_tensor();
                        if (name_t.kind == CurvatureToken::AlignAs || name_t.kind == CurvatureToken::AlignOf) {
                            int d = 0; while (true) { LexicalTensor t = lexer.extract_next_tensor();
                                if (t.kind == CurvatureToken::LParen) d++; else if (t.kind == CurvatureToken::RParen) { if (--d == 0) break; }
                                else if (t.kind == CurvatureToken::EndOfStream) break; }
                        } else if (name_t.kind == CurvatureToken::LBracket) {
                            int d = 1; while (true) { LexicalTensor t = lexer.extract_next_tensor();
                                if (t.kind == CurvatureToken::LBracket) d++; else if (t.kind == CurvatureToken::RBracket) { if (--d == 0) break; }
                                else if (t.kind == CurvatureToken::EndOfStream) break; }
                        } else if (name_t.kind == CurvatureToken::ScopeResolution) {
                            // Leading :: handled by qualify later, just consume it
                        } else if (name_t.kind == CurvatureToken::Identifier) {
                            if (name_t.payload == "__attribute__" || name_t.payload == "__declspec") {
                                LexicalTensor nxt = lexer.peek_next_tensor();
                                if (nxt.kind == CurvatureToken::LParen) {
                                    lexer.extract_next_tensor();
                                    int p = 1; while(p > 0) {
                                        LexicalTensor pt = lexer.extract_next_tensor();
                                        if (pt.kind == CurvatureToken::LParen) p++;
                                        else if (pt.kind == CurvatureToken::RParen) p--;
                                        else if (pt.kind == CurvatureToken::EndOfStream) break;
                                    }
                                }
                                continue;
                            }
                            break;
                        } else {
                            break;
                        }
                    }
                    
                    if (name_t.kind != CurvatureToken::Identifier) { last_token = tensor.kind; continue; }
                    
                    std::string raw_name(name_t.payload);
                    
                    if (raw_name == "meta_traits" || raw_name == "typelist") { last_token = tensor.kind; continue; }
                    
                    // Precise definition name generation: Absolute path from current namespace stack
                    std::string name = "::";
                    bool is_in_anonymous = false;
                    for (const auto& ns : ns_stack) { 
                        if (ns.name.empty()) is_in_anonymous = true;
                        else name += ns.name + "::"; 
                    }
                    name += raw_name;
                    
                    // Register the type early so it can be resolved as a base class for subsequent types
                    if (!name.empty() && !is_template) {
                        inheritance_mappings[name] = {is_struct, {}, is_nested};
                        if (is_nested) nested_type_registry.insert(name);
                    }
                    if (!name.empty() && is_template) template_registry.insert(name);

                    std::vector<std::string> bases;
                    bool has_body = false;
                    while (true) {
                        LexicalTensor next = lexer.extract_next_tensor();
                        if (next.kind == CurvatureToken::Colon) {
                            while (true) {
                                std::string b = extract_qualified_type(CurvatureToken::Comma, CurvatureToken::LBrace);
                                if (!b.empty()) bases.push_back(b);
                                // The stopper (Comma or LBrace) is already consumed by extract_qualified_type
                                // We need to check if we hit the body
                                if (lexer.last_token_was(CurvatureToken::LBrace)) {
                                    brace_depth++;
                                    type_braces.push_back(brace_depth);
                                    ns_stack.push_back({raw_name, brace_depth, true});
                                    has_body = true; goto end_class_decl;
                                }
                                if (lexer.last_token_was(CurvatureToken::SemiColon)) break;
                                if (!lexer.last_token_was(CurvatureToken::Comma) && !lexer.last_token_was(CurvatureToken::LAngle)) {
                                    // If we hit something else at depth 0, check if it's the start of body
                                    break;
                                }
                            }
                        } else if (next.kind == CurvatureToken::LBrace) {
                            brace_depth++;
                            type_braces.push_back(brace_depth);
                            ns_stack.push_back({raw_name, brace_depth, true});
                            has_body = true; break;
                        } else if (next.kind == CurvatureToken::SemiColon || next.kind == CurvatureToken::EndOfStream) break;
                    }
                end_class_decl:
                    if (has_body && !is_template && !name.empty()) {
                        inheritance_mappings[name].bases = std::move(bases);
                        if (is_primary_target && !is_in_anonymous && !is_nested) {
                            if (primary_type_registry.insert(name).second)
                                primary_type_order.push_back(name);
                        }
                    }
                    last_token = tensor.kind; continue;
                }

                if (tensor.kind == CurvatureToken::RegisterContext) {
                    if (lexer.extract_next_tensor().kind != CurvatureToken::LParen) { last_token = tensor.kind; continue; }
                    std::string ev = extract_qualified_type(CurvatureToken::Comma, CurvatureToken::RParen);
                    std::string ctx = extract_qualified_type(CurvatureToken::Comma, CurvatureToken::RParen);
                    if (!ev.empty() && !ctx.empty()) {
                        context_mappings[ev].push_back(ctx);
                        if (is_primary_target && primary_context_registry.insert(ev).second)
                            primary_context_order.push_back(ev);
                    }
                }
                last_token = tensor.kind;
            }
        }

        const std::unordered_map<std::string, std::vector<std::string>>& retrieve_contexts() const {
            return context_mappings;
        }

        const std::unordered_map<std::string, InheritanceNode>& retrieve_inheritance() const {
            return inheritance_mappings;
        }
        
        const std::unordered_set<std::string>& retrieve_namespaces() const {
            return discovered_namespaces;
        }

        const std::vector<std::string>& retrieve_namespace_order() const {
            return namespace_discovery_order;
        }

        const std::vector<std::string>& retrieve_primary_types() const {
            return primary_type_order;
        }

        const std::vector<std::string>& retrieve_primary_contexts() const {
            return primary_context_order;
        }

        const std::unordered_set<std::string>& retrieve_templates() const {
            return template_registry;
        }

        const std::unordered_set<std::string>& retrieve_nested() const {
            return nested_type_registry;
        }
    };

    // ============================================================================
    // STRICT MONOIDAL COMPILER EMISSION
    // ============================================================================

    class MetadataCompiler {
        std::vector<std::string> target_files;
        std::string output_target;

    public:
        MetadataCompiler(std::vector<std::string> inputs, std::string output)
        : target_files(std::move(inputs)), output_target(std::move(output)) {}

        void execute_symbolic_compilation() {
            auto start_time = std::chrono::high_resolution_clock::now();
            TopologicalParser parser;
            std::vector<std::string> buffers;

            // Pre-load and Pass 1
            for (const auto& filepath : target_files) {
                // Break the generation feedback loop: Never parse previously generated MOC headers
                if (filepath.find("_meta.hpp") != std::string::npos || 
                    filepath.find("_moc.hpp") != std::string::npos || 
                    filepath.find("slabflux_generated") != std::string::npos) { 
                    buffers.push_back(""); continue; 
                }
                std::ifstream stream(filepath, std::ios::in | std::ios::ate | std::ios::binary);
                if (!stream) { buffers.push_back(""); continue; }
                std::streamsize size = stream.tellg();
                stream.seekg(0, std::ios::beg);
                std::string buf(size, '\0');
                stream.read(&buf[0], size);

                // Skip files that explicitly request to be ignored (e.g., isolated test environments)
                if (buf.find("SLABFLUX_SKIP_META_COMPILER") != std::string::npos) {
                    buffers.push_back(""); continue;
                }

                parser.discover_symbols(buf);
                buffers.push_back(std::move(buf));
            }

            // Pass 2
            for (size_t i = 0; i < buffers.size(); ++i) {
                if (!buffers[i].empty()) {
                    bool is_primary = (i == 0);
                    if (target_files[i].ends_with(".cpp") || target_files[i].ends_with(".cc") || target_files[i].ends_with(".cxx")) is_primary = true;
                    parser.digest_manifold(buffers[i], is_primary);
                }
            }

            // Registry isolation
            const auto& primary_types = parser.retrieve_primary_types();
            const auto& primary_contexts = parser.retrieve_primary_contexts();
            const auto& templates = parser.retrieve_templates();
            const auto& inheritance = parser.retrieve_inheritance();
            const auto& namespaces = parser.retrieve_namespaces();
            const auto& ns_order = parser.retrieve_namespace_order();
            const auto& contexts = parser.retrieve_contexts();

            std::ostringstream out_stream;

            // Boundary generation following absolute top-down rigid specification
            out_stream << "// ============================================================================\n";
            out_stream << "// FILE: " << std::filesystem::path(output_target).filename().string() << "\n";
            out_stream << "// GENERATED BY SLABFLUX METADATA COMPILER\n";
            out_stream << "// DOMAIN ISOLATION STRICTLY ENFORCED\n";
            out_stream << "// ============================================================================\n";
            out_stream << "#ifndef SLABFLUX_GENERATED_META_HPP\n";
            out_stream << "#define SLABFLUX_GENERATED_META_HPP\n\n";
            out_stream << "#include \"slabflux/meta.hpp\"\n\n";
            out_stream << "#include \"slabflux/pipeline/context_vault.hpp\"\n\n";

            std::unordered_set<std::string> emitted_fwd;
            auto emit_fwd = [&](const std::string& t) {
                if (t.find('<') != std::string::npos || t.empty()) return;
                if (emitted_fwd.count(t)) return;

                // DYNAMIC ISOLATION: Never guess global namespace for unresolved unqualified types.
                // If it lacks "::", it relies on C++ ADL/using. Emitting it globally causes ODR conflicts.
                if (!t.starts_with("::")) return;

                // SYSTEM ISOLATION: Never forward declare names from standard or test-framework namespaces.
                if (t.starts_with("::std") || t.starts_with("::testing") || t.starts_with("::google")) return;

                // DYNAMIC ISOLATION: Never forward declare known namespaces as structs,
                // UNLESS the symbol is explicitly registered as a type.
                if (namespaces.contains(t) && !inheritance.contains(t)) return;

                std::string type_name = (t.starts_with("::")) ? t.substr(2) : t;
                std::string base_name = type_name;
                size_t last_ns = type_name.rfind("::");
                if (last_ns != std::string::npos) base_name = type_name.substr(last_ns + 2);

                // Primary Sovereignty: Symbols defined in the target manifold must be emitted
                // even if headers misidentify them as templates or namespaces.
                bool is_primary = std::find(primary_types.begin(), primary_types.end(), t) != primary_types.end();

                // Strict Agnostic Filter: Protect C++ language and Metadata internals
                static const std::unordered_set<std::string_view> blacklisted = {
                    "meta_traits", "context_association", "typelist", "typelist_append",
                    "typelist_cat", "typelist_contains", "typelist_unique", "all_registered_contexts",
                    "void", "bool", "char", "int", "long", "float", "double", "unsigned",
                    "size_t", "uint8_t", "uint16_t", "uint32_t", "uint64_t", "alignas", "alignof"
                };

                if (!is_primary && (blacklisted.contains(base_name) || templates.contains(t))) return;
                
                emitted_fwd.insert(t);
                
                std::string ns_open, ns_close;
                std::string current_fq = "::";
                
                size_t pos = 0;
                while ((pos = type_name.find("::")) != std::string::npos) {
                    std::string part = type_name.substr(0, pos);
                    current_fq += part;
                    if (namespaces.contains(current_fq) && !inheritance.contains(current_fq)) {
                        ns_open += "namespace " + part + " { ";
                        ns_close = " }" + ns_close;
                    } else {
                        // ODR GUARD: Impossible to safely forward-declare nested structs inside force-included MOCs
                        return;
                    }
                    current_fq += "::";
                    type_name = type_name.substr(pos + 2);
                }
                out_stream << ns_open << "struct " << type_name << ";" << ns_close << "\n";
            };

            // Discovery-Order Preservation: Collect required fwds in the exact order they appear
            std::unordered_set<std::string> required_fwds;
            std::vector<std::string> required_fwd_order;
            auto add_required = [&](const std::string& t) {
                if (t.empty()) return;
                if (required_fwds.insert(t).second) required_fwd_order.push_back(t);
            };

            for (const auto& target_name : primary_types) {
                if (inheritance.contains(target_name) && !inheritance.at(target_name).bases.empty()) {
                    add_required(target_name);
                    for (const auto& b : inheritance.at(target_name).bases) add_required(b);
                }
            }
            for (const auto& target_ev : primary_contexts) {
                add_required(target_ev);
                if (contexts.contains(target_ev)) { for (const auto& ctx : contexts.at(target_ev)) add_required(ctx); }
            }

            // Collect active namespaces based on discovery order of types to prevent ghost shells
            std::unordered_set<std::string> active_namespaces;
            auto collect_ns = [&](const std::string& t) {
                std::string ns_check = t;
                size_t pos;
                while ((pos = ns_check.rfind("::")) != std::string::npos) {
                    ns_check = ns_check.substr(0, pos);
                    if (ns_check == "::" || ns_check.empty()) break;
                    if (namespaces.contains(ns_check)) active_namespaces.insert(ns_check);
                }
            };
            for (const auto& t : primary_types) collect_ns(t);
            for (const auto& t : primary_contexts) collect_ns(t);

            out_stream << "\n    // ==========================================\n";
            out_stream << "    // SMART FORWARD DECLARATIONS\n";
            out_stream << "    // ==========================================\n";

            // Stable Namespace Shells: Follow discovery order to maintain topological integrity.
            // Mandatory for C++ compiler visibility in sibling namespaces.
            for (const auto& ns : ns_order) {
                if (!active_namespaces.contains(ns)) continue;
                if (ns.starts_with("::std") || ns.starts_with("::testing") || ns.starts_with("::google")) continue;
                std::string ns_path = (ns.starts_with("::")) ? ns.substr(2) : ns;
                std::string ns_open, ns_close;
                size_t pos = 0;
                while ((pos = ns_path.find("::")) != std::string::npos) {
                    ns_open += "namespace " + ns_path.substr(0, pos) + " { ";
                    ns_close = " }" + ns_close;
                    ns_path = ns_path.substr(pos + 2);
                }
                out_stream << ns_open << "namespace " << ns_path << " {}" << ns_close << "\n";
            }

            // Stable Forward Declarations: Follow discovery order to maintain topological integrity.
            // This ensures sibling types like verification::rig and test::rig are both emitted.
            for (const auto& fwd : required_fwd_order) {
                emit_fwd(fwd);
            }

            out_stream << "\nnamespace slabflux::reflection {\n\n";
            out_stream << "#ifndef SLABFLUX_REFLECTION_meta_traits_DEFINED\n";
            out_stream << "#define SLABFLUX_REFLECTION_meta_traits_DEFINED\n";
            out_stream << "    template <typename T> struct meta_traits { using parents = slabflux::typelist<>; };\n";
            out_stream << "#endif\n\n";

            out_stream << "\n    // ==========================================\n";
            out_stream << "    // UNIFIED CONTEXT ASSOCIATIONS\n";
            out_stream << "    // ==========================================\n";
            std::unordered_set<std::string> unique_contexts;
            for (const auto& target_ev : parser.retrieve_primary_contexts()) {
                if (contexts.contains(target_ev)) {
                    const auto& ctxs = contexts.at(target_ev);
                    out_stream << "    template <> struct context_association< " << (target_ev.starts_with("::") ? " " : "") << target_ev << " > {\n";
                    out_stream << "        using contexts = slabflux::typelist<";
                    for (std::size_t i = 0; i < ctxs.size(); ++i) {
                        out_stream << ctxs[i] << (i < ctxs.size() - 1 ? ", " : "");
                        unique_contexts.insert(ctxs[i]);
                    }
                    out_stream << ">;\n";
                    out_stream << "        static constexpr bool has_context = true;\n";
                    out_stream << "    };\n";
                }
            }

            out_stream << "\n    // ==========================================\n";
            out_stream << "    // TOPOLOGICAL INHERITANCE TRAITS\n";
            out_stream << "    // ==========================================\n";
            for (const auto& target_name : primary_types) {
                if (inheritance.contains(target_name)) {
                    const auto& node = inheritance.at(target_name);
                    // Skip leaf nodes with no parents. Native C++ typelist<> handles them flawlessly.
                    if (node.bases.empty()) continue;

                    out_stream << "    template <> struct meta_traits< " << (target_name.starts_with("::") ? " " : "") << target_name << " > { ";
                    out_stream << "using parents = slabflux::typelist<";
                    
                    std::vector<std::string> filtered;
                    for (const auto& b : node.bases) {
                        if (b.starts_with("::std::") || b.starts_with("::testing::") || b.starts_with("::google::") ||
                            b == "::std" || b == "::testing" || b == "::google") continue;
                        filtered.push_back(b);
                    }

                    for (std::size_t i = 0; i < filtered.size(); ++i) {
                        out_stream << filtered[i] << (i < filtered.size() - 1 ? ", " : "");
                    }
                    out_stream << ">; };\n";
                }
            }

            out_stream << "\n    // ==========================================\n";
            out_stream << "    // GLOBAL CONTEXT REGISTRY\n";
            out_stream << "    // ==========================================\n";
            out_stream << "    using all_registered_contexts = slabflux::typelist<";
            bool first = true;
            for (const auto& ctx : unique_contexts) {
                out_stream << (first ? "" : ", ") << ctx;
                first = false;
            }
            out_stream << ">;\n";

            out_stream << "\n}\n\n";
            out_stream << "#endif // SLABFLUX_GENERATED_META_HPP\n";

            std::string new_content = out_stream.str();
            bool content_changed = true;
            
            if (std::filesystem::exists(output_target)) {
                std::ifstream existing_file(output_target, std::ios::in | std::ios::binary | std::ios::ate);
                if (existing_file && existing_file.tellg() == static_cast<std::streamsize>(new_content.size())) {
                    existing_file.seekg(0, std::ios::beg);
                    std::string existing_content((std::istreambuf_iterator<char>(existing_file)), std::istreambuf_iterator<char>());
                    if (existing_content == new_content) content_changed = false;
                }
            }

            if (content_changed) {
                std::ofstream final_out(output_target, std::ios::trunc | std::ios::binary);
                if (!final_out) {
                    std::cerr << "SlabFlux Compiler Fault: Output geometry boundary failure -> " << output_target << "\n";
                    return;
                }
                final_out << new_content;
            }

            auto end_time = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double, std::milli> compilation_latency = end_time - start_time;

            std::cout << "SlabFlux Metadata Compilation Terminated Successfully.\n";
            std::cout << "Context Types Mapped: " << unique_contexts.size() << "\n";
            std::cout << "Execution Latency: " << compilation_latency.count() << " ms\n";
        }
    };

} // namespace slabflux::compiler

// ============================================================================
// HARD-TARGET EXECUTION ENTRY
// ============================================================================
#ifndef SLABFLUX_META_TESTING
int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Syntax Violation: SlabFlux Compiler requires exact dimensional bounding.\n";
        std::cerr << "Usage: slabflux_meta <output_file.meta.hpp> <input_header_1.hpp> [input_header_2.hpp ...]\n";
        return 1;
    }

    std::string output_target = argv[1];
    std::vector<std::string> input_targets;

    for (int i = 2; i < argc; ++i) {
        input_targets.emplace_back(argv[i]);
    }

    slabflux::compiler::MetadataCompiler compiler(std::move(input_targets), std::move(output_target));
    compiler.execute_symbolic_compilation();

    return 0;
}
#endif
