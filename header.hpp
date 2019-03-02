#pragma once

#include <filesystem>
#include <memory>
#include <optional>

class Header {
public:
  Header(const std::filesystem::path &path) : path_(path) {}
  Header(const std::filesystem::path &path,
         const std::filesystem::path &relativePath)
      : path_(path), relativePath_(relativePath) {}
  void addParentIfNeeded(std::shared_ptr<Header> parent);
  // Right now it's incorrect because assumes that internal header can have just
  // one parent. It's not always the case.
  std::filesystem::path getRealPath();
  bool isInternal() { return parent_.has_value(); }
  const std::filesystem::path &relativePath() { return relativePath_; }

private:
  std::filesystem::path path_;
  std::filesystem::path relativePath_;
  std::optional<std::weak_ptr<Header>> parent_;
};
