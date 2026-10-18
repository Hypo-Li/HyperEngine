#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace hyp::lang
{
    struct LspPosition
    {
        std::uint32_t line = 0;
        std::uint32_t character = 0;
    };

    struct LspRange
    {
        LspPosition start;
        LspPosition end;
    };

    // Matches the LSP spec's numeric DiagnosticSeverity (1-4); anything else observed on the
    // wire is clamped to Error rather than rejected, since a malformed severity shouldn't hide
    // a real diagnostic from the caller. SlangProvider's own text-diagnostic parser uses the
    // differently-valued SlangDiagnosticSeverity instead, to avoid conflating the two.
    enum class DiagnosticSeverity
    {
        Error = 1,
        Warning = 2,
        Information = 3,
        Hint = 4,
    };

    struct LspRelatedInformation
    {
        std::string uri;
        LspRange range;
        std::string message;
    };

    struct LspDiagnostic
    {
        LspRange range;
        DiagnosticSeverity severity = DiagnosticSeverity::Error;
        std::string code;
        std::string message;
        std::vector<LspRelatedInformation> relatedInformation;
    };

    // Subset of the LSP CompletionItemKind enum actually distinguished by callers so far;
    // extend as needed rather than mapping the full 25-entry spec up front.
    enum class LspCompletionItemKind
    {
        Unknown = 0,
        Function = 3,
        Field = 5,
        Variable = 6,
        Class = 7,
        Interface = 8,
        Module = 9,
        Enum = 13,
        Keyword = 14,
        EnumMember = 20,
        Constant = 21,
        Struct = 22,
        TypeParameter = 25,
    };

    struct LspCompletionItem
    {
        std::string label;
        LspCompletionItemKind kind = LspCompletionItemKind::Unknown;
        std::string detail;
    };

    struct LspServerConfig
    {
        std::string executablePath;
        std::vector<std::string> arguments;
        std::string languageId;
        // Milliseconds a blocking request (initialize/completion/...) waits before giving up.
        int requestTimeoutMs = 5000;
    };

    // A generic LSP client: process lifecycle + JSON-RPC framing + the document-sync/completion/
    // diagnostics subset needed by a source-analysis provider. Carries no knowledge of any
    // specific language server's quirks (that belongs in the provider using this client) and no
    // dependency on Slang, tree-sitter, or the editor UI.
    //
    // Only a Windows process-transport implementation exists so far. On other platforms start()
    // always returns false and every other method is a harmless no-op (empty results, isRunning()
    // stays false) — callers that treat "not running" as "skip diagnostics/completion" degrade
    // safely without any platform-specific handling of their own.
    class LspClient
    {
    public:
        explicit LspClient(LspServerConfig config);
        ~LspClient();

        LspClient(const LspClient&) = delete;
        LspClient& operator=(const LspClient&) = delete;

        // Launches the server process and performs the initialize/initialized handshake.
        // Returns false if the executable could not be launched or initialize timed out/errored.
        bool start();
        void stop();
        bool isRunning() const;

        // uri identifies the document to the server (e.g. a file:// URI, real or synthetic).
        void didOpen(const std::string& uri, std::string text, int version);
        void didChange(const std::string& uri, std::string text, int version);
        void didClose(const std::string& uri);

        // Blocking request; returns nullopt on timeout, transport failure, or a server-side error.
        std::optional<std::vector<LspCompletionItem>> completion(const std::string& uri, LspPosition position);

        // Snapshot of the most recent publishDiagnostics push for this uri; empty if none received.
        std::vector<LspDiagnostic> latestDiagnostics(const std::string& uri) const;

        // Same snapshot as latestDiagnostics(), paired with an opaque generation counter that
        // increases every time a fresh publishDiagnostics notification arrives for uri (starts
        // at 0, meaning "nothing received yet"). Compare against a previously-observed generation
        // to detect a genuinely new push rather than re-reading a stale one.
        std::pair<std::vector<LspDiagnostic>, std::uint64_t> diagnosticsSnapshot(const std::string& uri) const;

        // Blocks until uri's diagnostics generation advances past sinceGeneration, or timeoutMs
        // elapses. publishDiagnostics is server-pushed on its own schedule, not a request/response
        // — this is how a caller correlates "I just sent didChange" with "the server has now
        // reprocessed it", instead of guessing a fixed delay or polling latestDiagnostics() in a
        // loop. Returns nullopt on timeout; the last-known diagnostics remain available via
        // latestDiagnostics()/diagnosticsSnapshot() regardless.
        std::optional<std::pair<std::vector<LspDiagnostic>, std::uint64_t>> waitForDiagnostics(
            const std::string& uri,
            std::uint64_t sinceGeneration,
            int timeoutMs
        ) const;

    private:
        struct Impl;
        std::unique_ptr<Impl> mImpl;
    };
}
