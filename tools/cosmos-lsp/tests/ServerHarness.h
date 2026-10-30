#pragma once

#include "JsonRpc.h"
#include "Server.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <unistd.h>
#include <vector>

namespace CosmosLspTests {

using namespace CosmosLsp;


// Feeds Requests (already-framed JSON-RPC messages) into a Server via a temp file
// standing in for stdin, capturing every message it writes to a second temp file
// standing in for stdout. Server::Run exits once it has processed every request
// (mirroring how a real client would end the session with "exit").
inline std::vector<Amanuensis::JsonValue> RunServer(const std::vector<Amanuensis::JsonValue>& Requests) {
    std::FILE* In = std::tmpfile();
    for (const auto& Req : Requests) {
        JsonRpc::WriteMessage(In, Req);
    }
    Amanuensis::JsonValue Exit = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(Exit, "jsonrpc", Amanuensis::JsonValue("2.0"));
    Amanuensis::Json::Insert(Exit, "method", Amanuensis::JsonValue("exit"));
    JsonRpc::WriteMessage(In, Exit);
    std::rewind(In);

    std::FILE* Out = std::tmpfile();
    Server ThisServer;
    ThisServer.Run(In, Out);
    std::fclose(In);

    std::rewind(Out);
    std::vector<Amanuensis::JsonValue> Responses;
    while (const auto Msg = JsonRpc::ReadMessage(Out)) {
        Responses.push_back(*Msg);
    }
    std::fclose(Out);
    return Responses;
}

inline Amanuensis::JsonValue MakeDidOpen(const std::string& Uri, const std::string& Text) {
    Amanuensis::JsonValue TextDocument = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(TextDocument, "uri", Amanuensis::JsonValue(Uri));
    Amanuensis::Json::Insert(TextDocument, "text", Amanuensis::JsonValue(Text));
    Amanuensis::JsonValue Params = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(Params, "textDocument", std::move(TextDocument));
    Amanuensis::JsonValue Message = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(Message, "jsonrpc", Amanuensis::JsonValue("2.0"));
    Amanuensis::Json::Insert(Message, "method", Amanuensis::JsonValue("textDocument/didOpen"));
    Amanuensis::Json::Insert(Message, "params", std::move(Params));
    return Message;
}

inline Amanuensis::JsonValue MakeRequest(int Id, const std::string& Method, const std::string& Uri, std::uint32_t Line0,
                                std::uint32_t Character0) {
    Amanuensis::JsonValue TextDocument = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(TextDocument, "uri", Amanuensis::JsonValue(Uri));
    Amanuensis::JsonValue Position = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(Position, "line", Amanuensis::JsonValue(static_cast<long long>(Line0)));
    Amanuensis::Json::Insert(Position, "character", Amanuensis::JsonValue(static_cast<long long>(Character0)));
    Amanuensis::JsonValue Params = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(Params, "textDocument", std::move(TextDocument));
    Amanuensis::Json::Insert(Params, "position", std::move(Position));
    Amanuensis::JsonValue Message = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(Message, "jsonrpc", Amanuensis::JsonValue("2.0"));
    Amanuensis::Json::Insert(Message, "id", Amanuensis::JsonValue(static_cast<long long>(Id)));
    Amanuensis::Json::Insert(Message, "method", Amanuensis::JsonValue(Method));
    Amanuensis::Json::Insert(Message, "params", std::move(Params));
    return Message;
}

inline Amanuensis::JsonValue MakeNotification(const std::string& Method, Amanuensis::JsonValue Params) {
    Amanuensis::JsonValue Message = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(Message, "jsonrpc", Amanuensis::JsonValue("2.0"));
    Amanuensis::Json::Insert(Message, "method", Amanuensis::JsonValue(Method));
    Amanuensis::Json::Insert(Message, "params", std::move(Params));
    return Message;
}

inline Amanuensis::JsonValue MakeDidChange(const std::string& Uri, const std::string& Text) {
    Amanuensis::JsonValue TextDocument = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(TextDocument, "uri", Amanuensis::JsonValue(Uri));
    Amanuensis::JsonValue Change = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(Change, "text", Amanuensis::JsonValue(Text));
    Amanuensis::JsonValue Changes = Amanuensis::Json::MakeArray();
    Amanuensis::Json::PushBack(Changes, std::move(Change));
    Amanuensis::JsonValue Params = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(Params, "textDocument", std::move(TextDocument));
    Amanuensis::Json::Insert(Params, "contentChanges", std::move(Changes));
    return MakeNotification("textDocument/didChange", std::move(Params));
}

inline Amanuensis::JsonValue MakeWatchedFileChange(const std::string& Uri) {
    Amanuensis::JsonValue Change = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(Change, "uri", Amanuensis::JsonValue(Uri));
    Amanuensis::Json::Insert(Change, "type", Amanuensis::JsonValue(static_cast<long long>(2)));
    Amanuensis::JsonValue Changes = Amanuensis::Json::MakeArray();
    Amanuensis::Json::PushBack(Changes, std::move(Change));
    Amanuensis::JsonValue Params = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(Params, "changes", std::move(Changes));
    return MakeNotification("workspace/didChangeWatchedFiles", std::move(Params));
}

// Every publishDiagnostics the server sent for Uri, in order; each entry is that
// notification's diagnostics array.
inline std::vector<Amanuensis::JsonValue> PublishedFor(const std::vector<Amanuensis::JsonValue>& Responses,
                                                const std::string& Uri) {
    std::vector<Amanuensis::JsonValue> Published;
    for (const auto& Msg : Responses) {
        if (!Amanuensis::Json::Contains(Msg, "method") ||
            Amanuensis::Json::AsString(Amanuensis::Json::Get(Msg, "method")) != "textDocument/publishDiagnostics") {
            continue;
        }
        const auto& Params = Amanuensis::Json::Get(Msg, "params");
        if (Amanuensis::Json::AsString(Amanuensis::Json::Get(Params, "uri")) == Uri) {
            Published.push_back(Amanuensis::Json::Get(Params, "diagnostics"));
        }
    }
    return Published;
}

inline long long IntegerAt(const Amanuensis::JsonValue& Value, std::initializer_list<const char*> Keys) {
    const Amanuensis::JsonValue* Current = &Value;
    for (const char* Key : Keys) {
        Current = &Amanuensis::Json::Get(*Current, Key);
    }
    return Amanuensis::Json::AsInteger(*Current);
}

inline std::string MessageOf(const Amanuensis::JsonValue& Diagnostic) {
    return Amanuensis::Json::AsString(Amanuensis::Json::Get(Diagnostic, "message"));
}

class ScratchFolder {
public:
    ScratchFolder() {
        static int Counter = 0;
        Path_ = std::filesystem::temp_directory_path() /
                ("cosmos-lsp-tests-" + std::to_string(::getpid()) + "-" + std::to_string(++Counter));
        std::filesystem::create_directories(Path_);
    }
    ~ScratchFolder() { std::filesystem::remove_all(Path_); }

    void Write(const std::string& Name, const std::string& Text) const { std::ofstream(Path_ / Name) << Text; }
    std::string Uri(const std::string& Name) const { return "file://" + (Path_ / Name).string(); }

private:
    std::filesystem::path Path_;
};


} // namespace CosmosLspTests
