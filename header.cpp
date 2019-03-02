#include "header.hpp"

#include <cassert>

namespace {
bool isSubDirectory(const std::filesystem::path &path,
                    const std::filesystem::path &root) {
  auto currentPath = path;
  while (!currentPath.empty() && currentPath.has_relative_path()) {
    if (currentPath == root) {
      return true;
    }
    currentPath = currentPath.parent_path();
  }
  return false;
}
} // namespace

void Header::addParentIfNeeded(std::shared_ptr<Header> parent) {
  // TODO: implement proper checker for internal header files.
  if (parent_.has_value()) {
    return;
  }

  auto directoryPath = parent->path_.parent_path();
  auto childDirectoryPath = path_.parent_path().parent_path();
  if (isSubDirectory(childDirectoryPath, directoryPath)) {
    if (parent_.has_value()) {
      auto parentLock = parent_.value().lock();
      assert(parentLock);
      assert(parentLock->path_ == parent->path_);
    }
    parent_ = parent;
  }
}

std::filesystem::path Header::getRealPath() {
  auto parent = parent_;
  auto realPath = path_;
  while (parent.has_value()) {
    auto parentLock = parent.value().lock();
    assert(parentLock);
    realPath = parentLock->path_;
    parent = parentLock->parent_;
    parent_ = parent;
  }
  return realPath;
}
