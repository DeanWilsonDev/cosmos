#pragma once

#include <amanuensis.hpp>

#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>

namespace CosmosLsp {

std::string UriToPath(const std::string& Uri);
std::string PathToUri(const std::string& Path);

std::optional<std::string> ReadFile(const std::filesystem::path& Path);

// Positions and ranges take Cosmos's 1-based lines and columns and write LSP's 0-based ones.
Amanuensis::JsonValue MakePosition(std::uint32_t Line, std::uint32_t Column);
Amanuensis::JsonValue MakeRange(std::uint32_t Line, std::uint32_t Column, std::uint32_t EndLine,
                                std::uint32_t EndColumn);
Amanuensis::JsonValue MakeRange(std::uint32_t Line, std::uint32_t Column);

const Amanuensis::JsonValue* FindPath(const Amanuensis::JsonValue& Root, std::initializer_list<std::string_view> Keys);

bool IsGlobalPath(const std::string& Path);
bool IsCosmosPath(const std::string& Path);

Amanuensis::JsonValue MakeDiagnostic(Amanuensis::JsonValue Range, long long Severity, const std::string& Message);

} // namespace CosmosLsp
