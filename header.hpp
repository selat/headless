#pragma once

#include <filesystem>
#include <memory>
#include <optional>

class Header {
public:
  Header(const std::filesystem::path &path) : path_(path) {}
  void addParent(std::shared_ptr<Header> parent);
  std::filesystem::path getRealPath();

private:
  std::filesystem::path path_;
  std::optional<std::weak_ptr<Header>> parent_;
};
