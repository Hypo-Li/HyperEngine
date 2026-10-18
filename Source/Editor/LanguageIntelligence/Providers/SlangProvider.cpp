#include "SlangProvider.h"

#include <LanguageIntelligence/LspClient/LspClient.h>
#include <Platform/Platform.h>

#include <tree_sitter/api.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <exception>
#include <filesystem>
#include <functional>
#include <mutex>
#include <random>
#include <span>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <utility>

extern "C" const TSLanguage* tree_sitter_slang(void);

namespace hyp::lang
{
    namespace
    {
        // HLSL/Slang texture resource methods (Texture2D::Sample etc.) — these have no tree-sitter
        // declaration to hang a real Function symbol off of, so both semantic coloring and hover
        // special-case this fixed table instead of going through the normal typeMembers/declaration
        // lookup. Not offered as completion: resolving "is the receiver actually a texture" reliably
        // would need macro-aware type inference this codebase doesn't have (see resolveExpressionType).
        const std::vector<FunctionSignature>& builtinTextureMethodSignatures()
        {
            static const std::vector<FunctionSignature> signatures = {
                { "float4", "Sample", { { "SamplerState", "s" }, { "float2", "location" } } },
                { "float4", "SampleLevel", { { "SamplerState", "s" }, { "float2", "location" }, { "float", "lod" } } },
                { "float4", "SampleBias", { { "SamplerState", "s" }, { "float2", "location" }, { "float", "bias" } } },
                { "float4", "SampleGrad", { { "SamplerState", "s" }, { "float2", "location" }, { "float2", "ddx" }, { "float2", "ddy" } } },
                { "float4", "SampleCmp", { { "SamplerComparisonState", "s" }, { "float2", "location" }, { "float", "compareValue" } } },
                { "float4", "SampleCmpLevelZero", { { "SamplerComparisonState", "s" }, { "float2", "location" }, { "float", "compareValue" } } },
                { "float4", "Load", { { "int3", "location" } } },
                { "float4", "Gather", { { "SamplerState", "s" }, { "float2", "location" } } },
                { "float4", "GatherRed", { { "SamplerState", "s" }, { "float2", "location" } } },
                { "float4", "GatherGreen", { { "SamplerState", "s" }, { "float2", "location" } } },
                { "float4", "GatherBlue", { { "SamplerState", "s" }, { "float2", "location" } } },
                { "float4", "GatherAlpha", { { "SamplerState", "s" }, { "float2", "location" } } },
                { "void", "GetDimensions", { { "out uint", "width" }, { "out uint", "height" } } },
                { "float", "CalculateLevelOfDetail", { { "SamplerState", "s" }, { "float2", "location" } } },
                { "float", "CalculateLevelOfDetailUnclamped", { { "SamplerState", "s" }, { "float2", "location" } } },
            };
            return signatures;
        }

        const std::unordered_set<std::string_view>& builtinTextureMethodNames()
        {
            static const std::unordered_set<std::string_view> names = []
            {
                std::unordered_set<std::string_view> result;
                for (const FunctionSignature& signature : builtinTextureMethodSignatures())
                    result.insert(signature.name);
                return result;
            }();
            return names;
        }

        // HLSL/Slang intrinsics (dot, reflect, mul, ...) — every one of these is templated/overloaded
        // for real, but there's no tree-sitter declaration to read an actual call site's overload
        // off of, so this lists one representative signature per name (usually the floatN form) for
        // hover to show. The name set doubles as the tokenizer's builtin-function word list (see
        // builtinFunctionNames()) — kept as a single table so the two can't drift apart.
        const std::vector<FunctionSignature>& builtinFunctionSignatures()
        {
            static const std::vector<FunctionSignature> signatures = {
                { "float", "abs", { { "float", "x" } } },
                { "float", "acos", { { "float", "x" } } },
                { "bool", "all", { { "float", "x" } } },
                { "bool", "any", { { "float", "x" } } },
                { "double", "asdouble", { { "uint", "lowbits" }, { "uint", "highbits" } } },
                { "float", "asfloat", { { "uint", "x" } } },
                { "float", "asin", { { "float", "x" } } },
                { "int", "asint", { { "float", "x" } } },
                { "uint", "asuint", { { "float", "x" } } },
                { "float", "atan", { { "float", "x" } } },
                { "float", "atan2", { { "float", "y" }, { "float", "x" } } },
                { "float", "ceil", { { "float", "x" } } },
                { "float", "clamp", { { "float", "x" }, { "float", "min" }, { "float", "max" } } },
                { "void", "clip", { { "float", "x" } } },
                { "float", "cos", { { "float", "x" } } },
                { "float", "cosh", { { "float", "x" } } },
                { "float3", "cross", { { "float3", "x" }, { "float3", "y" } } },
                { "float", "ddx", { { "float", "x" } } },
                { "float", "ddx_coarse", { { "float", "x" } } },
                { "float", "ddx_fine", { { "float", "x" } } },
                { "float", "ddy", { { "float", "x" } } },
                { "float", "ddy_coarse", { { "float", "x" } } },
                { "float", "ddy_fine", { { "float", "x" } } },
                { "float", "degrees", { { "float", "x" } } },
                { "float", "determinant", { { "float4x4", "m" } } },
                { "float", "distance", { { "float3", "x" }, { "float3", "y" } } },
                { "float", "dot", { { "float3", "x" }, { "float3", "y" } } },
                { "float", "exp", { { "float", "x" } } },
                { "float", "exp2", { { "float", "x" } } },
                { "float3", "faceforward", { { "float3", "n" }, { "float3", "i" }, { "float3", "ng" } } },
                { "uint", "firstbithigh", { { "uint", "x" } } },
                { "uint", "firstbitlow", { { "uint", "x" } } },
                { "float", "floor", { { "float", "x" } } },
                { "double", "fma", { { "double", "a" }, { "double", "b" }, { "double", "c" } } },
                { "float", "fmod", { { "float", "x" }, { "float", "y" } } },
                { "float", "frac", { { "float", "x" } } },
                { "float", "frexp", { { "float", "x" }, { "out float", "exp" } } },
                { "bool", "isfinite", { { "float", "x" } } },
                { "bool", "isinf", { { "float", "x" } } },
                { "bool", "isnan", { { "float", "x" } } },
                { "float", "ldexp", { { "float", "x" }, { "float", "exp" } } },
                { "float", "length", { { "float3", "x" } } },
                { "float3", "lerp", { { "float3", "x" }, { "float3", "y" }, { "float", "s" } } },
                { "float", "log", { { "float", "x" } } },
                { "float", "log10", { { "float", "x" } } },
                { "float", "log2", { { "float", "x" } } },
                { "float", "mad", { { "float", "a" }, { "float", "b" }, { "float", "c" } } },
                { "float", "max", { { "float", "x" }, { "float", "y" } } },
                { "float", "min", { { "float", "x" }, { "float", "y" } } },
                { "float", "modf", { { "float", "x" }, { "out float", "ip" } } },
                { "float4x4", "mul", { { "float4x4", "a" }, { "float4x4", "b" } } },
                { "float3", "normalize", { { "float3", "x" } } },
                { "float", "pow", { { "float", "x" }, { "float", "y" } } },
                { "float", "radians", { { "float", "x" } } },
                { "float", "rcp", { { "float", "x" } } },
                { "float3", "reflect", { { "float3", "i" }, { "float3", "n" } } },
                { "float3", "refract", { { "float3", "i" }, { "float3", "n" }, { "float", "eta" } } },
                { "uint", "reversebits", { { "uint", "x" } } },
                { "float", "round", { { "float", "x" } } },
                { "float", "rsqrt", { { "float", "x" } } },
                { "float", "saturate", { { "float", "x" } } },
                { "int", "sign", { { "float", "x" } } },
                { "float", "sin", { { "float", "x" } } },
                { "void", "sincos", { { "float", "x" }, { "out float", "s" }, { "out float", "c" } } },
                { "float", "sinh", { { "float", "x" } } },
                { "float", "smoothstep", { { "float", "min" }, { "float", "max" }, { "float", "x" } } },
                { "float", "sqrt", { { "float", "x" } } },
                { "float", "step", { { "float", "y" }, { "float", "x" } } },
                { "float", "tan", { { "float", "x" } } },
                { "float", "tanh", { { "float", "x" } } },
                { "float4x4", "transpose", { { "float4x4", "m" } } },
                { "float", "trunc", { { "float", "x" } } },
                { "void", "InterlockedAdd", { { "uint", "dest" }, { "uint", "value" }, { "out uint", "original" } } },
                { "void", "InterlockedAnd", { { "uint", "dest" }, { "uint", "value" }, { "out uint", "original" } } },
                { "void", "InterlockedCompareExchange", { { "uint", "dest" }, { "uint", "compare" }, { "uint", "value" }, { "out uint", "original" } } },
                { "void", "InterlockedExchange", { { "uint", "dest" }, { "uint", "value" }, { "out uint", "original" } } },
                { "void", "InterlockedMax", { { "uint", "dest" }, { "uint", "value" }, { "out uint", "original" } } },
                { "void", "InterlockedMin", { { "uint", "dest" }, { "uint", "value" }, { "out uint", "original" } } },
                { "void", "InterlockedOr", { { "uint", "dest" }, { "uint", "value" }, { "out uint", "original" } } },
                { "void", "InterlockedXor", { { "uint", "dest" }, { "uint", "value" }, { "out uint", "original" } } },
                { "void", "GroupMemoryBarrier", {} },
                { "void", "GroupMemoryBarrierWithGroupSync", {} },
                { "void", "AllMemoryBarrier", {} },
                { "void", "AllMemoryBarrierWithGroupSync", {} },
                { "void", "DeviceMemoryBarrier", {} },
                { "void", "DeviceMemoryBarrierWithGroupSync", {} },
                { "bool", "WaveActiveAllTrue", { { "bool", "expr" } } },
                { "bool", "WaveActiveAnyTrue", { { "bool", "expr" } } },
                { "uint4", "WaveActiveBallot", { { "bool", "expr" } } },
                { "float", "WaveActiveSum", { { "float", "expr" } } },
                { "uint", "WaveGetLaneIndex", {} },
                { "bool", "WaveIsFirstLane", {} },
            };
            return signatures;
        }

