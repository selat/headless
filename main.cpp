#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Tooling/Tooling.h>

#include <cassert>
#include <filesystem>
#include <iostream>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "header.hpp"

using namespace clang;
using namespace clang::tooling;
using namespace llvm;

// hashing function for std::filesystem::path so we can put it in unordered_map
namespace std {
template <> struct hash<filesystem::path> {
  size_t operator()(const filesystem::path &path) const {
    return hash_value(path);
  }
};
} // namespace std

std::unordered_map<std::filesystem::path, std::vector<std::filesystem::path>>
    fileIncludes;
std::unordered_map<std::filesystem::path, std::shared_ptr<Header>> headersMap;
std::unordered_set<std::filesystem::path> mainFileIncludes;
std::map<std::filesystem::path, std::set<std::string>> headerUsages;
std::unordered_map<const clang::Type *, const TypeDecl *> hackyTypeMap;

class IncludesCollectorCallback : public PPCallbacks {
public:
  IncludesCollectorCallback(const SourceManager &sourceManager,
                            const std::filesystem::path &mainFilePath)
      : sourceManager_(sourceManager), mainFilePath_(mainFilePath) {}

  void InclusionDirective(SourceLocation HashLoc, const Token &IncludeTok,
                          StringRef FileName, bool IsAngled,
                          CharSourceRange FilenameRange, const FileEntry *File,
                          StringRef SearchPath, StringRef RelativePath,
                          const clang::Module *Imported,
                          SrcMgr::CharacteristicKind FileType) override {
    auto filePath = std::filesystem::path(sourceManager_.getFilename(HashLoc));
    if (!headersMap.count(filePath)) {
      headersMap[filePath] = std::make_shared<Header>(filePath);
    }

    auto includePath = std::filesystem::path(File->getName().str());
    if (!headersMap.count(includePath)) {
      headersMap[includePath] =
          std::make_shared<Header>(includePath, RelativePath.str());
    }

    headersMap[includePath]->addParentIfNeeded(headersMap[filePath]);
    fileIncludes[filePath].push_back(includePath);

    if (filePath == mainFilePath_) {
      mainFileIncludes.insert(includePath);
    }
  }

private:
  const SourceManager &sourceManager_;
  const std::filesystem::path mainFilePath_;
};

class ExampleVisitor : public RecursiveASTVisitor<ExampleVisitor> {
public:
  explicit ExampleVisitor(CompilerInstance *compilerInstance)
      : astContext_(compilerInstance->getASTContext()),
        sourceManager_(compilerInstance->getASTContext().getSourceManager()) {
    FileID mainFileID = sourceManager_.getMainFileID();
    auto mainFileLocation = sourceManager_.getLocForStartOfFile(mainFileID);
    mainFilePath_ = std::filesystem::path(
        sourceManager_.getFilename(mainFileLocation).str());

    compilerInstance->getPreprocessor().addPPCallbacks(
        std::make_unique<IncludesCollectorCallback>(sourceManager_,
                                                    mainFilePath_));
  }

  virtual bool VisitStmt(Stmt *statement) {
    if (CXXConstructExpr *constructorExpr =
            dyn_cast<CXXConstructExpr>(statement)) {
      if (CXXConstructorDecl *constructorDecl =
              constructorExpr->getConstructor()) {
        processStatement(constructorExpr, constructorDecl->getParent());
      }
    } else if (CXXMemberCallExpr *memberCallExpr =
                   dyn_cast<CXXMemberCallExpr>(statement)) {
      if (CXXMethodDecl *methodDecl = memberCallExpr->getMethodDecl()) {
        processStatement(memberCallExpr, methodDecl->getParent());
      }
    } else if (CallExpr *callExpr = dyn_cast<CallExpr>(statement)) {
      if (FunctionDecl *funcDecl = callExpr->getDirectCallee()) {
        processStatement(callExpr, funcDecl);
      }
    } else if (DeclStmt *declStmt = dyn_cast<DeclStmt>(statement)) {
      // TODO: support non-single declarations
      if (!declStmt->isSingleDecl()) {
        return true;
      }
      if (VarDecl *varDecl = dyn_cast<VarDecl>(declStmt->getSingleDecl())) {
        QualType type = varDecl->getType();
        const clang::Type *typePtr = type.getTypePtr();
        TagDecl *tagDecl = typePtr->getAsTagDecl();
        if (tagDecl) {
          processStatement(declStmt, tagDecl);
        } else if (hackyTypeMap.count(typePtr)) {
          processStatement(declStmt, hackyTypeMap[typePtr]);
        }
      }
    }
    return true;
  }

  virtual bool VisitDecl(Decl *decl) {
    // This is horrible, but I don't know a workaround. Basically I want to be
    // able to convert clang::Type -> clang::TypeDecl to get source location
    // of the former. Unrortunately after 3 hours of research I wasn't able to
    // find a proper way to do it. Therefore I use approach with populating
    // Type* -> TypeDecl* map using declarations visitor.
    // Basically it is inverse implementation of ASTContext::getTypeDeclType()
    if (TypeDecl *typeDecl = dyn_cast<TypeDecl>(decl)) {
      QualType type = astContext_.getTypeDeclType(typeDecl);
      hackyTypeMap[type.getTypePtr()] = typeDecl;
    }
    return true;
  }

