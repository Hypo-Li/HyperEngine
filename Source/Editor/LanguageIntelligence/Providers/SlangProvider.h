#pragma once

#include <LanguageIntelligence/LspClient/LspClient.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace hyp::lang
{
    enum class SymbolKind
    {
        Type,
        Function,
        Field,
        Parameter,
        LocalVariable,
        GlobalVariable,
        EnumMember,
        Macro,
    };
    using TokenKind = SymbolKind;

    struct SourceRange
    {
        std::uint32_t startByte = 0;
        std::uint32_t endByte = 0;

        bool contains(std::uint32_t byteOffset) const
        {
            return startByte <= byteOffset && byteOffset < endByte;
        }

        bool operator==(const SourceRange&) const = default;
    };

    struct SemanticToken
    {
        std::uint32_t startByte = 0;
        std::uint32_t endByte = 0;
        TokenKind kind = TokenKind::LocalVariable;

        bool operator==(const SemanticToken&) const = default;
    };

    struct Symbol
    {
        std::string name;
        SymbolKind kind = SymbolKind::LocalVariable;
        std::string typeName;
        SourceRange declaration;
        bool external = false;

        bool operator==(const Symbol&) const = default;
    };

    struct Scope
    {
        SourceRange range;
        std::optional<std::size_t> parent;
        std::vector<std::size_t> children;
        std::vector<Symbol> symbols;

        bool operator==(const Scope&) const = default;
    };

    struct SymbolIndex
    {
        std::vector<Scope> scopes;
        std::vector<SemanticToken> semanticTokens;
        std::unordered_map<std::string, std::vector<Symbol>> typeMembers;

        bool operator==(const SymbolIndex&) const = default;
    };

    struct PositionedSemanticToken
    {
        std::size_t line = 0;
        std::size_t startCodePoint = 0;
        std::size_t endCodePoint = 0;
        SymbolKind kind = SymbolKind::LocalVariable;
    };

    struct CompletionCandidate
    {
        std::string name;
        SymbolKind kind = SymbolKind::LocalVariable;
        std::string typeName;
        int matchQuality = 0;
        int categoryPriority = 0;
    };

    enum class SlangDiagnosticSeverity
    {
        Info,
        Warning,
        Error,
        Fatal,
    };

    struct SlangDiagnostic
    {
        int line = -1;   // zero-based user-source line
        int column = -1; // zero-based UTF-8 byte column; -1 means the whole line
        int length = 1;  // UTF-8 byte length reported by Slang's caret line
        SlangDiagnosticSeverity severity = SlangDiagnosticSeverity::Error;
        std::string code;
        std::string message;
    };

    struct DiagnosticRange
    {
        std::size_t line = 0;
        std::size_t startCodePoint = 0;
        std::size_t endCodePoint = 0;
    };

    struct AnalysisTimings
    {
        std::chrono::microseconds queueWait{};
        std::chrono::microseconds treeParse{};
        std::chrono::microseconds declarationScan{};
        std::chrono::microseconds semanticScan{};
        std::chrono::microseconds tokenFinalize{};
        std::chrono::microseconds treeSitter{};
        std::chrono::microseconds slangSession{};
        std::chrono::microseconds slangCompile{};
        std::chrono::microseconds diagnosticParse{};
        std::chrono::microseconds slang{};
        std::chrono::microseconds tokenPositioning{};
        std::chrono::microseconds total{};
        std::chrono::microseconds requestToResult{};
    };

    // A tree-sitter byte offset + (row, byte-column) point, mirroring TSPoint. Kept free of
    // tree-sitter types so this header doesn't need <tree_sitter/api.h>.
    struct SourcePoint
    {
        std::uint32_t row = 0;
        std::uint32_t column = 0;
    };

    // Describes a single text edit for incremental reparsing, mirroring tree-sitter's
    // TSInputEdit. startByte/startPoint is the position where the edit begins (same in the
    // pre-edit and post-edit text, since content before it is untouched); oldEndByte/oldEndPoint
    // is where the replaced span ended in the PRE-edit text; newEndByte/newEndPoint is where the
    // replacement ends in the POST-edit text. A pure insertion has oldEnd == start; a pure
    // deletion has newEnd == start.
    struct SourceEdit
    {
        std::uint32_t startByte = 0;
        std::uint32_t oldEndByte = 0;
        std::uint32_t newEndByte = 0;
        SourcePoint startPoint;
        SourcePoint oldEndPoint;
        SourcePoint newEndPoint;
    };

    // Output of the tree-sitter scope/symbol pass only — no Slang involved, so this stays cheap
    // enough to request on nearly every keystroke without the editor feeling laggy.
    struct SymbolAnalysisResult
    {
        std::uint64_t documentVersion = 0;
        SymbolIndex symbolIndex;
        std::string failureMessage;
        AnalysisTimings timings;

        // True when this pass reused the previous tree via ts_tree_edit() + incremental
        // ts_parser_parse_string(); false when it did a full parse (no edits given, no previous
        // tree yet, or the caller couldn't verify its edits were trustworthy). Purely
        // informational — useful for surfacing "was this incremental?" in a HUD/diagnostic.
        bool incrementalParse = false;
    };

    // Output of the Slang compile/diagnose pass only. Meant to be requested at a slower cadence
    // than SymbolAnalysisResult so red squiggles don't flicker in and out while typing.
    struct DiagnosticsResult
    {
        std::uint64_t documentVersion = 0;
        std::vector<SlangDiagnostic> diagnostics;
        std::string failureMessage;
        AnalysisTimings timings;
    };

    std::optional<DiagnosticRange> normalizeDiagnosticRange(
        const std::string& source,
        const SlangDiagnostic& diagnostic
    );

    std::vector<PositionedSemanticToken> positionSemanticTokens(
        std::string_view source,
        const std::vector<SemanticToken>& tokens
    );

    std::vector<CompletionCandidate> collectCompletionCandidates(
        const SymbolIndex& index,
        std::string_view source,
        std::size_t cursorByte,
        std::string_view partial
    );

    // Every visible symbol at cursorByte with no partial-text filtering applied yet
    // (matchQuality left at 0). Cache this keyed on whatever identifies "the underlying data
    // hasn't changed" (e.g. a symbolIndex generation counter plus cursorByte) to avoid re-walking
    // the scope chain / re-resolving a member-access receiver on every keystroke of a completion
    // session — filterCompletionCandidates() below is the cheap part meant to re-run every time.
    std::vector<CompletionCandidate> collectCompletionCandidateUniverse(
        const SymbolIndex& index,
        std::string_view source,
        std::size_t cursorByte
    );

    // Filters a candidate universe (see collectCompletionCandidateUniverse) against partial and
    // sorts by match quality. Cheap enough to call on every keystroke.
    std::vector<CompletionCandidate> filterCompletionCandidates(
        const std::vector<CompletionCandidate>& universe,
        std::string_view partial
    );

    // True when cursorByte falls directly in the file-level scope (outside every function body
    // and block) — statement-only keywords (if/for/while/...) aren't valid there, only
    // declarations are, so completion should not offer them at this position.
    bool isGlobalScope(const SymbolIndex& index, std::size_t cursorByte);

    struct SymbolHover
    {
        Symbol symbol;

        // Only set when symbol.kind == Field: the struct/type that declares it (e.g.
        // "MaterialInput" for a hover over the "color" in "input.color"). Field symbols only ever
        // come from typeMembers (see collectDeclarations' struct_specifier handling), never from a
        // scope's own symbol list, so this is the only place that owning-type name is known —
        // Symbol::typeName on a Field holds the field's own type (e.g. "float3"), not its owner.
        std::string ownerTypeName;
    };

    // Resolves the symbol a hovered identifier/field refers to, using the same scope-chain /
    // member-access resolution as collectCompletionCandidateUniverse. name should be the exact
    // identifier text at byteOffset (e.g. from TextEditor::GetWordAtMousePos()); byteOffset should
    // be that identifier's start (e.g. from TextEditor::GetDocPosAtMousePos() converted to a byte
    // offset). Returns std::nullopt when nothing resolves (unknown name, keyword, or a receiver
    // expression whose type couldn't be inferred).
    std::optional<SymbolHover> findSymbolAt(
        const SymbolIndex& index,
        std::string_view source,
        std::size_t byteOffset,
        std::string_view name
    );

    struct FunctionParameter
    {
        std::string typeName;
        std::string name;
    };

    struct FunctionSignature
    {
        std::string returnType;
        std::string name;
        std::vector<FunctionParameter> parameters;
    };

    // Builds the full "ReturnType Name(ParamType ParamName, ...)" shape of a Function symbol found
    // via findSymbolAt/resolveVisibleSymbol. functionSymbol.kind must be SymbolKind::Function.
    // Parameters are read back from the function's own scope (found by locating the innermost
    // scope containing functionSymbol's declaration, since a function's scope spans its whole
    // definition including the signature — see collectDeclarations' function_definition handling).
    std::optional<FunctionSignature> describeFunctionSignature(
        const SymbolIndex& index,
        const Symbol& functionSymbol
    );

    // Names of HLSL/Slang intrinsics (dot, reflect, mul, ...) — these have no tree-sitter
    // declaration anywhere, so callers that want them to color/hover as functions look them up
    // through this fixed table instead of the usual declaration-based symbol machinery.
    const std::unordered_set<std::string_view>& builtinFunctionNames();

    // Signature for a name from builtinFunctionNames(), for hovering an intrinsic call the same
    // way describeFunctionSignature() covers a user-declared one. Each intrinsic here is
    // generic/overloaded in real HLSL; this returns one representative overload (usually the
    // float/floatN form), not the full overload set.
    std::optional<FunctionSignature> describeBuiltinFunctionSignature(std::string_view name);

    // Same idea as describeBuiltinFunctionSignature(), but for texture resource methods
    // (Texture2D::Sample etc.) hovered as "receiver.Sample(...)" rather than called bare.
    std::optional<FunctionSignature> describeBuiltinTextureMethodSignature(std::string_view name);

    std::size_t countUtf8CodePoints(std::string_view text);
    std::size_t utf8ByteOffsetForCodePoint(std::string_view text, std::size_t codePointIndex);

    // Synchronous tree-sitter scope/symbol analysis. Cheap enough (single-digit milliseconds for
    // realistic document sizes) to call directly on the UI thread after every edit — no
    // debouncing or background worker, matching how mainstream editors actually use tree-sitter
    // (Neovim, Zed, and Helix all reparse synchronously on the calling thread; incremental
    // reparsing existing specifically so this doesn't need to be pushed to a background thread).
    //
    // Only the parse step is incremental. declarationScan/semanticScan (see SlangProvider.cpp)
    // still walk the whole resulting tree on every call — a declaration or usage anywhere can
    // affect symbol visibility/coloring anywhere else in the document, so scoping a rescan to
    // just the edited byte range isn't a sound general rule the way it is for syntax
    // highlighting. This is deliberately out of scope here, not an oversight.
    class SymbolAnalyzer
    {
    public:
        SymbolAnalyzer(std::string preamble, int preambleLineCount);
        ~SymbolAnalyzer();

        SymbolAnalyzer(const SymbolAnalyzer&) = delete;
        SymbolAnalyzer& operator=(const SymbolAnalyzer&) = delete;

        // Analyzes source: updateTree() + scanCurrentTree() in one call. When edits is non-empty
        // and there's a previous tree to apply it to, reparsing is incremental; edits must
        // exactly describe the transformation from the previous call's source to this one, in
        // order — anything else risks tree-sitter silently producing a wrong tree, so callers
        // that can't verify their edits are correct should pass an empty list instead (always
        // safe: it just forces a full reparse for this call, identical in cost and correctness to
        // today's behavior).
        SymbolAnalysisResult analyze(const std::string& source, const std::vector<SourceEdit>& edits = {});

        // Just the incremental-reparse half of analyze() — updates the persistent tree without
        // extracting a symbol table. Cheap enough to call on every single keystroke, including
        // rapid key-repeat bursts (e.g. holding Backspace), where scanCurrentTree()'s full-document
        // walk would otherwise become the bottleneck if run on every repeat event too. Returns
        // true if the reparse was incremental, false if it fell back to a full parse (also see
        // SymbolAnalysisResult::incrementalParse). On failure (see failureMessage), the tree isn't
        // usable — a subsequent scanCurrentTree() call will report an empty index.
        bool updateTree(
            const std::string& source,
            const std::vector<SourceEdit>& edits,
            AnalysisTimings* timings,
            std::string* failureMessage
        );

        // Just the symbol-table-extraction half of analyze() — walks the tree updateTree() (or
        // analyze()) most recently produced. source must be exactly the text that call parsed;
        // passing anything else produces a meaningless result. This is the expensive part —
        // callers debouncing rapid edit bursts should keep calling updateTree() every keystroke
        // (to keep the tree from drifting) but only call this once the burst settles.
        SymbolAnalysisResult scanCurrentTree(const std::string& source);

        // The symbol index scanned from the preamble alone, with its own intact scope tree
        // (unlike the merged SymbolIndex returned by analyze()/scanCurrentTree(), which only
        // carries the preamble's top-level symbols — see injectExternalSymbols() in the .cpp).
        // describeFunctionSignature() needs this to look up an external function's parameters,
        // since a Symbol::declaration byte offset for an external symbol is only meaningful
        // against this preamble-only tree.
        const SymbolIndex& getExternalIndex() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> mImpl;
    };

    // Owns a single shared `slangd.exe` process (via one LspClient) so that multiple concurrently
    // open code editors don't each pay for their own subprocess. Lazily started on first
    // acquire() and torn down once the last owner releases its shared_ptr; a later acquire()
    // after that starts a fresh process. Safe to call/share from multiple threads: LspClient
    // itself is internally mutex-protected and keys documents by URI (see
    // LspClient::didOpen/didClose/diagnosticsSnapshot), so independent documents on a shared
    // client don't interfere with each other.
    class SlangdSession
    {
    public:
        static std::shared_ptr<SlangdSession> acquire();

        LspClient* getClient() const noexcept
        {
            return mClient.get();
        }

    private:
        SlangdSession();

        std::unique_ptr<LspClient> mClient;
    };

    // Owns a single Slang compile/diagnose worker on a background thread, independent of
    // SymbolAnalyzer (which runs synchronously) so a slow Slang pass never delays coloring
    // updates. New requests replace pending older requests, and completed results are published
    // only if their document version is still the newest submitted version. Callers are expected
    // to request() this at a debounced, lower frequency than every keystroke, since every request
    // re-compiles the whole document through Slang.
    class DiagnosticsService
    {
    public:
        // Shares slangd.exe with every other DiagnosticsService via SlangdSession::acquire().
        // Acquiring a fresh session can block on spawning slangd.exe and completing its LSP
        // initialize handshake (acquire() itself is thread-safe, but the work it does when
        // creating a new session is not fast) — that call happens on this DiagnosticsService's
        // own background worker thread, not the constructing thread, so constructing one here
        // never blocks whoever is opening an editor.
        //
        // preamble/preambleLineCount default to empty/0 for callers (e.g. hyp::ed::Editor's
        // process-lifetime keep-alive instance) that have no document of their own to analyze —
        // request() is simply never called on such an instance, so the values are never read.
        DiagnosticsService(std::string preamble = std::string(), int preambleLineCount = 0);

        ~DiagnosticsService();

        DiagnosticsService(const DiagnosticsService&) = delete;
        DiagnosticsService& operator=(const DiagnosticsService&) = delete;

        void request(std::uint64_t documentVersion, std::string source);
        std::optional<DiagnosticsResult> takeLatestResult();
        bool isBusy() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> mImpl;
    };
}