        TSNode fieldOf(TSNode node, const char* fieldName)
        {
            return ts_node_child_by_field_name(node, fieldName, static_cast<uint32_t>(std::strlen(fieldName)));
        }

        std::string_view nodeText(TSNode node, const std::string& source)
        {
            const uint32_t start = ts_node_start_byte(node);
            const uint32_t end = ts_node_end_byte(node);
            if (start > end || end > source.size())
                return {};
            return std::string_view(source).substr(start, end - start);
        }

        SourceRange rangeOf(TSNode node)
        {
            return SourceRange{ ts_node_start_byte(node), ts_node_end_byte(node) };
        }

        std::string normalizedTypeName(std::string_view text)
        {
            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
                text.remove_prefix(1);
            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
                text.remove_suffix(1);

            for (const std::string_view qualifier : { "const ", "static ", "uniform ", "in ", "out ", "inout " })
            {
                if (text.starts_with(qualifier))
                    text.remove_prefix(qualifier.size());
            }

            const std::size_t generic = text.find('<');
            const std::size_t array = text.find('[');
            const std::size_t pointer = text.find_first_of("*&");
            std::size_t end = text.size();
            for (const std::size_t candidate : { generic, array, pointer })
            {
                if (candidate != std::string_view::npos)
                    end = std::min(end, candidate);
            }
            while (end > 0 && std::isspace(static_cast<unsigned char>(text[end - 1])))
                --end;
            return std::string(text.substr(0, end));
        }

        void collectDeclaratorIdentifiers(TSNode node, std::vector<TSNode>& identifiers, std::size_t depth = 0)
        {
            if (ts_node_is_null(node) || depth > 64)
                return;

            const char* type = ts_node_type(node);
            if (std::strcmp(type, "identifier") == 0 || std::strcmp(type, "field_identifier") == 0)
            {
                identifiers.push_back(node);
                return;
            }

            if (TSNode inner = fieldOf(node, "declarator"); !ts_node_is_null(inner))
            {
                collectDeclaratorIdentifiers(inner, identifiers, depth + 1);
                return;
            }

            const uint32_t childCount = ts_node_named_child_count(node);
            for (uint32_t i = 0; i < childCount; ++i)
            {
                TSNode child = ts_node_named_child(node, i);
                const char* childType = ts_node_type(child);
                if (std::strstr(childType, "declarator") != nullptr ||
                    std::strcmp(childType, "identifier") == 0 ||
                    std::strcmp(childType, "field_identifier") == 0)
                {
                    collectDeclaratorIdentifiers(child, identifiers, depth + 1);
                }
            }
        }

        std::vector<TSNode> declaratorIdentifiers(TSNode declaration)
        {
            std::vector<TSNode> result;
            const uint32_t childCount = ts_node_child_count(declaration);
            for (uint32_t i = 0; i < childCount; ++i)
            {
                const char* fieldName = ts_node_field_name_for_child(declaration, i);
                if (fieldName && std::strcmp(fieldName, "declarator") == 0)
                    collectDeclaratorIdentifiers(ts_node_child(declaration, i), result);
            }

            if (result.empty())
            {
                if (TSNode declarator = fieldOf(declaration, "declarator"); !ts_node_is_null(declarator))
                    collectDeclaratorIdentifiers(declarator, result);
            }
            return result;
        }

        TSNode findFunctionDeclarator(TSNode node, std::size_t depth = 0)
        {
            if (ts_node_is_null(node) || depth > 64)
                return {};
            if (std::strcmp(ts_node_type(node), "function_declarator") == 0)
                return node;

            if (TSNode declarator = fieldOf(node, "declarator"); !ts_node_is_null(declarator))
            {
                if (TSNode found = findFunctionDeclarator(declarator, depth + 1); !ts_node_is_null(found))
                    return found;
            }

            const uint32_t childCount = ts_node_named_child_count(node);
            for (uint32_t i = 0; i < childCount; ++i)
            {
                TSNode child = ts_node_named_child(node, i);
                if (std::strstr(ts_node_type(child), "declarator") != nullptr)
                {
                    if (TSNode found = findFunctionDeclarator(child, depth + 1); !ts_node_is_null(found))
                        return found;
                }
            }
            return {};
        }

        std::string declarationType(TSNode node, const std::string& source)
        {
            TSNode typeNode = fieldOf(node, "type");
            return ts_node_is_null(typeNode) ? std::string{} : normalizedTypeName(nodeText(typeNode, source));
        }

        void addToken(SymbolIndex& index, TSNode node, SymbolKind kind)
        {
            if (ts_node_is_null(node) || ts_node_is_missing(node) ||
                ts_node_end_byte(node) <= ts_node_start_byte(node))
                return;
            index.semanticTokens.push_back(SemanticToken{
                ts_node_start_byte(node),
                ts_node_end_byte(node),
                kind,
            });
        }

        void addSymbol(
            SymbolIndex& index,
            std::size_t scopeIndex,
            TSNode identifier,
            SymbolKind kind,
            std::string typeName,
            const std::string& source
        )
        {
            if (ts_node_is_null(identifier) || ts_node_is_missing(identifier) ||
                ts_node_end_byte(identifier) <= ts_node_start_byte(identifier))
                return;
            const std::string_view name = nodeText(identifier, source);
            if (name.empty())
                return;
            index.scopes[scopeIndex].symbols.push_back(Symbol{
                std::string(name),
                kind,
                std::move(typeName),
                rangeOf(identifier),
                false,
            });
            addToken(index, identifier, kind);
        }

        std::size_t addScope(SymbolIndex& index, SourceRange range, std::size_t parent)
        {
            const std::size_t scopeIndex = index.scopes.size();
            index.scopes.push_back(Scope{ range, parent, {}, {} });
            index.scopes[parent].children.push_back(scopeIndex);
            return scopeIndex;
        }

        void collectParameters(
            TSNode node,
            std::size_t functionScope,
            const std::string& source,
            SymbolIndex& index,
            std::size_t depth = 0
        )
        {
            if (ts_node_is_null(node) || depth > 128)
                return;
            if (std::strcmp(ts_node_type(node), "parameter_declaration") == 0)
            {
                const std::string typeName = declarationType(node, source);
                for (TSNode identifier : declaratorIdentifiers(node))
                    addSymbol(index, functionScope, identifier, SymbolKind::Parameter, typeName, source);
                return;
            }

            const uint32_t childCount = ts_node_named_child_count(node);
            for (uint32_t i = 0; i < childCount; ++i)
                collectParameters(ts_node_named_child(node, i), functionScope, source, index, depth + 1);
        }

        // typeName on an EnumMember symbol holds the *owning* enum's name (there's no meaningful
        // "own type" for an enumerator otherwise) — this is the one SymbolKind where typeName means
        // "declared in", not "is a", matching what hover needs to render "EnumName::member".
        void collectEnumMembers(
            TSNode node,
            std::size_t currentScope,
            const std::string& source,
            const std::string& enumTypeName,
            SymbolIndex& index,
            std::size_t depth = 0
        )
        {
            if (ts_node_is_null(node) || depth > 128)
                return;
            if (std::strcmp(ts_node_type(node), "enumerator") == 0)
            {
                if (TSNode name = fieldOf(node, "name"); !ts_node_is_null(name))
                    addSymbol(index, currentScope, name, SymbolKind::EnumMember, enumTypeName, source);
                return;
            }

            const uint32_t childCount = ts_node_named_child_count(node);
            for (uint32_t i = 0; i < childCount; ++i)
                collectEnumMembers(ts_node_named_child(node, i), currentScope, source, enumTypeName, index, depth + 1);
        }

