#include "LspClient.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

namespace hyp::lang
{
    using Json = nlohmann::json;

    namespace
    {
        Json positionToJson(LspPosition position)
        {
            return Json{ { "line", position.line }, { "character", position.character } };
        }

        LspPosition positionFromJson(const Json& json)
        {
            LspPosition position;
            if (json.contains("line") && json["line"].is_number())
                position.line = json["line"].get<std::uint32_t>();
            if (json.contains("character") && json["character"].is_number())
                position.character = json["character"].get<std::uint32_t>();
            return position;
        }

        LspRange rangeFromJson(const Json& json)
        {
            LspRange range;
            if (json.contains("start"))
                range.start = positionFromJson(json["start"]);
            if (json.contains("end"))
                range.end = positionFromJson(json["end"]);
            return range;
        }

        LspDiagnostic diagnosticFromJson(const Json& json)
        {
            LspDiagnostic diagnostic;
            if (json.contains("range"))
                diagnostic.range = rangeFromJson(json["range"]);
            if (json.contains("severity") && json["severity"].is_number())
            {
                const int value = json["severity"].get<int>();
                diagnostic.severity = (value >= 1 && value <= 4)
                    ? static_cast<DiagnosticSeverity>(value)
                    : DiagnosticSeverity::Error;
            }
            if (json.contains("code"))
            {
                if (json["code"].is_string())
                    diagnostic.code = json["code"].get<std::string>();
                else if (json["code"].is_number())
                    diagnostic.code = std::to_string(json["code"].get<long long>());
            }
            if (json.contains("message") && json["message"].is_string())
                diagnostic.message = json["message"].get<std::string>();
            if (json.contains("relatedInformation") && json["relatedInformation"].is_array())
            {
                for (const Json& related : json["relatedInformation"])
                {
                    LspRelatedInformation info;
                    if (related.contains("location"))
                    {
                        const Json& location = related["location"];
                        if (location.contains("uri") && location["uri"].is_string())
                            info.uri = location["uri"].get<std::string>();
                        if (location.contains("range"))
                            info.range = rangeFromJson(location["range"]);
                    }
                    if (related.contains("message") && related["message"].is_string())
                        info.message = related["message"].get<std::string>();
                    diagnostic.relatedInformation.push_back(std::move(info));
                }
            }
            return diagnostic;
        }

        LspCompletionItem completionItemFromJson(const Json& json)
        {
            LspCompletionItem item;
            if (json.contains("label") && json["label"].is_string())
                item.label = json["label"].get<std::string>();
            if (json.contains("kind") && json["kind"].is_number())
                item.kind = static_cast<LspCompletionItemKind>(json["kind"].get<int>());
            if (json.contains("detail") && json["detail"].is_string())
                item.detail = json["detail"].get<std::string>();
            return item;
        }

        struct PendingRequest
        {
            std::mutex mutex;
            std::condition_variable condition;
            bool done = false;
            Json result;
            bool isError = false;
        };
    }

    struct LspClient::Impl
    {
        LspServerConfig config;
        std::atomic<bool> running{ false };
        std::atomic<int> nextId{ 1 };

#ifdef _WIN32
        HANDLE process = nullptr;
        HANDLE stdinWrite = nullptr;
        HANDLE stdoutRead = nullptr;
#endif
        std::thread readerThread;
        std::atomic<bool> stopping{ false };

        std::mutex writeMutex;

        std::mutex pendingMutex;
        std::unordered_map<int, std::shared_ptr<PendingRequest>> pending;

        struct DiagnosticsEntry
        {
            std::vector<LspDiagnostic> diagnostics;
            std::uint64_t generation = 0;
        };

        mutable std::mutex diagnosticsMutex;
        mutable std::condition_variable diagnosticsCondition;
        std::unordered_map<std::string, DiagnosticsEntry> diagnosticsByUri;

        explicit Impl(LspServerConfig config_) : config(std::move(config_)) {}
        ~Impl() { stop(); }

