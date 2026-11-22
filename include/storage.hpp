#pragma once
#include "graph.hpp"
#include <filesystem>
#include <functional>
#include <iosfwd>

Result<Graph> readGraph(std::istream& input);
Result<void> writeGraph(const Graph& graph, std::ostream& output);
Result<Graph> loadGraph(const std::filesystem::path& path);
Result<void> saveGraph(const Graph& graph, const std::filesystem::path& path);
// Файлы графов и CSV с измерениями записываются одним способом через временный файл
Result<void> writeFile(const std::filesystem::path& path,
                       const std::function<Result<void>(std::ostream&)>& writer);
