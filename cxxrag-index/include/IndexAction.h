#pragma once

#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Frontend/FrontendAction.h>

namespace cxxrag {
  struct CodeChunk {
    enum class Kind {
      RECORD,
      ENUM,
      FUNCTION,
    };

    Kind kind;
    std::string name;
    std::string filepath;
    uint32_t start_line;
    uint32_t end_line;
    std::string signature;
    std::string comment;
    std::string body;

    [[nodiscard]]
    std::string str() const;
  };

  class IndexAction final : public clang::ASTFrontendAction {
    class ASTVisitor final : public clang::RecursiveASTVisitor<ASTVisitor> {
      clang::ASTContext &ctx;
      std::vector<CodeChunk> &chunks;

      [[nodiscard]]
      std::string getSourceText(clang::SourceRange range) const;

    public:
      explicit ASTVisitor(clang::ASTContext &ctx, std::vector<CodeChunk> &chunks);

      bool VisitFunctionDecl(clang::FunctionDecl *decl);
      bool VisitCXXRecordDecl(clang::CXXRecordDecl *decl);
      bool VisitEnumDecl(clang::EnumDecl *decl);
    };

    class ASTConsumer final : public clang::ASTConsumer {
      ASTVisitor visitor;
      std::string in_file;

    public:
      explicit ASTConsumer(std::vector<CodeChunk> &chunks, clang::ASTContext &ctx, llvm::StringRef in_file);

      void HandleTranslationUnit(clang::ASTContext &ctx) override;
    };

    std::vector<CodeChunk> chunks;

  public:
    ~IndexAction() override;

    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
      clang::CompilerInstance &cc, llvm::StringRef file) override;
  };

  const std::vector<CodeChunk> &GetChunks();
} // cxxrag
