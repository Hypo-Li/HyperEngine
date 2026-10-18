#include "SlangCodeEditor.h"

#include <HypImGui/ImGuiFrontend.h>

#include <algorithm>
#include <cctype>
#include <functional>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace hyp::ed
{
    namespace
    {
        // Identical for every SlangCodeEditor instance (nothing here depends on preamble/text), so
        // built once and shared rather than duplicated per instance. TextEditor stores the Language
        // pointer for the editor's whole lifetime (see TextEditor::SetLanguage), so it needs to be
        // this kind of permanently-valid object rather than a per-call temporary.
        const TextEditor::Language& sharedLanguage()
        {
            static const TextEditor::Language language = []
            {
                TextEditor::Language lang = *TextEditor::Language::Hlsl();
                lang.declarations.insert("enum");

                std::function<bool(ImWchar)> basePunctuation = lang.isPunctuation;
                lang.isPunctuation = [basePunctuation](ImWchar codepoint)
                {
                    return codepoint == '#' || (basePunctuation && basePunctuation(codepoint));
                };
                lang.preprocess = 0;
                for (const char* directive : {
                    "define", "include", "if", "ifdef", "ifndef", "endif", "else", "elif",
                    "undef", "pragma", "error", "warning", "line"
                })
                {
                    lang.keywords.insert(directive);
                }

                // HLSL/Slang intrinsics — not declared anywhere the tree-sitter symbol pass could
                // see, so without this they'd render as plain identifiers instead of functions.
                for (std::string_view intrinsic : lang::builtinFunctionNames())
                    lang.builtinFunctions.insert(std::string(intrinsic));

                return lang;
            }();
            return language;
        }

        TextEditor::Palette buildPalette()
        {
            TextEditor::Palette palette = TextEditor::GetDarkPalette();

            palette[static_cast<size_t>(TextEditor::Color::keyword)] = IM_COL32(0xD8, 0xA0, 0xDF, 255);
            palette[static_cast<size_t>(TextEditor::Color::number)] = IM_COL32(0xB5, 0xCE, 0xA8, 255);
            palette[static_cast<size_t>(TextEditor::Color::string)] = IM_COL32(0xE8, 0xC9, 0xBB, 255);
            palette[static_cast<size_t>(TextEditor::Color::punctuation)] = IM_COL32(0xD4, 0xD4, 0xD4, 255);
            palette[static_cast<size_t>(TextEditor::Color::preprocessor)] = IM_COL32(0xDC, 0xDC, 0xAA, 255);
            palette[static_cast<size_t>(TextEditor::Color::identifier)] = IM_COL32(0x9C, 0xDC, 0xFE, 255);
            palette[static_cast<size_t>(TextEditor::Color::knownIdentifier)] = IM_COL32(0x4E, 0xC9, 0xB0, 255);
            palette[static_cast<size_t>(TextEditor::Color::text)] = IM_COL32(0xDA, 0xDA, 0xDA, 255);
            palette[static_cast<size_t>(TextEditor::Color::declaration)] = IM_COL32(0x56, 0x9C, 0xD6, 255);
            palette[static_cast<size_t>(TextEditor::Color::parameter)] = IM_COL32(0x9A, 0x9A, 0x9A, 255);
            palette[static_cast<size_t>(TextEditor::Color::globalVariable)] = IM_COL32(0xC8, 0xC8, 0xC8, 255);
            palette[static_cast<size_t>(TextEditor::Color::enumMember)] = IM_COL32(0xB8, 0xD7, 0xA3, 255);
            palette[static_cast<size_t>(TextEditor::Color::macro)] = IM_COL32(0xBE, 0xB7, 0xFF, 255);
            palette[static_cast<size_t>(TextEditor::Color::comment)] = IM_COL32(0x6A, 0x99, 0x55, 255);
            palette[static_cast<size_t>(TextEditor::Color::background)] = IM_COL32(0x1E, 0x1E, 0x1E, 255);
            palette[static_cast<size_t>(TextEditor::Color::cursor)] = IM_COL32(0xAE, 0xAF, 0xAD, 255);
            palette[static_cast<size_t>(TextEditor::Color::selection)] = IM_COL32(0x26, 0x4F, 0x78, 255);
            palette[static_cast<size_t>(TextEditor::Color::whitespace)] = IM_COL32(0xE3, 0xE4, 0xE2, 0x29);
            palette[static_cast<size_t>(TextEditor::Color::matchingBracketBackground)] = IM_COL32(0x00, 0x64, 0x00, 0x1A);
            palette[static_cast<size_t>(TextEditor::Color::matchingBracketActive)] = IM_COL32(0x88, 0x88, 0x88, 255);
            palette[static_cast<size_t>(TextEditor::Color::matchingBracketLevel1)] = IM_COL32(0xFF, 0xD7, 0x00, 255);
            palette[static_cast<size_t>(TextEditor::Color::matchingBracketLevel2)] = IM_COL32(0xDA, 0x70, 0xD6, 255);
            palette[static_cast<size_t>(TextEditor::Color::matchingBracketLevel3)] = IM_COL32(0x17, 0x9F, 0xFF, 255);
            palette[static_cast<size_t>(TextEditor::Color::matchingBracketError)] = IM_COL32(0xFF, 0x12, 0x12, 0xCC);
            palette[static_cast<size_t>(TextEditor::Color::lineNumber)] = IM_COL32(0x85, 0x85, 0x85, 255);
            palette[static_cast<size_t>(TextEditor::Color::currentLineNumber)] = IM_COL32(0xC6, 0xC6, 0xC6, 255);
            return palette;
        }

        int matchQuality(std::string_view text, std::string_view partial)
        {
            if (partial.empty())
                return 0;
            if (text.starts_with(partial))
                return text == partial ? 0 : 1;
            if (partial.size() <= text.size())
            {
                bool prefix = true;
                for (size_t i = 0; i < partial.size(); ++i)
                {
                    prefix &= std::tolower(static_cast<unsigned char>(text[i])) ==
                        std::tolower(static_cast<unsigned char>(partial[i]));
                }
                if (prefix)
                    return 2;
            }
            return -1;
        }

        struct BytePosition
        {
            std::size_t byteOffset = 0;
            lang::SourcePoint point;
        };

        BytePosition bytePositionForDocPos(
            std::string_view source,
            TextEditor::DocPos position
        )
        {
            std::size_t lineStart = 0;
            for (std::size_t line = 0; line < position.line; ++line)
            {
                const std::size_t newline = source.find('\n', lineStart);
                if (newline == std::string_view::npos)
                {
                    return BytePosition{
                        source.size(),
                        lang::SourcePoint{ static_cast<std::uint32_t>(position.line), 0 },
                    };
                }
                lineStart = newline + 1;
            }
            const std::size_t lineEnd = source.find('\n', lineStart);
            const std::string_view line = source.substr(
                lineStart,
                lineEnd == std::string_view::npos ? source.size() - lineStart : lineEnd - lineStart
            );
            const std::size_t byteColumn = lang::utf8ByteOffsetForCodePoint(line, position.index);
            return BytePosition{
                lineStart + byteColumn,
                lang::SourcePoint{
                    static_cast<std::uint32_t>(position.line),
                    static_cast<std::uint32_t>(byteColumn),
                },
            };
        }

        std::size_t byteOffsetForDocPos(
            std::string_view source,
            TextEditor::DocPos position
        )
        {
            return bytePositionForDocPos(source, position).byteOffset;
        }

        // Inverse of byteOffsetForDocPos — used to turn a Symbol::declaration byte offset (from
        // findSymbolAt) back into a DocPos so go-to-declaration can hand it to editor.SetCursor().
        TextEditor::DocPos docPosForByteOffset(
            std::string_view source,
            std::size_t byteOffset
        )
        {
            byteOffset = std::min(byteOffset, source.size());
            std::size_t line = 0;
            std::size_t lineStart = 0;
            for (std::size_t newline = source.find('\n');
                 newline != std::string_view::npos && newline < byteOffset;
                 newline = source.find('\n', lineStart))
            {
                lineStart = newline + 1;
                ++line;
            }
            const std::size_t codePointColumn =
                lang::countUtf8CodePoints(source.substr(lineStart, byteOffset - lineStart));
            return TextEditor::DocPos(line, codePointColumn);
        }

        // Replays changes onto workingText (the text as it was right before this transaction),
        // producing the SourceEdit list tree-sitter's incremental reparse needs, and mutating
        // workingText so it ends up matching the real post-transaction text. Each Change's DocPos
        // coordinates only resolve correctly against the document state as it existed right
        // before that specific change, so changes must be replayed strictly in order.
        std::vector<lang::SourceEdit> buildSourceEdits(
            std::string& workingText,
            const std::vector<TextEditor::Change>& changes
        )
        {
            std::vector<lang::SourceEdit> edits;
            edits.reserve(changes.size());

            for (const TextEditor::Change& change : changes)
            {
                if (change.insert)
                {
                    const BytePosition start = bytePositionForDocPos(workingText, change.start);
                    workingText.insert(start.byteOffset, change.text);
                    const BytePosition newEnd = bytePositionForDocPos(workingText, change.end);
                    edits.push_back(lang::SourceEdit{
                        static_cast<std::uint32_t>(start.byteOffset),
                        static_cast<std::uint32_t>(start.byteOffset),
                        static_cast<std::uint32_t>(newEnd.byteOffset),
                        start.point,
                        start.point,
                        newEnd.point,
                    });
                }
                else
                {
                    const BytePosition start = bytePositionForDocPos(workingText, change.start);
                    const BytePosition oldEnd = bytePositionForDocPos(workingText, change.end);
                    workingText.erase(start.byteOffset, oldEnd.byteOffset - start.byteOffset);
                    edits.push_back(lang::SourceEdit{
                        static_cast<std::uint32_t>(start.byteOffset),
                        static_cast<std::uint32_t>(oldEnd.byteOffset),
                        static_cast<std::uint32_t>(start.byteOffset),
                        start.point,
                        oldEnd.point,
                        start.point,
                    });
                }
            }
            return edits;
        }

        TextEditor::Color semanticColor(lang::SymbolKind kind)
        {
            switch (kind)
            {
            case lang::SymbolKind::Type: return TextEditor::Color::knownIdentifier;
            case lang::SymbolKind::Function: return TextEditor::Color::preprocessor;
            case lang::SymbolKind::Field: return TextEditor::Color::text;
            case lang::SymbolKind::Parameter: return TextEditor::Color::parameter;
            case lang::SymbolKind::LocalVariable: return TextEditor::Color::identifier;
            case lang::SymbolKind::GlobalVariable: return TextEditor::Color::globalVariable;
            case lang::SymbolKind::EnumMember: return TextEditor::Color::enumMember;
            case lang::SymbolKind::Macro: return TextEditor::Color::macro;
            }
            return TextEditor::Color::identifier;
        }

        // A type name in a hover tooltip should match how the live editor colors that same text.
        // Built-in types (float3, int, ...) are colorized as language.declarations (VS Code's
        // storage.type, blue); only user-declared struct/enum names go through the tree-sitter
        // semantic pass and get Color::knownIdentifier (teal/green, matching Type in
        // semanticColor() above).
        TextEditor::Color typeNameColor(const TextEditor::Language& language, const std::string& typeName)
        {
            return language.declarations.contains(typeName)
                ? TextEditor::Color::declaration
                : TextEditor::Color::knownIdentifier;
        }

        // Renders one run of text in a palette color, for building up a hover tooltip out of
        // several differently-colored pieces glued together on one line.
        void renderHoverPiece(const TextEditor::Palette& palette, TextEditor::Color color, std::string_view text)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, palette.get(color));
            ImGui::TextUnformatted(text.data(), text.data() + text.size());
            ImGui::PopStyleColor();
        }

        ImU32 diagnosticColor(lang::SlangDiagnosticSeverity severity)
        {
            switch (severity)
            {
            case lang::SlangDiagnosticSeverity::Info:
                return IM_COL32(0x75, 0xBE, 0xFF, 0xDD);
            case lang::SlangDiagnosticSeverity::Warning:
                return IM_COL32(0xE5, 0xC0, 0x7B, 0xDD);
            case lang::SlangDiagnosticSeverity::Error:
            case lang::SlangDiagnosticSeverity::Fatal:
                return IM_COL32(0xFF, 0x12, 0x12, 0xCC);
            }
            return IM_COL32(0xFF, 0x12, 0x12, 0xCC);
        }
    }

    SlangCodeEditor::SlangCodeEditor(std::string preamble, std::string initialText) :
        mPreamble(std::move(preamble))
    {
        mEditor.SetLanguage(&sharedLanguage());
        mEditor.SetPalette(buildPalette());
        mEditor.SetInsertSpacesOnTabs(true);
        mEditor.SetLineFoldingEnabled(true);
        mEditor.SetText(initialText);

        mEditor.SetTransactionCallback([this](const std::vector<TextEditor::Change>& changes)
        {
            ++mDocumentVersion;
            mLastEditTime = std::chrono::steady_clock::now();
            mEditor.ClearSquiggles();
            mEditor.ApplySemanticTokenChanges(changes);
            mDiagnosticsFailure.clear();
            mSymbolFailure.clear();

            // Replay the transaction's edits onto our own tracked copy of the pre-edit text. If
            // the replay doesn't end up byte-for-byte identical to the editor's real post-edit
            // text, the edits can't be trusted, so fall back to an empty list: updateTree()
            // always treats that as "do a full reparse."
            std::string workingText = mPreviousSource;
            std::vector<lang::SourceEdit> edits = buildSourceEdits(workingText, changes);
            const std::string currentSource = mEditor.GetText();
            if (workingText != currentSource)
                edits.clear();

            mSymbolAnalyzer->updateTree(currentSource, edits, nullptr, &mSymbolFailure);
            if (mSymbolFailure.empty())
                mSymbolScanPending = true;
            mPreviousSource = currentSource;
        });

        mAutoCompleteConfig.callback = [this](TextEditor::AutoCompleteState& state)
        {
            populateSuggestions(state);
        };
        mAutoCompleteConfig.triggerCharacters = { '.' };
        mEditor.SetAutoCompleteConfig(&mAutoCompleteConfig);

        mEditor.SetTextHoverCallback([this](TextEditor::PopupData& data)
        {
            showHoverPopup(data);
        });

        resetAnalysis();
    }

    SlangCodeEditor::~SlangCodeEditor() = default;

    void SlangCodeEditor::setText(const std::string& text)
    {
        mEditor.SetText(text);
        ++mDocumentVersion;
        resetAnalysis();
    }

    std::string SlangCodeEditor::getText() const
    {
        return mEditor.GetText();
    }

    void SlangCodeEditor::setPreamble(std::string preamble)
    {
        mPreamble = std::move(preamble);
        resetAnalysis();
    }

    void SlangCodeEditor::resetAnalysis()
    {
        mPreambleLineCount = static_cast<int>(std::count(mPreamble.begin(), mPreamble.end(), '\n'));
        mSymbolAnalyzer = std::make_unique<lang::SymbolAnalyzer>(mPreamble, mPreambleLineCount);
        mDiagnosticsService = std::make_unique<lang::DiagnosticsService>(mPreamble, mPreambleLineCount);

        mPreviousSource = mEditor.GetText();
        lang::SymbolAnalysisResult initial = mSymbolAnalyzer->analyze(mPreviousSource);
        mSymbolFailure = initial.failureMessage;
        applySymbolAnalysisResult(std::move(initial));

        mEditor.ClearSquiggles();
        mDiagnosticsFailure.clear();
        ++mDocumentVersion;
        mDiagnosticsRequestedVersion = mDocumentVersion;
        mDiagnosticsService->request(mDocumentVersion, mEditor.GetText());
    }

    void SlangCodeEditor::applySymbolAnalysisResult(lang::SymbolAnalysisResult result)
    {
        const std::string analyzedSource = mEditor.GetText();
        std::vector<TextEditor::SemanticToken> semanticTokens;
        for (const lang::PositionedSemanticToken& token :
             lang::positionSemanticTokens(analyzedSource, result.symbolIndex.semanticTokens))
        {
            semanticTokens.emplace_back(
                TextEditor::DocPos(token.line, token.startCodePoint),
                TextEditor::DocPos(token.line, token.endCodePoint),
                semanticColor(token.kind)
            );
        }
        mEditor.SetSemanticTokens(std::move(semanticTokens));
        mSymbolIndex = std::move(result.symbolIndex);
        ++mSymbolIndexGeneration;
    }

    void SlangCodeEditor::populateSuggestions(TextEditor::AutoCompleteState& state)
    {
        state.suggestions.clear();

        struct RankedSuggestion
        {
            std::string name;
            int quality;
            int category;
        };

        const std::string source = mEditor.GetText();
        const std::size_t cursorByte = byteOffsetForDocPos(source, state.searchTermStart);
        const bool memberAccess = cursorByte > 0 && source[cursorByte - 1] == '.';
        if (!state.inIdentifier && !memberAccess)
            return;

        const std::vector<lang::CompletionCandidate> universe =
            lang::collectCompletionCandidateUniverse(mSymbolIndex, source, cursorByte);
        const bool globalScope = lang::isGlobalScope(mSymbolIndex, cursorByte);
        const std::vector<lang::CompletionCandidate> candidates =
            lang::filterCompletionCandidates(universe, state.searchTerm);

        std::vector<RankedSuggestion> ranked;
        ranked.reserve(candidates.size() + 64);
        for (const lang::CompletionCandidate& candidate : candidates)
        {
            ranked.push_back(RankedSuggestion{
                candidate.name,
                candidate.matchQuality,
                candidate.categoryPriority,
            });
        }

        // language->keywords holds statement-only control-flow keywords (if/for/while/...), which
        // aren't valid at file scope — only inside a function body, where a statement is actually
        // expected.
        if (!memberAccess && !globalScope)
        {
            if (const TextEditor::Language* language = mEditor.GetLanguage())
            {
                for (const std::string& keyword : language->keywords)
                {
                    const int quality = matchQuality(keyword, state.searchTerm);
                    if (quality >= 0)
                        ranked.push_back(RankedSuggestion{ keyword, quality, 8 });
                }
            }
        }

        std::ranges::sort(ranked, [](const RankedSuggestion& left, const RankedSuggestion& right)
        {
            if (left.quality != right.quality)
                return left.quality < right.quality;
            if (left.category != right.category)
                return left.category < right.category;
            return left.name < right.name;
        });

        std::unordered_set<std::string> emitted;
        for (RankedSuggestion& suggestion : ranked)
        {
            if (emitted.insert(suggestion.name).second)
                state.suggestions.push_back(std::move(suggestion.name));
        }
    }

    void SlangCodeEditor::showHoverPopup(TextEditor::PopupData& data)
    {
        const std::string source = mEditor.GetText();
        const std::string word = mEditor.GetWordAtMousePos(ImGui::GetMousePos());
        const std::size_t byteOffset = byteOffsetForDocPos(source, data.pos);
        const std::optional<lang::SymbolHover> hover =
            word.empty() ? std::nullopt : lang::findSymbolAt(mSymbolIndex, source, byteOffset, word);

        // Go-to-declaration, independent of the hover-tooltip delay below. external symbols (see
        // SymbolHover) come from the preamble, which isn't part of the visible document, so
        // there's nowhere to jump to.
        if (hover && !hover->symbol.external &&
            ImGui::GetIO().KeyCtrl && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            mEditor.SetCursor(docPosForByteOffset(source, hover->symbol.declaration.startByte));
            ImGui::CloseCurrentPopup();
            return;
        }

        // Underline the hovered word as a "this is Ctrl+Click-able" affordance, matching the
        // go-to-declaration eligibility check above exactly.
        if (hover && !hover->symbol.external && ImGui::GetIO().KeyCtrl)
        {
            const ImVec2 wordStart = mEditor.GetScreenPosForDocPos(data.pos);
            const ImFontBaked* baked = ImGui::GetFontBaked();
            const float baselineY = wordStart.y + (baked ? baked->Ascent : mEditor.GetLineHeight());
            const float underlineY = baselineY + 2.0f;
            const float underlineEndX = wordStart.x + mEditor.GetGlyphWidth() * static_cast<float>(word.size());
            ImGui::GetForegroundDrawList()->AddLine(
                ImVec2(wordStart.x, underlineY),
                ImVec2(underlineEndX, underlineY),
                mEditor.GetPalette().get(TextEditor::Color::declaration)
            );
        }

        if (data.pos != mHoveredDocPos)
        {
            mHoveredDocPos = data.pos;
            mHoverStartTime = std::chrono::steady_clock::now();
        }
        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - mHoverStartTime
        ).count();
        if (elapsedMs < kHoverDelayMs)
        {
            ImGui::CloseCurrentPopup();
            return;
        }

        const TextEditor::Palette& palette = mEditor.GetPalette();
        const TextEditor::Language& language = sharedLanguage();
        const auto glue = []() { ImGui::SameLine(0.0f, 0.0f); };

        const auto renderFunctionSignature = [&](const lang::FunctionSignature& signature)
        {
            renderHoverPiece(palette, typeNameColor(language, signature.returnType), signature.returnType + " ");
            glue();
            renderHoverPiece(palette, semanticColor(lang::SymbolKind::Function), signature.name);
            glue();
            renderHoverPiece(palette, TextEditor::Color::punctuation, "(");
            for (std::size_t i = 0; i < signature.parameters.size(); ++i)
            {
                if (i > 0)
                {
                    glue();
                    renderHoverPiece(palette, TextEditor::Color::punctuation, ", ");
                }
                glue();
                renderHoverPiece(
                    palette,
                    typeNameColor(language, signature.parameters[i].typeName),
                    signature.parameters[i].typeName + " "
                );
                glue();
                renderHoverPiece(
                    palette, semanticColor(lang::SymbolKind::Parameter), signature.parameters[i].name
                );
            }
            glue();
            renderHoverPiece(palette, TextEditor::Color::punctuation, ")");
        };

        // No declared symbol at all — the only other things worth a tooltip are a builtin
        // intrinsic (dot, reflect, ...) or a texture resource method (Sample, Load, ...), neither
        // of which has a declaration to resolve via findSymbolAt.
        if (!hover)
        {
            std::optional<lang::FunctionSignature> builtin = lang::describeBuiltinFunctionSignature(word);
            if (!builtin)
                builtin = lang::describeBuiltinTextureMethodSignature(word);
            if (builtin)
                renderFunctionSignature(*builtin);
            else
                ImGui::CloseCurrentPopup();
            return;
        }

        // A type name hovered on its own has nothing more to say than the identifier itself
        // already shows, so skip the popup entirely rather than echo it back.
        if (hover->symbol.kind == lang::SymbolKind::Type)
        {
            ImGui::CloseCurrentPopup();
            return;
        }

        const lang::Symbol& symbol = hover->symbol;

        switch (symbol.kind)
        {
        case lang::SymbolKind::Function:
        {
            const std::optional<lang::FunctionSignature> signature = lang::describeFunctionSignature(
                symbol.external ? mSymbolAnalyzer->getExternalIndex() : mSymbolIndex, symbol
            );
            if (!signature)
            {
                ImGui::CloseCurrentPopup();
                return;
            }
            renderFunctionSignature(*signature);
            break;
        }
        case lang::SymbolKind::Field:
            renderHoverPiece(palette, typeNameColor(language, symbol.typeName), symbol.typeName + " ");
            glue();
            renderHoverPiece(palette, TextEditor::Color::knownIdentifier, hover->ownerTypeName);
            glue();
            renderHoverPiece(palette, TextEditor::Color::punctuation, "::");
            glue();
            renderHoverPiece(palette, semanticColor(lang::SymbolKind::Field), symbol.name);
            break;
        case lang::SymbolKind::EnumMember:
            renderHoverPiece(palette, TextEditor::Color::knownIdentifier, symbol.typeName);
            glue();
            renderHoverPiece(palette, TextEditor::Color::punctuation, "::");
            glue();
            renderHoverPiece(palette, semanticColor(lang::SymbolKind::EnumMember), symbol.name);
            break;
        default:
            if (!symbol.typeName.empty())
            {
                renderHoverPiece(palette, typeNameColor(language, symbol.typeName), symbol.typeName + " ");
                glue();
            }
            renderHoverPiece(palette, semanticColor(symbol.kind), symbol.name);
            break;
        }
    }

    bool SlangCodeEditor::render(const char* id, const ImVec2& size)
    {
        const auto sinceEdit = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - mLastEditTime
        ).count();

        if (mSymbolScanPending && sinceEdit >= kSymbolScanDebounceMs)
        {
            mSymbolScanPending = false;
            lang::SymbolAnalysisResult scanned = mSymbolAnalyzer->scanCurrentTree(mEditor.GetText());
            applySymbolAnalysisResult(std::move(scanned));
        }

        if (mDiagnosticsRequestedVersion != mDocumentVersion && sinceEdit >= kDiagnosticsDebounceMs)
        {
            mDiagnosticsRequestedVersion = mDocumentVersion;
            mDiagnosticsService->request(mDocumentVersion, mEditor.GetText());
        }

        if (std::optional<lang::DiagnosticsResult> result = mDiagnosticsService->takeLatestResult())
        {
            if (result->documentVersion == mDocumentVersion)
            {
                const std::string analyzedSource = mEditor.GetText();
                mDiagnosticsFailure = std::move(result->failureMessage);

                mEditor.ClearSquiggles();
                for (const lang::SlangDiagnostic& diagnostic : result->diagnostics)
                {
                    const std::optional<lang::DiagnosticRange> range =
                        lang::normalizeDiagnosticRange(analyzedSource, diagnostic);
                    if (!range)
                        continue;

                    const std::string tooltip = diagnostic.code.empty()
                        ? diagnostic.message
                        : diagnostic.code + ": " + diagnostic.message;
                    mEditor.AddSquiggle(
                        TextEditor::DocPos(range->line, range->startCodePoint),
                        TextEditor::DocPos(range->line, range->endCodePoint),
                        diagnosticColor(diagnostic.severity),
                        tooltip
                    );
                }
            }
        }

        if (!mSymbolFailure.empty())
            ImGui::TextColored(ImVec4(1.0f, 0.25f, 0.25f, 1.0f), "%s", mSymbolFailure.c_str());
        if (!mDiagnosticsFailure.empty())
            ImGui::TextColored(ImVec4(1.0f, 0.25f, 0.25f, 1.0f), "%s", mDiagnosticsFailure.c_str());

        // mDocumentVersion is bumped exactly once per real edit, by the transaction callback
        // wired up in the constructor — cheaper than comparing GetText() before/after every
        // frame just to detect whether this Render() call produced an edit.
        const std::uint64_t versionBeforeRender = mDocumentVersion;
        ImFont* monospaceFont = ImGuiFrontend::getMonospaceFont();
        ImGui::PushFont(monospaceFont);
        mEditor.Render(id, size);
        ImGui::PopFont();
        return mDocumentVersion != versionBeforeRender;
    }
}