        void collectDeclarations(
            TSNode node,
            std::size_t currentScope,
            const std::string& source,
            SymbolIndex& index,
            std::size_t depth,
            bool reuseCompoundScope = false
        )
        {
            if (depth > 4096)
                return;

            const char* type = ts_node_type(node);
            if (std::strcmp(type, "function_definition") == 0)
            {
                TSNode functionDeclarator = findFunctionDeclarator(fieldOf(node, "declarator"));
                if (!ts_node_is_null(functionDeclarator))
                {
                    std::vector<TSNode> names;
                    collectDeclaratorIdentifiers(fieldOf(functionDeclarator, "declarator"), names);
                    if (!names.empty())
                        addSymbol(index, currentScope, names.front(), SymbolKind::Function, declarationType(node, source), source);
                }

                const std::size_t functionScope = addScope(index, rangeOf(node), currentScope);
                collectParameters(functionDeclarator, functionScope, source, index);
                if (TSNode body = fieldOf(node, "body"); !ts_node_is_null(body))
                    collectDeclarations(body, functionScope, source, index, depth + 1, true);
                return;
            }

            if (std::strcmp(type, "compound_statement") == 0 && !reuseCompoundScope)
            {
                currentScope = addScope(index, rangeOf(node), currentScope);
            }

            if (std::strcmp(type, "struct_specifier") == 0)
            {
                TSNode nameNode = fieldOf(node, "name");
                TSNode bodyNode = fieldOf(node, "body");
                if (!ts_node_is_null(nameNode))
                    addSymbol(index, currentScope, nameNode, SymbolKind::Type, {}, source);

                if (!ts_node_is_null(nameNode) && !ts_node_is_null(bodyNode))
                {
                    std::vector<Symbol>& members = index.typeMembers[std::string(nodeText(nameNode, source))];
                    const uint32_t childCount = ts_node_named_child_count(bodyNode);
                    for (uint32_t i = 0; i < childCount; ++i)
                    {
                        TSNode fieldDeclaration = ts_node_named_child(bodyNode, i);
                        if (std::strcmp(ts_node_type(fieldDeclaration), "field_declaration") != 0)
                            continue;
                        const std::string fieldType = declarationType(fieldDeclaration, source);
                        for (TSNode identifier : declaratorIdentifiers(fieldDeclaration))
                        {
                            members.push_back(Symbol{
                                std::string(nodeText(identifier, source)),
                                SymbolKind::Field,
                                fieldType,
                                rangeOf(identifier),
                                false,
                            });
                            addToken(index, identifier, SymbolKind::Field);
                        }
                    }
                }
                return;
            }

            if (std::strcmp(type, "enum_specifier") == 0)
            {
                std::string enumTypeName;
                if (TSNode nameNode = fieldOf(node, "name"); !ts_node_is_null(nameNode))
                {
                    addSymbol(index, currentScope, nameNode, SymbolKind::Type, {}, source);
                    enumTypeName = std::string(nodeText(nameNode, source));
                }
                collectEnumMembers(node, currentScope, source, enumTypeName, index);
                return;
            }

            if (std::strcmp(type, "preproc_def") == 0 || std::strcmp(type, "preproc_function_def") == 0)
            {
                if (TSNode name = fieldOf(node, "name"); !ts_node_is_null(name))
                    addSymbol(index, currentScope, name, SymbolKind::Macro, {}, source);
            }
            else if (std::strcmp(type, "declaration") == 0)
            {
                const std::string declaredType = declarationType(node, source);
                const uint32_t childCount = ts_node_child_count(node);
                for (uint32_t i = 0; i < childCount; ++i)
                {
                    const char* fieldName = ts_node_field_name_for_child(node, i);
                    if (!fieldName || std::strcmp(fieldName, "declarator") != 0)
                        continue;

                    TSNode declarator = ts_node_child(node, i);
                    const SymbolKind kind = ts_node_is_null(findFunctionDeclarator(declarator))
                        ? (currentScope == 0 ? SymbolKind::GlobalVariable : SymbolKind::LocalVariable)
                        : SymbolKind::Function;
                    for (TSNode identifier : [&]
                    {
                        std::vector<TSNode> ids;
                        collectDeclaratorIdentifiers(declarator, ids);
                        return ids;
                    }())
                    {
                        addSymbol(index, currentScope, identifier, kind, declaredType, source);
                    }
                }
            }

            const uint32_t childCount = ts_node_named_child_count(node);
            for (uint32_t i = 0; i < childCount; ++i)
            {
                TSNode child = ts_node_named_child(node, i);

                // tree-sitter-slang mis-parses "TypeName identifier;" as an orphan type_identifier
                // followed by an expression_statement wrapping just the identifier, instead of a
                // single declaration node, once other top-level constructs (enums, macros) have
                // been scanned since the type's struct_specifier. Recover the intended declaration
                // from that shape rather than silently losing the symbol.
                if (std::strcmp(ts_node_type(child), "type_identifier") == 0 && i + 1 < childCount)
                {
                    TSNode next = ts_node_named_child(node, i + 1);
                    if (std::strcmp(ts_node_type(next), "expression_statement") == 0 &&
                        ts_node_named_child_count(next) == 1)
                    {
                        TSNode identifier = ts_node_named_child(next, 0);
                        if (std::strcmp(ts_node_type(identifier), "identifier") == 0)
                        {
                            const SymbolKind kind =
                                currentScope == 0 ? SymbolKind::GlobalVariable : SymbolKind::LocalVariable;
                            addSymbol(index, currentScope, identifier, kind, std::string(nodeText(child, source)), source);
                            ++i;
                            continue;
                        }
                    }
                }

                collectDeclarations(child, currentScope, source, index, depth + 1);
            }
        }

        std::uint64_t rangeKey(SourceRange range)
        {
            return (static_cast<std::uint64_t>(range.startByte) << 32) | range.endByte;
        }

        std::size_t innermostScopeAt(const SymbolIndex& index, std::uint32_t byteOffset)
        {
            if (index.scopes.empty())
                return 0;

            std::size_t current = 0;
            while (!index.scopes[current].children.empty())
            {
                const std::vector<std::size_t>& children = index.scopes[current].children;
                auto child = std::upper_bound(
                    children.begin(),
                    children.end(),
                    byteOffset,
                    [&index](std::uint32_t position, std::size_t childIndex)
                    {
                        return position < index.scopes[childIndex].range.startByte;
                    }
                );
                if (child == children.begin())
                    break;
                --child;
                if (!index.scopes[*child].range.contains(byteOffset))
                    break;
                current = *child;
            }
            return current;
        }

        const Symbol* resolveVisibleSymbol(
            const SymbolIndex& index,
            std::string_view name,
            std::uint32_t byteOffset,
            std::optional<SymbolKind> requiredKind = std::nullopt
        )
        {
            if (index.scopes.empty())
                return nullptr;

            std::optional<std::size_t> scopeIndex = innermostScopeAt(index, byteOffset);
            while (scopeIndex)
            {
                const Scope& scope = index.scopes[*scopeIndex];
                for (auto symbol = scope.symbols.rbegin(); symbol != scope.symbols.rend(); ++symbol)
                {
                    if (symbol->name != name || (requiredKind && symbol->kind != *requiredKind))
                        continue;
                    if (symbol->external || *scopeIndex == 0 || symbol->declaration.startByte <= byteOffset)
                        return &*symbol;
                }
                scopeIndex = scope.parent;
            }
            return nullptr;
        }

        void collectSemanticUsages(
            TSNode node,
            const std::string& source,
            SymbolIndex& index,
            const std::unordered_set<std::uint64_t>& declarationRanges,
            std::size_t depth
        )
        {
            if (depth > 4096)
                return;

            const char* type = ts_node_type(node);
            const SourceRange range = rangeOf(node);
            // tree-sitter-slang parses "EnumType::Member" as a qualified_identifier whose scope
            // field is a namespace_identifier — not type_identifier — even though this grammar has
            // no actual namespaces, only enum-qualified access. Treat it exactly like type_identifier
            // so the left side of "::" still resolves and colors as the Type it always is here.
            if ((std::strcmp(type, "identifier") == 0 ||
                 std::strcmp(type, "type_identifier") == 0 ||
                 std::strcmp(type, "namespace_identifier") == 0 ||
                 std::strcmp(type, "field_identifier") == 0) &&
                !declarationRanges.contains(rangeKey(range)))
            {
                const std::string_view name = nodeText(node, source);
                if (std::strcmp(type, "field_identifier") == 0)
                {
                    const bool builtinTextureMethod = builtinTextureMethodNames().contains(name);
                    addToken(index, node, builtinTextureMethod ? SymbolKind::Function : SymbolKind::Field);
                }
                else
                {
                    const std::optional<SymbolKind> requiredKind =
                        (std::strcmp(type, "type_identifier") == 0 || std::strcmp(type, "namespace_identifier") == 0)
                        ? std::optional<SymbolKind>(SymbolKind::Type)
                        : std::nullopt;
                    if (const Symbol* symbol = resolveVisibleSymbol(index, name, range.startByte, requiredKind))
                        addToken(index, node, symbol->kind);
                }
                return;
            }

            const uint32_t childCount = ts_node_named_child_count(node);
            for (uint32_t i = 0; i < childCount; ++i)
                collectSemanticUsages(
                    ts_node_named_child(node, i),
                    source,
                    index,
                    declarationRanges,
                    depth + 1
                );
        }