  void processStatement(Stmt *stmt, const NamedDecl *decl) {
    std::filesystem::path sourceFilePath(
        sourceManager_.getFilename(decl->getLocStart()).str());

    processStatement(stmt, sourceFilePath, decl->getNameAsString());
  }

  void processStatement(Stmt *stmt, const std::filesystem::path &sourceFilePath,
                        std::string declName) {
    std::filesystem::path filePath(
        sourceManager_.getFilename(stmt->getLocStart()).str());

    // Probably some internal type, like __va_list_tag
    if (sourceFilePath.empty() || sourceFilePath == mainFilePath_) {
      return;
    }

    assert(headersMap.count(sourceFilePath));
    if (!headersMap[sourceFilePath]->isInternal()) {
      if (filePath == mainFilePath_) {
        headerUsages[sourceFilePath].insert(declName);
      }
    } else {
      if (filePath == mainFilePath_) {
        headerUsages[headersMap[sourceFilePath]->getRealPath()].insert(
            declName);
      }
    }
  }

private:
  const ASTContext &astContext_;
  const SourceManager &sourceManager_;
  std::filesystem::path mainFilePath_;
};

class ExampleASTConsumer : public ASTConsumer {
private:
  ExampleVisitor *visitor;

public:
  explicit ExampleASTConsumer(CompilerInstance *compilerInstance)
      : visitor(new ExampleVisitor(compilerInstance)) {}

  virtual void HandleTranslationUnit(ASTContext &context) {
    visitor->TraverseDecl(context.getTranslationUnitDecl());
  }
};

class ExampleFrontendAction : public ASTFrontendAction {
public:
  virtual std::unique_ptr<ASTConsumer>
  CreateASTConsumer(CompilerInstance &compilerInstance,
                    StringRef file) override {
    return std::make_unique<ExampleASTConsumer>(&compilerInstance);
  }
};

std::string getRootDirectory(const std::filesystem::path &path) {
  int curId = 0;
  std::string rootDirectory;
  for (const auto &item : path) {
    ++curId;
    if (curId == 2) {
      rootDirectory = item.string();
      break;
    }
  }

  return rootDirectory;
}

void printIncludesInfo() {
  std::cout << "\n Includes info: " << std::endl;
  for (const auto &fileInclude : fileIncludes) {
    auto rootDirectory = getRootDirectory(fileInclude.first);
    if (rootDirectory != "home")
      continue;

    std::cout << fileInclude.first << std::endl;
    for (const auto &includePath : fileInclude.second) {
      std::cout << "    " << includePath << std::endl;
    }
    std::cout << std::endl;
  }
}

void printIncludesMapInfo() {
  for (const auto &p : headersMap) {
    const auto &path = p.first;
    std::cout << p.second->isInternal() << " " << path << "    ->    "
              << p.second->getRealPath() << std::endl;
  }
}

void printMissingIncludesInfo() {
  std::cout << "Please add the following headers:" << std::endl;
  for (const auto &header : headerUsages) {
    // Print warnings only about missing headers
    if (mainFileIncludes.count(header.first) ||
        headersMap[header.first]->isInternal()) {
      continue;
    }

    auto relativePath = headersMap[header.first]->relativePath();
    std::cout << relativePath.c_str() << "\n    ";
    auto iterator = header.second.begin();
    if (iterator != header.second.end()) {
      std::cout << *iterator;
      ++iterator;
    }

    while (iterator != header.second.end()) {
      std::cout << ", " << *iterator;
      ++iterator;
    }
    std::cout << std::endl << std::endl;
  }
}

void printRedundantHeadersInfo() {
  std::cout << "Please remove the following headers:" << std::endl;
  for (const auto &headerPath : mainFileIncludes) {
    if (!headerUsages.count(headerPath)) {
      auto relativePath = headersMap[headerPath]->relativePath();
      std::cout << relativePath.c_str() << std::endl;
    }
  }
}

static cl::OptionCategory MyToolCategory("My tool options");
static cl::extrahelp CommonHelp(CommonOptionsParser::HelpMessage);
static cl::extrahelp MoreHelp("\nMore help text...\n");

int main(int argc, const char **argv) {
  CommonOptionsParser OptionsParser(argc, argv, MyToolCategory);
  ClangTool Tool(OptionsParser.getCompilations(),
                 OptionsParser.getSourcePathList());
  auto frontendActionFactory =
      newFrontendActionFactory<ExampleFrontendAction>();
  auto result = Tool.run(frontendActionFactory.get());

  // printIncludesInfo();
  // printIncludesMapInfo();
  printMissingIncludesInfo();
  printRedundantHeadersInfo();

  return result;
}