        bool launchProcess();
        void closeHandles();
        void readerLoop();
        bool writeMessage(const Json& message);
        std::optional<Json> sendRequest(const std::string& method, const Json& params);
        void sendNotification(const std::string& method, const Json& params);
        void handleServerRequest(const Json& message);
        void handleNotification(const Json& message);
        void stop();
    };

#ifdef _WIN32
    bool LspClient::Impl::launchProcess()
    {
        SECURITY_ATTRIBUTES security{};
        security.nLength = sizeof(security);
        security.bInheritHandle = TRUE;

        HANDLE childStdinRead = nullptr;
        HANDLE childStdoutWrite = nullptr;
        if (!CreatePipe(&childStdinRead, &stdinWrite, &security, 0))
            return false;
        if (!SetHandleInformation(stdinWrite, HANDLE_FLAG_INHERIT, 0))
        {
            CloseHandle(childStdinRead);
            CloseHandle(stdinWrite);
            return false;
        }
        if (!CreatePipe(&stdoutRead, &childStdoutWrite, &security, 0))
        {
            CloseHandle(childStdinRead);
            CloseHandle(stdinWrite);
            return false;
        }
        if (!SetHandleInformation(stdoutRead, HANDLE_FLAG_INHERIT, 0))
        {
            CloseHandle(childStdinRead);
            CloseHandle(stdinWrite);
            CloseHandle(stdoutRead);
            CloseHandle(childStdoutWrite);
            return false;
        }

        std::ostringstream commandLine;
        commandLine << '"' << config.executablePath << '"';
        for (const std::string& argument : config.arguments)
            commandLine << " \"" << argument << '"';
        std::string commandLineStr = commandLine.str();

        STARTUPINFOA startupInfo{};
        startupInfo.cb = sizeof(startupInfo);
        startupInfo.dwFlags = STARTF_USESTDHANDLES;
        startupInfo.hStdInput = childStdinRead;
        startupInfo.hStdOutput = childStdoutWrite;
        startupInfo.hStdError = GetStdHandle(STD_ERROR_HANDLE);

        PROCESS_INFORMATION processInfo{};
        const BOOL created = CreateProcessA(
            nullptr,
            commandLineStr.data(),
            nullptr,
            nullptr,
            TRUE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &startupInfo,
            &processInfo
        );

        CloseHandle(childStdinRead);
        CloseHandle(childStdoutWrite);

        if (!created)
        {
            CloseHandle(stdinWrite);
            CloseHandle(stdoutRead);
            stdinWrite = nullptr;
            stdoutRead = nullptr;
            return false;
        }

        CloseHandle(processInfo.hThread);
        process = processInfo.hProcess;
        return true;
    }

    void LspClient::Impl::closeHandles()
    {
        if (stdinWrite)
        {
            CloseHandle(stdinWrite);
            stdinWrite = nullptr;
        }
        if (stdoutRead)
        {
            CloseHandle(stdoutRead);
            stdoutRead = nullptr;
        }
        if (process)
        {
            TerminateProcess(process, 0);
            WaitForSingleObject(process, 2000);
            CloseHandle(process);
            process = nullptr;
        }
    }
#else
    bool LspClient::Impl::launchProcess()
    {
        // Only a Windows implementation exists so far — no supported target platform in this
        // codebase currently ships a language server to drive, so a POSIX fork/exec + pipe
        // implementation has not been written yet.
        return false;
    }

    void LspClient::Impl::closeHandles() {}
#endif

    bool LspClient::Impl::writeMessage(const Json& message)
    {
#ifdef _WIN32
        if (!stdinWrite)
            return false;
        const std::string body = message.dump();
        const std::string header = "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n";

        std::lock_guard<std::mutex> lock(writeMutex);
        DWORD written = 0;
        if (!WriteFile(stdinWrite, header.data(), static_cast<DWORD>(header.size()), &written, nullptr))
            return false;
        if (!WriteFile(stdinWrite, body.data(), static_cast<DWORD>(body.size()), &written, nullptr))
            return false;
        return true;
#else
        (void)message;
        return false;
#endif
    }