        void injectExternalSymbols(SymbolIndex& target, const SymbolIndex& external)
        {
            if (!target.scopes.empty() && !external.scopes.empty())
            {
                auto& symbols = target.scopes.front().symbols;
                const auto& externalSymbols = external.scopes.front().symbols;
                symbols.insert(symbols.begin(), externalSymbols.begin(), externalSymbols.end());
            }
            for (const auto& [typeName, externalMembers] : external.typeMembers)
            {
                auto& members = target.typeMembers[typeName];
                members.insert(members.begin(), externalMembers.begin(), externalMembers.end());
            }
        }

        // Extracts declarations + semantic-usage tokens from an already-parsed tree's root. Does
        // NOT touch timings->treeParse or timings->treeSitter — the caller owns the parse step
        // (which may be incremental) and should set treeSitter itself once it knows the total
        // (parse + this) elapsed time.
        SymbolIndex scanSymbolIndexFromRoot(
            TSNode root,
            const std::string& text,
            const SymbolIndex* external,
            AnalysisTimings* timings
        )
        {
            SymbolIndex result;
            result.scopes.push_back(Scope{
                SourceRange{ 0, static_cast<std::uint32_t>(text.size()) },
                std::nullopt,
                {},
                {},
            });

            const auto declarationsStart = std::chrono::steady_clock::now();
            collectDeclarations(root, 0, text, result, 0);
            if (external)
                injectExternalSymbols(result, *external);
            if (timings)
            {
                timings->declarationScan = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - declarationsStart
                );
            }
            const auto semanticStart = std::chrono::steady_clock::now();
            std::unordered_set<std::uint64_t> declarationRanges;
            declarationRanges.reserve(result.semanticTokens.size());
            for (const SemanticToken& token : result.semanticTokens)
                declarationRanges.insert(rangeKey(SourceRange{ token.startByte, token.endByte }));
            collectSemanticUsages(root, text, result, declarationRanges, 0);
            if (timings)
            {
                timings->semanticScan = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - semanticStart
                );
            }

