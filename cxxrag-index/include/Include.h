#pragma once

#include <filesystem>
#include <thread>

#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Driver/Driver.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/Process.h>
