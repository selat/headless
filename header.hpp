#pragma once

#include <filesystem>
#include <memory>
#include <optional>

class Header {
public:
  Header(const std::filesystem::path &path) : path_(path) {}
  void addParentIfNeeded(std::shared_ptr<Header> parent);
  // Right now it's incorrect because assumes that internal header can have just
  // one parent. It's not always the case.
  std::filesystem::path getRealPath();
  bool isInternal() { return parent_.has_value(); }

private:
  std::filesystem::path path_;
  std::optional<std::weak_ptr<Header>> parent_;
};