    void LspClient::Impl::readerLoop()
    {
#ifdef _WIN32
        std::string buffer;
        char chunk[4096];

        while (!stopping.load())
        {
            DWORD bytesRead = 0;
            if (!stdoutRead || !ReadFile(stdoutRead, chunk, sizeof(chunk), &bytesRead, nullptr) || bytesRead == 0)
                break;
            buffer.append(chunk, bytesRead);

            while (true)
            {
                const std::size_t headerEnd = buffer.find("\r\n\r\n");
                if (headerEnd == std::string::npos)
                    break;

                std::size_t contentLength = 0;
                bool foundLength = false;
                std::size_t lineStart = 0;
                while (lineStart < headerEnd)
                {
                    std::size_t lineEnd = buffer.find("\r\n", lineStart);
                    if (lineEnd == std::string::npos || lineEnd > headerEnd)
                        lineEnd = headerEnd;
                    const std::string line = buffer.substr(lineStart, lineEnd - lineStart);
                    constexpr std::string_view prefix = "Content-Length:";
                    if (line.size() > prefix.size() &&
                        std::equal(prefix.begin(), prefix.end(), line.begin(), [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); }))
                    {
                        try
                        {
                            contentLength = static_cast<std::size_t>(std::stoul(line.substr(prefix.size())));
                            foundLength = true;
                        }
                        catch (...)
                        {
                        }
                    }
                    lineStart = lineEnd + 2;
                }

                if (!foundLength)
                {
                    buffer.erase(0, headerEnd + 4);
                    continue;
                }

                const std::size_t bodyStart = headerEnd + 4;
                if (buffer.size() < bodyStart + contentLength)
                    break;

                const std::string body = buffer.substr(bodyStart, contentLength);
                buffer.erase(0, bodyStart + contentLength);

                Json message;
                try
                {
                    message = Json::parse(body);
                }
                catch (...)
                {
                    continue;
                }

                const bool hasId = message.contains("id");
                const bool hasMethod = message.contains("method");
                const bool hasResultOrError = message.contains("result") || message.contains("error");

                if (hasId && hasResultOrError && !hasMethod)
                {
                    const int id = message["id"].is_number() ? message["id"].get<int>() : -1;
                    std::shared_ptr<PendingRequest> request;
                    {
                        std::lock_guard<std::mutex> lock(pendingMutex);
                        auto it = pending.find(id);
                        if (it != pending.end())
                        {
                            request = it->second;
                            pending.erase(it);
                        }
                    }
                    if (request)
                    {
                        std::lock_guard<std::mutex> requestLock(request->mutex);
                        request->isError = message.contains("error");
                        request->result = request->isError ? message["error"] : message.value("result", Json());
                        request->done = true;
                        request->condition.notify_all();
                    }
                }
                else if (hasMethod && hasId)
                {
                    handleServerRequest(message);
                }
                else if (hasMethod)
                {
                    handleNotification(message);
                }
            }
        }
#endif
    }

    void LspClient::Impl::handleServerRequest(const Json& message)
    {
        // We don't implement workspace configuration or dynamic capability registration;
        // responding with an empty/null result is enough to keep the server from blocking on
        // a reply it will never otherwise receive.
        const std::string method = message.value("method", "");
        Json response{
            { "jsonrpc", "2.0" },
            { "id", message["id"] },
        };
        if (method == "workspace/configuration" && message.contains("params") &&
            message["params"].contains("items") && message["params"]["items"].is_array())
        {
            response["result"] = Json::array();
            for (std::size_t i = 0; i < message["params"]["items"].size(); ++i)
                response["result"].push_back(nullptr);
        }
        else
        {
            response["result"] = Json::object();
        }
        writeMessage(response);
    }

    void LspClient::Impl::handleNotification(const Json& message)
    {
        const std::string method = message.value("method", "");
        if (method != "textDocument/publishDiagnostics" || !message.contains("params"))
            return;

        const Json& params = message["params"];
        const std::string uri = params.value("uri", "");
        if (uri.empty())
            return;

        std::vector<LspDiagnostic> diagnostics;
        if (params.contains("diagnostics") && params["diagnostics"].is_array())
        {
            for (const Json& diagnosticJson : params["diagnostics"])
                diagnostics.push_back(diagnosticFromJson(diagnosticJson));
        }

        {
            std::lock_guard<std::mutex> lock(diagnosticsMutex);
            DiagnosticsEntry& entry = diagnosticsByUri[uri];
            entry.diagnostics = std::move(diagnostics);
            ++entry.generation;
        }
        diagnosticsCondition.notify_all();
    }

    std::optional<Json> LspClient::Impl::sendRequest(const std::string& method, const Json& params)
    {
        const int id = nextId.fetch_add(1);
        auto request = std::make_shared<PendingRequest>();
        {
            std::lock_guard<std::mutex> lock(pendingMutex);
            pending[id] = request;
        }

        Json message{
            { "jsonrpc", "2.0" },
            { "id", id },
            { "method", method },
            { "params", params },
        };
        if (!writeMessage(message))
        {
            std::lock_guard<std::mutex> lock(pendingMutex);
            pending.erase(id);
            return std::nullopt;
        }

        std::unique_lock<std::mutex> resultLock(request->mutex);
        const bool completed = request->condition.wait_for(
            resultLock,
            std::chrono::milliseconds(config.requestTimeoutMs),
            [&] { return request->done; }
        );
        if (!completed)
        {
            std::lock_guard<std::mutex> lock(pendingMutex);
            pending.erase(id);
            return std::nullopt;
        }
        if (request->isError)
            return std::nullopt;
        return request->result;
    }

    void LspClient::Impl::sendNotification(const std::string& method, const Json& params)
    {
        Json message{
            { "jsonrpc", "2.0" },
            { "method", method },
            { "params", params },
        };
        writeMessage(message);
    }

    void LspClient::Impl::stop()
    {
        if (!running.exchange(false))
            return;
        stopping = true;
        closeHandles();
        if (readerThread.joinable())
            readerThread.join();
    }

    LspClient::LspClient(LspServerConfig config) : mImpl(std::make_unique<Impl>(std::move(config))) {}
    LspClient::~LspClient() = default;

    bool LspClient::start()
    {
        if (mImpl->running.load())
            return true;
        if (!mImpl->launchProcess())
            return false;

        mImpl->stopping = false;
        mImpl->running = true;
        mImpl->readerThread = std::thread([this] { mImpl->readerLoop(); });

        Json capabilities{
            { "textDocument", {
                { "completion", { { "completionItem", { { "snippetSupport", false } } } } },
                { "publishDiagnostics", { { "relatedInformation", true } } },
                { "synchronization", { { "didSave", false }, { "willSave", false } } },
            } },
        };
        Json initParams{
            { "processId", nullptr },
            { "rootUri", nullptr },
            { "capabilities", capabilities },
        };

        const std::optional<Json> result = mImpl->sendRequest("initialize", initParams);
        if (!result)
        {
            stop();
            return false;
        }
        mImpl->sendNotification("initialized", Json::object());
        return true;
    }

    void LspClient::stop()
    {
        mImpl->stop();
    }

    bool LspClient::isRunning() const
    {
        return mImpl->running.load();
    }

    void LspClient::didOpen(const std::string& uri, std::string text, int version)
    {
        Json params{
            { "textDocument", {
                { "uri", uri },
                { "languageId", mImpl->config.languageId },
                { "version", version },
                { "text", std::move(text) },
            } },
        };
        mImpl->sendNotification("textDocument/didOpen", params);
    }

    void LspClient::didChange(const std::string& uri, std::string text, int version)
    {
        Json params{
            { "textDocument", { { "uri", uri }, { "version", version } } },
            { "contentChanges", Json::array({ Json{ { "text", std::move(text) } } }) },
        };
        mImpl->sendNotification("textDocument/didChange", params);
    }

    void LspClient::didClose(const std::string& uri)
    {
        Json params{ { "textDocument", { { "uri", uri } } } };
        mImpl->sendNotification("textDocument/didClose", params);
    }

    std::optional<std::vector<LspCompletionItem>> LspClient::completion(const std::string& uri, LspPosition position)
    {
        Json params{
            { "textDocument", { { "uri", uri } } },
            { "position", positionToJson(position) },
        };
        const std::optional<Json> result = mImpl->sendRequest("textDocument/completion", params);
        if (!result)
            return std::nullopt;

        const Json& itemsJson = result->is_object() && result->contains("items") ? (*result)["items"] : *result;
        if (!itemsJson.is_array())
            return std::vector<LspCompletionItem>{};

        std::vector<LspCompletionItem> items;
        items.reserve(itemsJson.size());
        for (const Json& itemJson : itemsJson)
            items.push_back(completionItemFromJson(itemJson));
        return items;
    }

    std::vector<LspDiagnostic> LspClient::latestDiagnostics(const std::string& uri) const
    {
        std::lock_guard<std::mutex> lock(mImpl->diagnosticsMutex);
        auto it = mImpl->diagnosticsByUri.find(uri);
        return it != mImpl->diagnosticsByUri.end() ? it->second.diagnostics : std::vector<LspDiagnostic>{};
    }

    std::pair<std::vector<LspDiagnostic>, std::uint64_t> LspClient::diagnosticsSnapshot(const std::string& uri) const
    {
        std::lock_guard<std::mutex> lock(mImpl->diagnosticsMutex);
        auto it = mImpl->diagnosticsByUri.find(uri);
        if (it == mImpl->diagnosticsByUri.end())
            return { {}, 0 };
        return { it->second.diagnostics, it->second.generation };
    }

    std::optional<std::pair<std::vector<LspDiagnostic>, std::uint64_t>> LspClient::waitForDiagnostics(
        const std::string& uri,
        std::uint64_t sinceGeneration,
        int timeoutMs
    ) const
    {
        std::unique_lock<std::mutex> lock(mImpl->diagnosticsMutex);
        const bool arrived = mImpl->diagnosticsCondition.wait_for(
            lock,
            std::chrono::milliseconds(timeoutMs),
            [&]
            {
                auto it = mImpl->diagnosticsByUri.find(uri);
                return it != mImpl->diagnosticsByUri.end() && it->second.generation > sinceGeneration;
            }
        );
        if (!arrived)
            return std::nullopt;
        const Impl::DiagnosticsEntry& entry = mImpl->diagnosticsByUri.at(uri);
        return std::make_pair(entry.diagnostics, entry.generation);
    }
}
