#include "header.hpp"

#include <cassert>

namespace {
bool isSubDirectory(const std::filesystem::path &path,
                    const std::filesystem::path &root) {
  auto currentPath = path;
  while (!currentPath.empty()) {
    if (currentPath == root) {
      return true;
    }
    currentPath = currentPath.parent_path();
  }
  return false;
}
} // namespace

void Header::addParent(std::shared_ptr<Header> parent) {
  auto directoryPath = parent->path_.remove_filename();
  if (isSubDirectory(path_, directoryPath)) {
    assert(!parent_.has_value());
    parent_ = parent;
  }
}

std::filesystem::path Header::getRealPath() {
  auto parent = parent_;
  std::filesystem::path path;
  while (parent.has_value()) {
    auto parentLock = parent.value().lock();
    assert(parentLock);
    path = parentLock->path_;
    parent = parentLock->parent_;
    parent_ = parent;
  }
  return path;
}