            const auto finalizeStart = std::chrono::steady_clock::now();
            std::ranges::sort(result.semanticTokens, [](const SemanticToken& left, const SemanticToken& right)
            {
                if (left.startByte != right.startByte)
                    return left.startByte < right.startByte;
                return left.endByte < right.endByte;
            });
            result.semanticTokens.erase(
                std::unique(result.semanticTokens.begin(), result.semanticTokens.end()),
                result.semanticTokens.end()
            );
            if (timings)
            {
                timings->tokenFinalize = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - finalizeStart
                );
            }
            return result;
        }

        // Convenience wrapper: always does a fresh, non-incremental parse (no previous tree to
        // reuse). Used for the one-off preamble scan, where there's no editing session and thus
        // nothing to be incremental about.
        SymbolIndex scanSymbolIndex(
            TSParser* parser,
            const std::string& text,
            const SymbolIndex* external = nullptr,
            AnalysisTimings* timings = nullptr
        )
        {
            const auto scanStart = std::chrono::steady_clock::now();
            SymbolIndex result;
            if (!parser || text.size() > UINT32_MAX)
                return result;

            const auto parseStart = std::chrono::steady_clock::now();
            TSTree* tree = ts_parser_parse_string(parser, nullptr, text.c_str(), static_cast<uint32_t>(text.size()));
            if (timings)
            {
                timings->treeParse = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - parseStart
                );
            }
            if (tree)
            {
                result = scanSymbolIndexFromRoot(ts_tree_root_node(tree), text, external, timings);
                ts_tree_delete(tree);
            }
            else
            {
                result.scopes.push_back(Scope{
                    SourceRange{ 0, static_cast<std::uint32_t>(text.size()) },
                    std::nullopt,
                    {},
                    {},
                });
            }
            if (timings)
            {
                timings->treeSitter = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - scanStart
                );
            }
            return result;
        }

        std::string_view lineAt(std::string_view source, std::size_t requestedLine)
        {
            std::size_t start = 0;
            for (std::size_t line = 0; line < requestedLine; ++line)
            {
                const std::size_t newline = source.find('\n', start);
                if (newline == std::string_view::npos)
                    return {};
                start = newline + 1;
            }

            std::size_t end = source.find('\n', start);
            if (end == std::string_view::npos)
                end = source.size();
            if (end > start && source[end - 1] == '\r')
                --end;
            return source.substr(start, end - start);
        }

        std::size_t nextUtf8CodePoint(std::string_view text, std::size_t index)
        {
            if (index >= text.size())
                return text.size();

            const unsigned char lead = static_cast<unsigned char>(text[index]);
            std::size_t length = 1;
            if ((lead & 0xE0) == 0xC0)
                length = 2;
            else if ((lead & 0xF0) == 0xE0)
                length = 3;
            else if ((lead & 0xF8) == 0xF0)
                length = 4;

            if (index + length > text.size())
                return index + 1;
            for (std::size_t i = 1; i < length; ++i)
            {
                if ((static_cast<unsigned char>(text[index + i]) & 0xC0) != 0x80)
                    return index + 1;
            }
            return index + length;
        }

        const Symbol* findTypeMember(
            const SymbolIndex& index,
            std::string_view typeName,
            std::string_view memberName
        )
        {
            const std::string normalized = normalizedTypeName(typeName);
            const auto type = index.typeMembers.find(normalized);
            if (type == index.typeMembers.end())
                return nullptr;

            for (auto member = type->second.rbegin(); member != type->second.rend(); ++member)
            {
                if (member->name == memberName)
                    return &*member;
            }
            return nullptr;
        }

        std::optional<std::string_view> memberReceiverExpression(
            std::string_view source,
            std::size_t cursorByte
        )
        {
            std::size_t end = std::min(cursorByte, source.size());
            while (end > 0 && std::isspace(static_cast<unsigned char>(source[end - 1])))
                --end;
            if (end == 0 || source[end - 1] != '.')
                return std::nullopt;

            --end;
            while (end > 0 && std::isspace(static_cast<unsigned char>(source[end - 1])))
                --end;
            const std::size_t expressionEnd = end;
            int squareDepth = 0;
            int parenDepth = 0;
            while (end > 0)
            {
                const char c = source[end - 1];
                if (c == ']')
                    ++squareDepth;
                else if (c == '[')
                {
                    if (squareDepth == 0)
                        break;
                    --squareDepth;
                }
                else if (c == ')')
                    ++parenDepth;
                else if (c == '(')
                {
                    if (parenDepth == 0)
                        break;
                    --parenDepth;
                }
                else if (squareDepth == 0 && parenDepth == 0 &&
                         std::isspace(static_cast<unsigned char>(c)))
                {
                    std::size_t left = end - 1;
                    while (left > 0 && std::isspace(static_cast<unsigned char>(source[left - 1])))
                        --left;
                    std::size_t right = end;
                    while (right < expressionEnd && std::isspace(static_cast<unsigned char>(source[right])))
                        ++right;
                    const bool adjacentToDot =
                        (left > 0 && source[left - 1] == '.') ||
                        (right < expressionEnd && source[right] == '.');
                    if (!adjacentToDot)
                        break;
                }
                else if (squareDepth == 0 && parenDepth == 0 &&
                         (c == '\n' || c == '\r' || c == ';' || c == '=' || c == ',' ||
                          c == '{' || c == '}' || c == '+' || c == '-' || c == '*' ||
                          c == '/' || c == '!' || c == '<' || c == '>' || c == '?' || c == ':'))
                {
                    break;
                }
                --end;
            }

            while (end < expressionEnd && std::isspace(static_cast<unsigned char>(source[end])))
                ++end;
            if (end == expressionEnd)
                return std::nullopt;
            return source.substr(end, expressionEnd - end);
        }

        std::optional<std::string> resolveExpressionType(
            const SymbolIndex& index,
            std::string_view expression,
            std::uint32_t cursorByte,
            std::size_t recursionDepth = 0
        )
        {
            if (recursionDepth > 16)
                return std::nullopt;

            std::string compact;
            compact.reserve(expression.size());
            for (char c : expression)
            {
                if (!std::isspace(static_cast<unsigned char>(c)))
                    compact.push_back(c);
            }
            if (compact.empty())
                return std::nullopt;

            std::size_t position = 0;
            std::string currentType;
            if (compact.front() == '(')
            {
                int depth = 1;
                std::size_t close = 1;
                for (; close < compact.size() && depth > 0; ++close)
                {
                    if (compact[close] == '(')
                        ++depth;
                    else if (compact[close] == ')')
                        --depth;
                }
                if (depth != 0)
                    return std::nullopt;
                const auto nested = resolveExpressionType(
                    index,
                    std::string_view(compact).substr(1, close - 2),
                    cursorByte,
                    recursionDepth + 1
                );
                if (!nested)
                    return std::nullopt;
                currentType = *nested;
                position = close;
            }
            else
            {
                if (!(std::isalpha(static_cast<unsigned char>(compact.front())) || compact.front() == '_'))
                    return std::nullopt;
                std::size_t identifierEnd = 1;
                while (identifierEnd < compact.size())
                {
                    const unsigned char c = static_cast<unsigned char>(compact[identifierEnd]);
                    if (!(std::isalnum(c) || c == '_'))
                        break;
                    ++identifierEnd;
                }

                const std::string_view name(compact.data(), identifierEnd);
                const Symbol* symbol = resolveVisibleSymbol(index, name, cursorByte);
                if (!symbol || symbol->typeName.empty())
                    return std::nullopt;
                currentType = symbol->typeName;
                position = identifierEnd;
            }

            while (position < compact.size())
            {
                if (compact[position] == '[')
                {
                    int depth = 1;
                    ++position;
                    while (position < compact.size() && depth > 0)
                    {
                        if (compact[position] == '[')
                            ++depth;
                        else if (compact[position] == ']')
                            --depth;
                        ++position;
                    }
                    if (depth != 0)
                        return std::nullopt;
                    continue;
                }

                if (compact[position] == '(')
                {
                    int depth = 1;
                    ++position;
                    while (position < compact.size() && depth > 0)
                    {
                        if (compact[position] == '(')
                            ++depth;
                        else if (compact[position] == ')')
                            --depth;
                        ++position;
                    }
                    if (depth != 0)
                        return std::nullopt;
                    continue;
                }

                if (compact[position] != '.')
                    return std::nullopt;
                ++position;
                const std::size_t memberStart = position;
                while (position < compact.size())
                {
                    const unsigned char c = static_cast<unsigned char>(compact[position]);
                    if (!(std::isalnum(c) || c == '_'))
                        break;
                    ++position;
                }
                if (memberStart == position)
                    return std::nullopt;

                const Symbol* member = findTypeMember(
                    index,
                    currentType,
                    std::string_view(compact).substr(memberStart, position - memberStart)
                );
                if (!member || member->typeName.empty())
                    return std::nullopt;
                currentType = member->typeName;
            }
            return normalizedTypeName(currentType);
        }

        int completionMatchQuality(std::string_view name, std::string_view partial)
        {
            if (partial.empty())
                return 0;
            if (name.starts_with(partial))
                return name == partial ? 0 : 1;

            auto lower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
            if (partial.size() <= name.size())
            {
                bool prefix = true;
                for (std::size_t i = 0; i < partial.size(); ++i)
                    prefix &= lower(static_cast<unsigned char>(name[i])) == lower(static_cast<unsigned char>(partial[i]));
                if (prefix)
                    return 2;
            }

            std::string lowerName(name);
            std::string lowerPartial(partial);
            std::ranges::transform(lowerName, lowerName.begin(), lower);
            std::ranges::transform(lowerPartial, lowerPartial.begin(), lower);
            return lowerName.find(lowerPartial) != std::string::npos ? 3 : -1;
        }

        int completionCategoryPriority(SymbolKind kind)
        {
            switch (kind)
            {
            case SymbolKind::Parameter: return 0;
            case SymbolKind::LocalVariable: return 1;
            case SymbolKind::GlobalVariable: return 2;
            case SymbolKind::Field: return 3;
            case SymbolKind::Function: return 4;
            case SymbolKind::Type: return 5;
            case SymbolKind::EnumMember: return 6;
            case SymbolKind::Macro: return 7;
            }
            return 8;
        }

        TSInputEdit toTSInputEdit(const SourceEdit& edit)
        {
            TSInputEdit input{};
            input.start_byte = edit.startByte;
            input.old_end_byte = edit.oldEndByte;
            input.new_end_byte = edit.newEndByte;
            input.start_point = TSPoint{ edit.startPoint.row, edit.startPoint.column };
            input.old_end_point = TSPoint{ edit.oldEndPoint.row, edit.oldEndPoint.column };
            input.new_end_point = TSPoint{ edit.newEndPoint.row, edit.newEndPoint.column };
            return input;
        }

        struct SymbolAnalyzerImpl
        {
            explicit SymbolAnalyzerImpl(std::string sourcePreamble, int /*sourcePreambleLineCount*/)
                : preamble(std::move(sourcePreamble))
            {
                parser = ts_parser_new();
                if (!parser)
                {
                    initializationFailure = "tree-sitter parser creation failed";
                    return;
                }
                const TSLanguage* language = tree_sitter_slang();
                if (!language || !ts_parser_set_language(parser, language))
                {
                    initializationFailure = "tree-sitter Slang language is incompatible with the parser";
                    return;
                }

                externalIndex = scanSymbolIndex(parser, preamble);
                if (!externalIndex.scopes.empty())
                {
                    for (Symbol& symbol : externalIndex.scopes.front().symbols)
                        symbol.external = true;
                }
                for (auto& [typeName, members] : externalIndex.typeMembers)
                {
                    (void)typeName;
                    for (Symbol& member : members)
                        member.external = true;
                }
                externalIndex.semanticTokens.clear();
            }

            ~SymbolAnalyzerImpl()
            {
                if (previousTree)
                    ts_tree_delete(previousTree);
                if (parser)
                    ts_parser_delete(parser);
            }

            // Incrementally (or fully) reparses source into previousTree. Cheap — safe to call on
            // every keystroke, including rapid key-repeat bursts (e.g. holding Backspace down),
            // where the actual bottleneck is scanCurrentTree()'s full-document walk, not this.
            bool updateTree(
                const std::string& source,
                const std::vector<SourceEdit>& edits,
                AnalysisTimings* timings,
                std::string* failureMessage
            )
            {
                if (!initializationFailure.empty())
                {
                    if (failureMessage)
                        *failureMessage = initializationFailure;
                    return false;
                }

                if (source.size() > UINT32_MAX)
                {
                    if (failureMessage)
                        *failureMessage = "source too large for tree-sitter's 32-bit byte offsets";
                    return false;
                }

                // Only trust the edit hints when there's a previous tree to apply them to — an
                // empty list (first call, or the caller couldn't verify its edits) means "do a
                // full parse", which is what happens when oldTree below ends up null either way.
                const bool incremental = !edits.empty() && previousTree != nullptr;
                if (incremental)
                {
                    for (const SourceEdit& edit : edits)
                    {
                        TSInputEdit input = toTSInputEdit(edit);
                        ts_tree_edit(previousTree, &input);
                    }
                }
                else if (previousTree)
                {
                    ts_tree_delete(previousTree);
                    previousTree = nullptr;
                }

                const auto parseStart = std::chrono::steady_clock::now();
                TSTree* newTree = ts_parser_parse_string(
                    parser, previousTree, source.c_str(), static_cast<uint32_t>(source.size())
                );
                if (timings)
                {
                    timings->treeParse = std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - parseStart
                    );
                }

                if (previousTree)
                    ts_tree_delete(previousTree);
                previousTree = newTree;
                return incremental;
            }

            // Extracts the symbol table from previousTree (as last produced by updateTree())
            // against source — source must match what updateTree() most recently parsed, or the
            // result is meaningless. This is the expensive full-document walk (declarationScan +
            // semanticScan); callers under rapid edit bursts should call updateTree() every
            // keystroke but debounce this.
            SymbolAnalysisResult scanCurrentTree(const std::string& source)
            {
                const auto totalStart = std::chrono::steady_clock::now();
                SymbolAnalysisResult result;

                if (previousTree)
                {
                    result.symbolIndex = scanSymbolIndexFromRoot(
                        ts_tree_root_node(previousTree), source, &externalIndex, &result.timings
                    );
                }
                else
                {
                    result.symbolIndex.scopes.push_back(Scope{
                        SourceRange{ 0, static_cast<std::uint32_t>(source.size()) },
                        std::nullopt,
                        {},
                        {},
                    });
                }

                result.timings.treeSitter = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - totalStart
                );
                result.timings.total = result.timings.treeSitter;
                return result;
            }

            SymbolAnalysisResult analyze(const std::string& source, const std::vector<SourceEdit>& edits)
            {
                SymbolAnalysisResult result;
                result.incrementalParse = updateTree(source, edits, &result.timings, &result.failureMessage);
                if (!result.failureMessage.empty())
                    return result;

                SymbolAnalysisResult scanned = scanCurrentTree(source);
                result.symbolIndex = std::move(scanned.symbolIndex);
                result.failureMessage = std::move(scanned.failureMessage);
                // scanCurrentTree() measured its own elapsed time from a fresh start; fold its
                // phase timings in on top of updateTree()'s treeParse, and recompute the totals
                // so they still cover parse + scan together as callers of the combined analyze()
                // API expect (see testSemanticIndexAndCompletion's timing assertions).
                result.timings.declarationScan = scanned.timings.declarationScan;
                result.timings.semanticScan = scanned.timings.semanticScan;
                result.timings.tokenFinalize = scanned.timings.tokenFinalize;
                result.timings.treeSitter = result.timings.treeParse +
                    result.timings.declarationScan +
                    result.timings.semanticScan +
                    result.timings.tokenFinalize;
                result.timings.total = result.timings.treeSitter;
                return result;
            }

            std::string preamble;
            TSParser* parser = nullptr;
            TSTree* previousTree = nullptr;
            SymbolIndex externalIndex;
            std::string initializationFailure;
        };

        // slangd reports positions as UTF-16 code units per LSP's negotiated positionEncoding.
        // For ASCII source (the only kind this editor can render meaningfully today — see
        // CodeEditorTest's Consolas-only glyph range) that's numerically identical to a UTF-8
        // byte offset, so no conversion is performed here; a non-ASCII diagnostic column would be
        // off by the UTF-16/UTF-8 width difference until this gets real position-encoding-aware
        // conversion.
        SlangDiagnostic convertLspDiagnostic(const LspDiagnostic& diagnostic)
        {
            SlangDiagnostic result;
            result.line = static_cast<int>(diagnostic.range.start.line);
            result.column = static_cast<int>(diagnostic.range.start.character);
            const int length =
                static_cast<int>(diagnostic.range.end.character) - static_cast<int>(diagnostic.range.start.character);
            result.length = length > 0 ? length : 1;
            switch (diagnostic.severity)
            {
            case DiagnosticSeverity::Error:
                result.severity = SlangDiagnosticSeverity::Error;
                break;
            case DiagnosticSeverity::Warning:
                result.severity = SlangDiagnosticSeverity::Warning;
                break;
            case DiagnosticSeverity::Information:
            case DiagnosticSeverity::Hint:
                result.severity = SlangDiagnosticSeverity::Info;
                break;
            }
            result.code = diagnostic.code;
            result.message = diagnostic.message;
            return result;
        }

        // Drives Slang's own `slangd` language-server process (see LspClient) instead of the
        // in-process Slang compiler API used by SlangBackend — same underlying compiler, so
        // diagnostic quality is identical, but this runs out-of-process and gets diagnostics as
        // an asynchronous push rather than a direct call return. Detection happens once per
        // Backend lifetime (constructor); if slangd isn't found or fails to start, every analyze()
        // call reports no diagnostics instead of retrying detection on every request.
        // slangd was observed (empirically, across repeated manual test runs against the same
        // hardcoded synthetic URI) to sometimes carry per-URI document state across separate
        // process launches — a fresh process reported "declaration conflicts with existing
        // declaration" / "ambiguous reference" for types that only the current session's document
        // declared, which only makes sense if a prior session's document was still indexed
        // somewhere. Each SlangdBackend therefore gets its own never-before-used URI so it can
        // never collide with any earlier session's leftover state, no matter how slangd caches.
        std::string makeUniqueSlangdDocumentUri()
        {
            static std::atomic<int> counter{ 0 };
            std::random_device randomDevice;
            const unsigned int entropy = randomDevice();
            return "file:///C%3A/HyperEngine_SlangProvider/session_" +
                std::to_string(entropy) + "_" + std::to_string(counter.fetch_add(1)) + ".slang";
        }

        struct SlangdBackend
        {
            // Generous relative to observed real compile times (session creation ~50ms,
            // publish-after-edit observed around ~1.1s worst case in manual testing) — a
            // timeout here just means "reuse the last-known diagnostics," so erring high costs
            // latency on the rare case they genuinely changed, not correctness.
            static constexpr int kDiagnosticsWaitMs = 1500;

            // Shares the process-wide slangd.exe (see SlangdSession::acquire()) instead of
            // spawning a private one. documentUri is still unique per SlangdBackend (see
            // makeUniqueSlangdDocumentUri), so concurrently open documents on the same shared
            // client never collide.
            explicit SlangdBackend(std::string sourcePreamble, int sourcePreambleLineCount)
                : preamble(std::move(sourcePreamble)), preambleLineCount(sourcePreambleLineCount), sharedSession(SlangdSession::acquire())
            {
                client = sharedSession ? sharedSession->getClient() : nullptr;
                if (!client)
                    initializationFailure = "slangd failed to start; diagnostics disabled";
            }

            ~SlangdBackend()
            {
                // SlangdSession's slangd.exe outlives this backend, so a document left open here stays indexed there for the rest of the session.
                if (client && opened)
                    client->didClose(documentUri);
            }

            DiagnosticsResult analyze(std::uint64_t version, const std::string& source)
            {
                const auto totalStart = std::chrono::steady_clock::now();
                DiagnosticsResult result;
                result.documentVersion = version;

                if (!client)
                {
                    result.failureMessage = initializationFailure;
                    result.timings.total = std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - totalStart
                    );
                    return result;
                }

                const std::string combined = preamble + source;

                const auto sessionStart = std::chrono::steady_clock::now();
                const auto [priorDiagnostics, priorGeneration] = client->diagnosticsSnapshot(documentUri);
                // Deliberately didClose+didOpen on every request rather than didChange: slangd
                // declared TextDocumentSyncKind.Incremental (2) in its initialize response, but a
                // rangeless (whole-document) didChange was observed (empirically) to leave the
                // previous version's declarations still indexed alongside the new ones —
                // "declaration of 'X' conflicts with existing declaration" for types the new
                // document alone declares once. Since every analyze() call already resends the
                // full preamble+fragment text regardless (no incremental diffing is done here),
                // close+reopen achieves the same "whole document replaced" effect without relying
                // on a didChange shape this server doesn't handle correctly.
                if (opened)
                    client->didClose(documentUri);
                client->didOpen(documentUri, combined, static_cast<int>(version));
                opened = true;
                result.timings.slangSession = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - sessionStart
                );

                // slangd only republishes diagnostics when the computed set actually differs from
                // what it last reported for this document (confirmed empirically: re-analyzing
                // still-clean code, or the very first open of clean code, produces no push at
                // all) — so a bounded wait that times out here does NOT mean anything failed, it
                // means the diagnostics are still whatever they were before this edit (most
                // commonly: still none). Fall back to the last-known snapshot in that case rather
                // than reporting a spurious failure on every clean keystroke.
                const auto compileStart = std::chrono::steady_clock::now();
                const auto pushed = client->waitForDiagnostics(documentUri, priorGeneration, kDiagnosticsWaitMs);
                result.timings.slangCompile = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - compileStart
                );

                const std::vector<LspDiagnostic>& rawDiagnostics = pushed ? pushed->first : priorDiagnostics;

                const auto diagnosticParseStart = std::chrono::steady_clock::now();
                result.diagnostics.reserve(rawDiagnostics.size());
                for (const LspDiagnostic& diagnostic : rawDiagnostics)
                {
                    SlangDiagnostic converted = convertLspDiagnostic(diagnostic);
                    converted.line -= preambleLineCount;
                    // Diagnostics located inside the preamble itself aren't actionable to a user
                    // who only edits the fragment appended after it — drop them.
                    if (converted.line >= 0)
                        result.diagnostics.push_back(std::move(converted));
                }
                result.timings.diagnosticParse = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - diagnosticParseStart
                );

                result.timings.total = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - totalStart
                );
                return result;
            }

            std::string preamble;
            int preambleLineCount = 0;
            std::shared_ptr<SlangdSession> sharedSession;
            LspClient* client = nullptr;
            std::string initializationFailure;
            bool opened = false;
            const std::string documentUri = makeUniqueSlangdDocumentUri();
        };

        // Generic single-worker debounced analysis queue: a new request() replaces whatever was
        // pending, and a completed result is only published if its document version is still the
        // newest one submitted (an older, slower-finishing request never clobbers a newer result).
        // Currently only DiagnosticsService uses this — symbol analysis runs synchronously now
        // (see SymbolAnalyzer) — but it's kept generic in case another async backend needs the
        // same staleness-safe queueing later.
        template <typename Backend, typename Result>
        class DebouncedAnalysisWorker
        {
        public:
            // makeBackend runs on the worker thread itself (not the constructing thread) so a
            // backend that does blocking setup (e.g. SlangdBackend spawning/attaching to slangd)
            // never stalls the caller.
            explicit DebouncedAnalysisWorker(std::function<Backend()> makeBackend)
                : state(std::make_shared<State>())
            {
                std::thread([state = state, makeBackend = std::move(makeBackend)]() mutable
                {
                    Backend backend = makeBackend();
                    run(*state, backend);
                }).detach();
            }

            // Backend::analyze() (e.g. SlangdBackend waiting on slangd, see kDiagnosticsWaitMs) can
            // block the worker thread for up to ~1.5s with no way to interrupt it mid-call — joining
            // here would stall whichever thread destroys this (typically the UI thread, e.g. every
            // time a code editor's preamble changes) for that entire duration. So this only signals
            // stopping and lets the detached thread exit on its own time; state is shared/refcounted
            // so it stays valid for the thread to finish reading/writing even after this object, and
            // the DiagnosticsService owning it, are gone.
            ~DebouncedAnalysisWorker()
            {
                std::lock_guard lock(state->mutex);
                state->stopping = true;
                state->condition.notify_one();
            }

            void request(std::uint64_t documentVersion, std::string source)
            {
                {
                    std::lock_guard lock(state->mutex);
                    state->latestSubmittedVersion = documentVersion;
                    state->pending = Request{
                        documentVersion,
                        std::move(source),
                        std::chrono::steady_clock::now(),
                    };
                    state->completed.reset();
                    state->busy = true;
                }
                state->condition.notify_one();
            }

            std::optional<Result> takeLatestResult()
            {
                std::lock_guard lock(state->mutex);
                if (!state->completed)
                    return std::nullopt;
                std::optional<Result> result = std::move(state->completed);
                state->completed.reset();
                return result;
            }

            bool isBusy() const
            {
                std::lock_guard lock(state->mutex);
                return state->busy;
            }

        private:
            struct Request
            {
                std::uint64_t version;
                std::string source;
                std::chrono::steady_clock::time_point submittedAt;
            };

            struct State
            {
                std::mutex mutex;
                std::condition_variable condition;
                std::optional<Request> pending;
                std::optional<Result> completed;
                std::uint64_t latestSubmittedVersion = 0;
                bool busy = false;
                bool stopping = false;
            };

            static void run(State& state, Backend& backend)
            {
                while (true)
                {
                    Request request;
                    {
                        std::unique_lock lock(state.mutex);
                        state.condition.wait(lock, [&state] { return state.stopping || state.pending.has_value(); });
                        if (state.stopping)
                            return;
                        request = std::move(*state.pending);
                        state.pending.reset();
                    }

                    Result result;
                    const auto analysisStart = std::chrono::steady_clock::now();
                    try
                    {
                        result = backend.analyze(request.version, request.source);
                    }
                    catch (const std::exception& error)
                    {
                        result.documentVersion = request.version;
                        result.failureMessage = std::string("Code editor analysis failed: ") + error.what();
                    }
                    catch (...)
                    {
                        result.documentVersion = request.version;
                        result.failureMessage = "Code editor analysis failed with an unknown exception";
                    }
                    result.timings.queueWait = std::chrono::duration_cast<std::chrono::microseconds>(
                        analysisStart - request.submittedAt
                    );
                    result.timings.requestToResult = std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - request.submittedAt
                    );

                    {
                        std::lock_guard lock(state.mutex);
                        if (state.stopping)
                            return;
                        if (request.version == state.latestSubmittedVersion)
                        {
                            state.completed = std::move(result);
                            state.busy = false;
                        }
                    }
                }
            }

            std::shared_ptr<State> state;
        };
    }

    std::size_t countUtf8CodePoints(std::string_view text)
    {
        std::size_t count = 0;
        for (std::size_t i = 0; i < text.size(); i = nextUtf8CodePoint(text, i))
            ++count;
        return count;
    }

    std::size_t utf8ByteOffsetForCodePoint(std::string_view text, std::size_t codePointIndex)
    {
        std::size_t byteOffset = 0;
        for (std::size_t current = 0; current < codePointIndex && byteOffset < text.size(); ++current)
            byteOffset = nextUtf8CodePoint(text, byteOffset);
        return byteOffset;
    }

    std::optional<DiagnosticRange> normalizeDiagnosticRange(
        const std::string& source,
        const SlangDiagnostic& diagnostic
    )
    {
        if (diagnostic.line < 0)
            return std::nullopt;

        const std::string_view line = lineAt(source, static_cast<std::size_t>(diagnostic.line));
        // An empty view can be either a valid empty line or an invalid line. Verify the latter by
        // counting line separators without allocating a line table.
        std::size_t lineCount = 1;
        lineCount += static_cast<std::size_t>(std::count(source.begin(), source.end(), '\n'));
        if (static_cast<std::size_t>(diagnostic.line) >= lineCount)
            return std::nullopt;

        if (diagnostic.column < 0)
        {
            return DiagnosticRange{
                static_cast<std::size_t>(diagnostic.line),
                0,
                countUtf8CodePoints(line),
            };
        }

        std::size_t startByte = std::min(static_cast<std::size_t>(diagnostic.column), line.size());
        // Never split a UTF-8 continuation sequence if Slang reports a byte inside a code point.
        while (startByte > 0 && startByte < line.size() &&
               (static_cast<unsigned char>(line[startByte]) & 0xC0) == 0x80)
            --startByte;

        const std::size_t requestedLength = diagnostic.length > 0
            ? static_cast<std::size_t>(diagnostic.length)
            : 0;
        std::size_t endByte = std::min(startByte + requestedLength, line.size());
        while (endByte > startByte && endByte < line.size() &&
               (static_cast<unsigned char>(line[endByte]) & 0xC0) == 0x80)
            ++endByte;
        endByte = std::min(endByte, line.size());

        if (endByte == startByte && startByte < line.size())
            endByte = nextUtf8CodePoint(line, startByte);

        return DiagnosticRange{
            static_cast<std::size_t>(diagnostic.line),
            countUtf8CodePoints(line.substr(0, startByte)),
            countUtf8CodePoints(line.substr(0, endByte)),
        };
    }

    std::vector<PositionedSemanticToken> positionSemanticTokens(
        std::string_view source,
        const std::vector<SemanticToken>& tokens
    )
    {
        std::vector<std::size_t> lineStarts{ 0 };
        for (std::size_t i = 0; i < source.size(); ++i)
        {
            if (source[i] == '\n')
                lineStarts.push_back(i + 1);
        }

        std::vector<PositionedSemanticToken> result;
        result.reserve(tokens.size());
        for (const SemanticToken& token : tokens)
        {
            if (token.startByte >= token.endByte || token.endByte > source.size())
                continue;

            const auto lineIterator = std::upper_bound(lineStarts.begin(), lineStarts.end(), token.startByte);
            const std::size_t line = static_cast<std::size_t>(std::distance(lineStarts.begin(), lineIterator) - 1);
            const std::size_t lineStart = lineStarts[line];
            const std::size_t lineEnd = line + 1 < lineStarts.size()
                ? lineStarts[line + 1] - 1
                : source.size();
            if (token.endByte > lineEnd)
                continue;

            result.push_back(PositionedSemanticToken{
                line,
                countUtf8CodePoints(source.substr(lineStart, token.startByte - lineStart)),
                countUtf8CodePoints(source.substr(lineStart, token.endByte - lineStart)),
                token.kind,
            });
        }
        return result;
    }

    std::optional<SymbolHover> findSymbolAt(
        const SymbolIndex& index,
        std::string_view source,
        std::size_t byteOffset,
        std::string_view name
    )
    {
        const std::uint32_t clampedOffset = static_cast<std::uint32_t>(std::min(byteOffset, source.size()));

        // memberReceiverExpression scans backward from byteOffset for a preceding "receiver.",
        // matching how collectCompletionCandidateUniverse decides between member-access and
        // scope-chain resolution below — byteOffset here plays the same role cursorByte does there
        // (the start of the identifier/field being looked up).
        if (const std::optional<std::string_view> receiver = memberReceiverExpression(source, byteOffset))
        {
            const std::optional<std::string> typeName = resolveExpressionType(index, *receiver, clampedOffset);
            if (!typeName)
                return std::nullopt;
            const auto type = index.typeMembers.find(*typeName);
            if (type == index.typeMembers.end())
                return std::nullopt;
            for (const Symbol& member : type->second)
                if (member.name == name)
                    return SymbolHover{ member, *typeName };
            return std::nullopt;
        }

        if (const Symbol* symbol = resolveVisibleSymbol(index, name, clampedOffset))
            return SymbolHover{ *symbol, {} };
        return std::nullopt;
    }

    std::optional<FunctionSignature> describeFunctionSignature(
        const SymbolIndex& index,
        const Symbol& functionSymbol
    )
    {
        if (functionSymbol.kind != SymbolKind::Function || index.scopes.empty())
            return std::nullopt;

        FunctionSignature signature;
        signature.returnType = functionSymbol.typeName;
        signature.name = functionSymbol.name;

        const std::size_t functionScope = innermostScopeAt(index, functionSymbol.declaration.startByte);
        for (const Symbol& candidate : index.scopes[functionScope].symbols)
            if (candidate.kind == SymbolKind::Parameter)
                signature.parameters.push_back(FunctionParameter{ candidate.typeName, candidate.name });
        return signature;
    }

    const std::unordered_set<std::string_view>& builtinFunctionNames()
    {
        static const std::unordered_set<std::string_view> names = []
        {
            std::unordered_set<std::string_view> result;
            for (const FunctionSignature& signature : builtinFunctionSignatures())
                result.insert(signature.name);
            return result;
        }();
        return names;
    }

    std::optional<FunctionSignature> describeBuiltinFunctionSignature(std::string_view name)
    {
        for (const FunctionSignature& signature : builtinFunctionSignatures())
            if (signature.name == name)
                return signature;
        return std::nullopt;
    }

    std::optional<FunctionSignature> describeBuiltinTextureMethodSignature(std::string_view name)
    {
        for (const FunctionSignature& signature : builtinTextureMethodSignatures())
            if (signature.name == name)
                return signature;
        return std::nullopt;
    }

    // The part of completion that's expensive relative to symbolIndex's update cadence: resolving
    // the member-access receiver's type, or walking the full enclosing-scope chain. Returns every
    // visible symbol with no partial-text filtering applied yet (matchQuality left at 0) — callers
    // that re-run completion many times against an unchanged symbolIndex (e.g. once per keystroke
    // while symbolIndex only refreshes every ~100ms) can cache this and only re-run
    // filterCompletionCandidates(), which is cheap, per keystroke.
    std::vector<CompletionCandidate> collectCompletionCandidateUniverse(
        const SymbolIndex& index,
        std::string_view source,
        std::size_t cursorByte
    )
    {
        std::vector<CompletionCandidate> result;
        std::unordered_set<std::string> emittedNames;

        std::size_t memberEnd = std::min(cursorByte, source.size());
        while (memberEnd > 0 && std::isspace(static_cast<unsigned char>(source[memberEnd - 1])))
            --memberEnd;
        const bool memberAccess = memberEnd > 0 && source[memberEnd - 1] == '.';
        const std::optional<std::string_view> receiver = memberReceiverExpression(source, cursorByte);
        if (memberAccess)
        {
            if (!receiver)
                return result;

            const std::optional<std::string> typeName = resolveExpressionType(
                index,
                *receiver,
                static_cast<std::uint32_t>(std::min(cursorByte, source.size()))
            );
            if (typeName)
            {
                if (const auto type = index.typeMembers.find(*typeName); type != index.typeMembers.end())
                {
                    for (auto member = type->second.rbegin(); member != type->second.rend(); ++member)
                    {
                        if (!emittedNames.insert(member->name).second)
                            continue;
                        result.push_back(CompletionCandidate{
                            member->name,
                            member->kind,
                            member->typeName,
                            0,
                            completionCategoryPriority(member->kind),
                        });
                    }
                }
            }
        }
        else if (!index.scopes.empty())
        {
            std::optional<std::size_t> scopeIndex =
                innermostScopeAt(index, static_cast<std::uint32_t>(std::min(cursorByte, source.size())));
            while (scopeIndex)
            {
                const Scope& scope = index.scopes[*scopeIndex];
                for (auto symbol = scope.symbols.rbegin(); symbol != scope.symbols.rend(); ++symbol)
                {
                    // cursorByte is the start of the word currently being completed (see the
                    // caller), not the live text-cursor position. A local declaration whose own
                    // name starts exactly there is the token the user is mid-typing, not a
                    // usable prior declaration — tree-sitter happily parses an in-progress
                    // "float ro" as a complete one-token declaration, so without this the
                    // identifier being typed offers itself as a completion for itself.
                    if (!symbol->external && *scopeIndex != 0 && symbol->declaration.startByte >= cursorByte)
                        continue;
                    if (!emittedNames.insert(symbol->name).second)
                        continue;

                    result.push_back(CompletionCandidate{
                        symbol->name,
                        symbol->kind,
                        symbol->typeName,
                        0,
                        completionCategoryPriority(symbol->kind),
                    });
                }
                scopeIndex = scope.parent;
            }

            for (std::string_view builtinName : builtinFunctionNames())
            {
                if (!emittedNames.insert(std::string(builtinName)).second)
                    continue;
                result.push_back(CompletionCandidate{
                    std::string(builtinName),
                    SymbolKind::Function,
                    {},
                    0,
                    completionCategoryPriority(SymbolKind::Function),
                });
            }
        }
        return result;
    }

    // The cheap part: filters a candidate universe (see collectCompletionCandidateUniverse)
    // against partial and sorts by match quality. Safe to re-run on every keystroke even when the
    // universe was cached from an earlier call.
    std::vector<CompletionCandidate> filterCompletionCandidates(
        const std::vector<CompletionCandidate>& universe,
        std::string_view partial
    )
    {
        std::vector<CompletionCandidate> result;
        result.reserve(universe.size());
        for (const CompletionCandidate& candidate : universe)
        {
            const int quality = completionMatchQuality(candidate.name, partial);
            if (quality >= 0)
            {
                CompletionCandidate matched = candidate;
                matched.matchQuality = quality;
                result.push_back(std::move(matched));
            }
        }

        std::ranges::sort(result, [](const CompletionCandidate& left, const CompletionCandidate& right)
        {
            if (left.matchQuality != right.matchQuality)
                return left.matchQuality < right.matchQuality;
            if (left.categoryPriority != right.categoryPriority)
                return left.categoryPriority < right.categoryPriority;
            return left.name < right.name;
        });
        return result;
    }

    std::vector<CompletionCandidate> collectCompletionCandidates(
        const SymbolIndex& index,
        std::string_view source,
        std::size_t cursorByte,
        std::string_view partial
    )
    {
        return filterCompletionCandidates(collectCompletionCandidateUniverse(index, source, cursorByte), partial);
    }

    bool isGlobalScope(const SymbolIndex& index, std::size_t cursorByte)
    {
        if (index.scopes.empty())
            return true;
        const std::size_t clamped = std::min(cursorByte, static_cast<std::size_t>(index.scopes.front().range.endByte));
        return innermostScopeAt(index, static_cast<std::uint32_t>(clamped)) == 0;
    }

    struct SymbolAnalyzer::Impl : SymbolAnalyzerImpl
    {
        using SymbolAnalyzerImpl::SymbolAnalyzerImpl;
    };

    SymbolAnalyzer::SymbolAnalyzer(std::string preamble, int preambleLineCount)
        : mImpl(std::make_unique<Impl>(std::move(preamble), preambleLineCount))
    {
    }

    SymbolAnalyzer::~SymbolAnalyzer() = default;

    SymbolAnalysisResult SymbolAnalyzer::analyze(const std::string& source, const std::vector<SourceEdit>& edits)
    {
        return mImpl->analyze(source, edits);
    }

    bool SymbolAnalyzer::updateTree(
        const std::string& source,
        const std::vector<SourceEdit>& edits,
        AnalysisTimings* timings,
        std::string* failureMessage
    )
    {
        return mImpl->updateTree(source, edits, timings, failureMessage);
    }

    SymbolAnalysisResult SymbolAnalyzer::scanCurrentTree(const std::string& source)
    {
        return mImpl->scanCurrentTree(source);
    }

    const SymbolIndex& SymbolAnalyzer::getExternalIndex() const
    {
        return mImpl->externalIndex;
    }

    std::shared_ptr<SlangdSession> SlangdSession::acquire()
    {
        // Called from each DiagnosticsService's own background worker thread (see
        // DiagnosticsService's constructor), so several editors opening around the same time can
        // race here concurrently — mutex-guarded so only one of them actually spawns slangd.exe;
        // the rest just observe the winner's already-constructed session once the lock is
        // released. Lazily (re)started on the first acquire() after the last owner released
        // theirs; in practice hyp::ed::Editor keeps one shared_ptr alive for its whole process
        // lifetime (see its mSlangdKeepAlive member) specifically so this teardown/respawn cycle
        // never happens between individual editor windows opening and closing.
        static std::mutex mutex;
        static std::weak_ptr<SlangdSession> cached;
        std::lock_guard<std::mutex> lock(mutex);
        std::shared_ptr<SlangdSession> session = cached.lock();
        if (!session)
        {
            session = std::shared_ptr<SlangdSession>(new SlangdSession());
            cached = session;
        }
        return session;
    }

    SlangdSession::SlangdSession()
    {
        const std::filesystem::path slangdPath = hyp::Platform::getExecutableDir() / "slangd.exe";
        if (!std::filesystem::exists(slangdPath))
            return;

        LspServerConfig config;
        config.executablePath = slangdPath.string();
        config.arguments = { "--stdio" };
        config.languageId = "slang";
        config.requestTimeoutMs = 5000;
        auto candidate = std::make_unique<LspClient>(std::move(config));
        if (candidate->start())
            mClient = std::move(candidate);
    }

    struct DiagnosticsService::Impl : DebouncedAnalysisWorker<SlangdBackend, DiagnosticsResult>
    {
        using DebouncedAnalysisWorker::DebouncedAnalysisWorker;
    };

    DiagnosticsService::DiagnosticsService(std::string preamble, int preambleLineCount)
        : mImpl(std::make_unique<Impl>(std::function<SlangdBackend()>(
            [preamble = std::move(preamble), preambleLineCount]() mutable
            {
                // SlangdBackend's constructor calls SlangdSession::acquire(), which runs here on
                // this DiagnosticsService's own worker thread — not the constructing thread — so
                // a cold-start slangd.exe spawn never blocks whoever is opening the editor.
                return SlangdBackend(std::move(preamble), preambleLineCount);
            })))
    {
    }

    DiagnosticsService::~DiagnosticsService() = default;

    void DiagnosticsService::request(std::uint64_t documentVersion, std::string source)
    {
        mImpl->request(documentVersion, std::move(source));
    }

    std::optional<DiagnosticsResult> DiagnosticsService::takeLatestResult()
    {
        return mImpl->takeLatestResult();
    }

    bool DiagnosticsService::isBusy() const
    {
        return mImpl->isBusy();
    }
}
