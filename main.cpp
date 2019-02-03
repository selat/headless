#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/ASTMatchers/ASTMatchers.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Tooling/Tooling.h>

#include <iostream>

using namespace clang;
using namespace clang::tooling;
using namespace llvm;

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
    if (filename == mainFilename_) {
      std::cout << "Include: " << File->getName().str() << std::endl;
    }
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
          std::cout << "Constructor: "
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

static cl::OptionCategory MyToolCategory("My tool options");
static cl::extrahelp CommonHelp(CommonOptionsParser::HelpMessage);
static cl::extrahelp MoreHelp("\nMore help text...\n");

int main(int argc, const char **argv) {
  CommonOptionsParser OptionsParser(argc, argv, MyToolCategory);
  ClangTool Tool(OptionsParser.getCompilations(),
                 OptionsParser.getSourcePathList());
  auto frontendActionFactory =
      newFrontendActionFactory<ExampleFrontendAction>();
  return Tool.run(frontendActionFactory.get());
}
