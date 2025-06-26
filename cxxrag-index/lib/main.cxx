#include "Include.h"
#include "IndexAction.h"

namespace fs = std::filesystem;

using namespace clang::tooling;
using namespace llvm;

#if __unix__
#define PATH_DELIM ':'
#else
#define PATH_DELIM ';'
#endif

std::vector<std::vector<std::string>> Chunk(const std::vector<std::string> &src, const size_t size) {
  std::vector<std::vector<std::string>> result(size);

  auto offset = 0;
  for (auto i = 0; i < size; i++) {
    auto local = src.size() / size;
    if (src.size() % size > i)
      local++;

    result[i].reserve(local);
    for (auto j = 0; j < local; j++) {
      result[i].emplace_back(src[offset + j]);
    }

    offset += local;
  }

  return result;
}

std::vector<std::string> GetPath() {
  const char *path = std::getenv("PATH");
  if (!path) {
    return {};
  }

  size_t nTerm = 1;
  for (const char *s = path; *s; ++s)
    if (*s == PATH_DELIM)
      nTerm++;

  std::vector<std::string> result;
  result.reserve(nTerm);

  std::istringstream iss(path);
  for (std::string buffer; std::getline(iss, buffer, PATH_DELIM);) {
    result.emplace_back(buffer);
  }

  return result;
}

std::string GetDefaultIncludePath() {
  auto target = std::format("clang-{}", LLVM_VERSION_MAJOR);
  for (auto dir : GetPath()) {
    if (!fs::is_directory(dir)) {
      continue;
    }

    for (const auto &entry : std::filesystem::directory_iterator(dir)) {
      if (entry.path().filename() == target) {
        target = entry.path().string();
        goto CLANG_FOUND;
      }
    }
  }

  return "";
CLANG_FOUND:

  fs::path path = clang::driver::Driver::GetResourcesPath(target);
  path /= "include";

  return path;
}

std::string RootDir;

int main(int argc, const char *argv[]) {
  if (argc < 3) {
    errs() << "insufficient argument\n";
    return -1;
  }

  RootDir = argv[1];

  std::string error;
  const auto db = CompilationDatabase::loadFromDirectory(argv[2], error);
  if (!db) {
    errs() << error << '\n';
    return -1;
  }

  auto failed = false;

  const auto files = db->getAllFiles();
  const auto fileChunks = Chunk(files, std::thread::hardware_concurrency());

  const auto include = GetDefaultIncludePath();

  std::mutex mutex;
  size_t offset = 0;
#pragma omp parallel for shared(fileChunks, failed)
  for (auto chunk: fileChunks) {
    if (chunk.empty())
      continue;

    {
      std::scoped_lock lock(mutex);
      outs() << "[" << (offset + chunk.size()) << "/" << files.size() << "] chunk load" << '\n';
      offset += chunk.size();
    }

    ClangTool tool(*db, chunk);
    tool.appendArgumentsAdjuster(
      getInsertArgumentAdjuster(
        {
          "-isystem", include
        }, ArgumentInsertPosition::BEGIN));

    if (const auto ret = tool.run(newFrontendActionFactory<cxxrag::IndexAction>().get()); ret) {
      failed = true;
    }
  }

  auto &chunks = cxxrag::GetChunks();
  outs() << "[\n";
  for (auto i = 0; i < chunks.size(); i++) {
    if (i != 0) {
      outs() << ",\n";
    }

    outs() << chunks[i].str();
  }
  outs() << "\n]";

  return failed;
}
