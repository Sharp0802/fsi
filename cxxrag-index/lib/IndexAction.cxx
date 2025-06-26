#include "Include.h"
#include "IndexAction.h"

#include <regex>

extern std::string RootDir;

namespace cxxrag {
  bool IsChildOf(std::filesystem::path parent, std::filesystem::path child) {
    std::error_code ec;

    parent = std::filesystem::canonical(parent, ec);
    if (ec) {
      return false;
    }

    child = std::filesystem::canonical(child, ec);
    if (ec) {
      return false;
    }

    for (; parent != child && child.has_parent_path() && child != child.root_directory(); child = child.parent_path()) {
    }
    return parent == child;
  }

  bool Replace(std::string &str, const std::string &from, const std::string &to) {
    size_t start_pos = str.find(from);
    if (start_pos == std::string::npos)
      return false;
    str.replace(start_pos, from.length(), to);
    return true;
  }

  void Encode(std::string &str) {
    {
      const std::regex regex(R"(\\)");
      str = std::regex_replace(str, regex, "\\\\");
    }

    {
      const std::regex regex(R"(")");
      str = std::regex_replace(str, regex, "\\\"");
    }

    while (Replace(str, "\n", "\\n")) {
    }
  }

  std::string CodeChunk::str() const {
    std::stringstream ss;
    ss << "\t{\n";
    ss << "\t\t\"kind\": " << static_cast<int>(kind) << ",\n";
    ss << "\t\t\"name\": \"" << name << "\",\n";
    ss << "\t\t\"filepath\": \"" << filepath << "\",\n";
    ss << "\t\t\"start_line\": " << start_line << ",\n";
    ss << "\t\t\"end_line\": " << end_line << ",\n";
    ss << "\t\t\"signature\": \"" << signature << "\",\n";
    ss << "\t\t\"comment\": \"" << comment << "\",\n";
    ss << "\t\t\"body\": \"" << body << "\"\n";
    ss << "\t}";
    return ss.str();
  }

  std::string IndexAction::ASTVisitor::getSourceText(clang::SourceRange range) const {
    const auto &sm = ctx.getSourceManager();
    const auto &opts = ctx.getLangOpts();
    return clang::Lexer::getSourceText(clang::CharSourceRange::getTokenRange(range), sm, opts).str();
  }

  IndexAction::ASTVisitor::ASTVisitor(
    clang::ASTContext &ctx, std::vector<CodeChunk> &chunks): ctx(ctx), chunks(chunks) {
  }

  // ReSharper disable once CppMemberFunctionMayBeConst
  // ReSharper disable once CppDFAConstantFunctionResult
  bool IndexAction::ASTVisitor::VisitFunctionDecl(clang::FunctionDecl *decl) {
    if (!decl->getBody()) {
      return true;
    }

    const auto &sm = ctx.getSourceManager();
    const std::filesystem::path path = sm.getFilename(decl->getLocation()).str();
    if (!IsChildOf(RootDir, path)) {
      return true;
    }

    CodeChunk chunk{
      .kind = CodeChunk::Kind::FUNCTION,
      .name = decl->getQualifiedNameAsString(),
      .filepath = path,
      .start_line = sm.getSpellingLineNumber(decl->getBeginLoc()),
      .end_line = sm.getSpellingLineNumber(decl->getEndLoc()),
      .signature = getSourceText({decl->getBeginLoc(), decl->getBody()->getBeginLoc()}),
      .body = getSourceText(decl->getBody()->getSourceRange())
    };
    if (const auto pos = chunk.signature.find_last_not_of(" \t\n\r{"); pos != std::string::npos) {
      chunk.signature.erase(pos + 1);
    }
    Encode(chunk.signature);

    Encode(chunk.body);

    if (const clang::RawComment *rc = ctx.getRawCommentForDeclNoCache(decl)) {
      chunk.comment = rc->getRawText(sm).str();
      Encode(chunk.comment);
    }

    chunks.push_back(chunk);
    return true;
  }

  // ReSharper disable once CppMemberFunctionMayBeConst
  // ReSharper disable once CppDFAConstantFunctionResult
  bool IndexAction::ASTVisitor::VisitCXXRecordDecl(clang::CXXRecordDecl *decl) {
    if (!decl->isThisDeclarationADefinition() || decl->getName().empty()) {
      return true;
    }

    const auto &sm = ctx.getSourceManager();
    const std::filesystem::path path = sm.getFilename(decl->getLocation()).str();
    if (!IsChildOf(RootDir, path)) {
      return true;
    }

    CodeChunk chunk{
      .kind = CodeChunk::Kind::RECORD,
      .name = decl->getQualifiedNameAsString(),
      .filepath = path,
      .start_line = sm.getSpellingLineNumber(decl->getBeginLoc()),
      .end_line = sm.getSpellingLineNumber(decl->getEndLoc()),
      .signature = getSourceText(decl->getSourceRange()),
      .body = getSourceText(decl->getSourceRange())
    };

    Encode(chunk.signature);
    Encode(chunk.body);

    if (const clang::RawComment *rc = ctx.getRawCommentForDeclNoCache(decl)) {
      chunk.comment = rc->getRawText(sm).str();
      Encode(chunk.comment);
    }

    chunks.push_back(chunk);
    return true;
  }

  // ReSharper disable once CppMemberFunctionMayBeConst
  // ReSharper disable once CppDFAConstantFunctionResult
  bool IndexAction::ASTVisitor::VisitEnumDecl(clang::EnumDecl *decl) {
    if (!decl->isThisDeclarationADefinition() || decl->getName().empty()) {
      return true;
    }

    const auto &sm = ctx.getSourceManager();
    const std::filesystem::path path = sm.getFilename(decl->getLocation()).str();
    if (!IsChildOf(RootDir, path)) {
      return true;
    }

    CodeChunk chunk{
      .kind = CodeChunk::Kind::ENUM,
      .name = decl->getQualifiedNameAsString(),
      .filepath = path,
      .start_line = sm.getSpellingLineNumber(decl->getBeginLoc()),
      .end_line = sm.getSpellingLineNumber(decl->getEndLoc()),
      .signature = getSourceText(decl->getSourceRange()),
      .body = getSourceText(decl->getSourceRange())
    };

    Encode(chunk.signature);
    Encode(chunk.body);

    if (const clang::RawComment *rc = ctx.getRawCommentForDeclNoCache(decl)) {
      chunk.comment = rc->getRawText(sm).str();
      Encode(chunk.comment);
    }

    chunks.push_back(chunk);
    return true;
  }

  IndexAction::ASTConsumer::ASTConsumer(
    std::vector<CodeChunk> &chunks, clang::ASTContext &ctx, llvm::StringRef in_file):
    visitor(ctx, chunks),
    in_file(in_file) {
  }

  std::mutex Mutex;
  std::vector<CodeChunk> Chunks{};

  void IndexAction::ASTConsumer::HandleTranslationUnit(clang::ASTContext &ctx) {
    visitor.TraverseDecl(ctx.getTranslationUnitDecl());
  }

  IndexAction::~IndexAction() {
    std::scoped_lock lock(Mutex);
    std::ranges::copy(chunks, std::back_inserter(Chunks));
  }

  std::unique_ptr<clang::ASTConsumer> IndexAction::CreateASTConsumer(
    clang::CompilerInstance &cc, llvm::StringRef file) {
    return std::make_unique<ASTConsumer>(chunks, cc.getASTContext(), file);
  }

  const std::vector<CodeChunk> &GetChunks() {
    return Chunks;
  }
} // cxxrag
