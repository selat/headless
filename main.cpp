#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/ASTMatchers/ASTMatchers.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Tooling/Tooling.h>

#include <filesystem>
#include <iostream>
#include <unordered_map>
#include <vector>

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

class MyCallback : public PPCallbacks {
public:
  MyCallback(ASTContext *astContext, StringRef mainFilename)
      : astContext_(astContext), mainFilename_(mainFilename) {}

  void InclusionDirective(SourceLocation HashLoc, const Token &IncludeTok,
                          StringRef FileName, bool IsAngled,
                          CharSourceRange FilenameRange, const FileEntry *File,
                          StringRef SearchPath, StringRef RelativePath,
                          const clang::Module *Imported,
                          SrcMgr::CharacteristicKind FileType) override {
    auto &sourceManager = astContext_->getSourceManager();
    auto filename = sourceManager.getFilename(HashLoc);
    auto filePath = std::filesystem::path(filename);
    auto includePath = std::filesystem::path(File->getName().str());
    fileIncludes[filePath].push_back(includePath);
  }

private:
  ASTContext *astContext_;
  StringRef mainFilename_;
};

class ExampleVisitor : public RecursiveASTVisitor<ExampleVisitor> {
public:
  explicit ExampleVisitor(CompilerInstance *compilerInstance)
      : astContext_(&(compilerInstance->getASTContext())) {
    auto &sourceManager = astContext_->getSourceManager();
    FileID mainFileID = sourceManager.getMainFileID();
    auto mainFileLocation = sourceManager.getLocForStartOfFile(mainFileID);
    mainFilename_ = sourceManager.getFilename(mainFileLocation);

    compilerInstance->getPreprocessor().addPPCallbacks(
        std::make_unique<MyCallback>(astContext_, mainFilename_));
  }

  virtual bool VisitStmt(Stmt *statement) {
    if (CXXConstructExpr *constructorExpr =
            dyn_cast<CXXConstructExpr>(statement)) {
      if (CXXConstructorDecl *constructorDecl =
              constructorExpr->getConstructor()) {
        auto &sourceManager = astContext_->getSourceManager();
        StringRef filename =
            sourceManager.getFilename(constructorExpr->getLocStart());
        if (filename == mainFilename_) {
          std::cout << filename.str() << " Constructor: "
                    << constructorDecl->getNameInfo().getName().getAsString()
                    << std::endl;
        }
      }
    } else if (CXXMemberCallExpr *memberCallExpr =
                   dyn_cast<CXXMemberCallExpr>(statement)) {
      if (CXXMethodDecl *methodDecl = memberCallExpr->getMethodDecl()) {
        CXXRecordDecl *recordDecl = methodDecl->getParent();
        auto &sourceManager = astContext_->getSourceManager();
        StringRef filename =
            sourceManager.getFilename(memberCallExpr->getLocStart());
        if (filename == mainFilename_) {
          std::cout << "Member: "
                    << astContext_
                           ->getTypeDeclType(
                               const_cast<CXXRecordDecl *>(recordDecl))
                           .getAsString()
                    << std::endl;
        }
      }
    } else if (CallExpr *callExpr = dyn_cast<CallExpr>(statement)) {
      if (FunctionDecl *func = callExpr->getDirectCallee()) {
        auto &sourceManager = astContext_->getSourceManager();
        StringRef filename = sourceManager.getFilename(func->getLocStart());
        if (filename == mainFilename_) {
          std::cout << "Seeing a func: "
                    << func->getNameInfo().getName().getAsString() << " "
                    << filename.str() << std::endl;
        }
      }
    }
    return true;
  }

private:
  ASTContext *astContext_;
  StringRef mainFilename_;
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
    // if (rootDirectory != "home")
    //   continue;

    std::cout << fileInclude.first << std::endl;
    for (const auto &includePath : fileInclude.second) {
      std::cout << "    " << includePath << std::endl;
    }
    std::cout << std::endl;
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

  printIncludesInfo();

  return result;
}
