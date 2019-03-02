#pragma once

#include <filesystem>
#include <optional>

class Header {
public:
  Header(const std::filesystem::path &path) : path_(path) {}

private:
  std::filesystem::path path_;
  std::optional<std::filesystem::path> parentHaderPath_;
};
