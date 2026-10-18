#pragma once

#include <LanguageIntelligence/Providers/SlangProvider.h>

#include <TextEditor.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

namespace hyp::ed
{
    // Embeds the Slang-aware TextEditor (syntax highlighting, autocomplete, hover, live
    // diagnostics — the machinery originally proven out in CodeEditorTest.cpp) as a reusable,
    // per-instance component. Unlike CodeEditorTest's function-level statics, every piece of
    // state here is a member, since callers (e.g. MaterialTemplateEditor) are not singletons and
    // may have several instances of this editor open at once, each editing different text with a
    // different preamble.
    class SlangCodeEditor
    {
    public:
        // preamble is invisible boilerplate prepended ahead of the user's own text before every
        // parse/compile pass (e.g. struct declarations the user's code is expected to reference)
        // — see setPreamble() for when to refresh it. initialText seeds the editor's text buffer
        // before the first analysis pass runs, so callers that already know their starting content
        // (e.g. a freshly opened asset's saved source) don't have to follow up with a setText()
        // call — see setText()'s doc comment for why doing so is actively harmful, not just
        // redundant, when it immediately follows construction.
        explicit SlangCodeEditor(std::string preamble, std::string initialText = std::string());
        ~SlangCodeEditor();

        SlangCodeEditor(const SlangCodeEditor&) = delete;
        SlangCodeEditor& operator=(const SlangCodeEditor&) = delete;

        // Replaces the editor's text outright. Not for reflecting per-keystroke edits — those
        // flow through the editor's own transaction callback. Don't call this immediately after
        // construction to seed initial content — pass it as the constructor's initialText
        // argument instead: this re-runs resetAnalysis(), which tears down and rebuilds
        // mDiagnosticsService; destroying the one the constructor just created synchronously
        // joins its background worker thread, and if that worker is still mid-analyze() (up to
        // DiagnosticsService's ~1.5s diagnostics-wait timeout, most likely on an empty/near-empty
        // first-pass document) this call blocks the caller for that entire duration instead of
        // the near-instant assignment it looks like.
        void setText(const std::string& text);

        // TextEditor::GetText() rebuilds the string from its internal line storage on every call
        // (no cached copy to reference), so this returns by value rather than by reference.
        std::string getText() const;

        // Rebuilds the symbol analyzer / diagnostics service against a new preamble, without
        // touching the current text buffer. Call this whenever whatever the preamble declares
        // (e.g. a material's parameter list) changes shape — the preamble is otherwise fixed at
        // construction/last call, and analysis results would silently drift from the current
        // parameter set if it weren't refreshed here.
        void setPreamble(std::string preamble);

        // Draws the editor into the current ImGui window/child region at the given size. Returns
        // true if the text changed since the previous render() call (mirrors
        // ImGui::InputTextMultiline's return value), so callers can drive their own "modified"
        // bookkeeping the same way they did with the plain-text widget.
        bool render(const char* id, const ImVec2& size);

    private:
        void resetAnalysis();
        void applySymbolAnalysisResult(lang::SymbolAnalysisResult result);
        void populateSuggestions(TextEditor::AutoCompleteState& state);
        void showHoverPopup(TextEditor::PopupData& data);

        std::string mPreamble;
        int mPreambleLineCount = 0;

        TextEditor mEditor;
        TextEditor::AutoCompleteConfig mAutoCompleteConfig;

        std::unique_ptr<lang::SymbolAnalyzer> mSymbolAnalyzer;
        std::unique_ptr<lang::DiagnosticsService> mDiagnosticsService;
        lang::SymbolIndex mSymbolIndex;
        std::uint64_t mSymbolIndexGeneration = 0;

        std::uint64_t mDocumentVersion = 0;
        std::uint64_t mDiagnosticsRequestedVersion = 0;
        std::chrono::steady_clock::time_point mLastEditTime = std::chrono::steady_clock::now();
        std::string mSymbolFailure;
        std::string mDiagnosticsFailure;
        bool mSymbolScanPending = false;
        static constexpr int kSymbolScanDebounceMs = 30;
        static constexpr int kDiagnosticsDebounceMs = 500;

        // Text as it was right before the in-flight transaction — replayed against Change events
        // to compute byte-accurate SourceEdits for tree-sitter's incremental reparse. See
        // buildSourceEdits() in the .cpp for why replay order matters.
        std::string mPreviousSource;

        TextEditor::DocPos mHoveredDocPos;
        std::chrono::steady_clock::time_point mHoverStartTime = std::chrono::steady_clock::now();
        static constexpr int kHoverDelayMs = 400;
    };
}
