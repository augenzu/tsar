//===-- OpenMPAutoPar.cpp - OpenMP Based Parallelization (Clang) -*- C++ -*===//
//
//                       Traits Static Analyzer (SAPFOR)
//
// Copyright 2019 DVM System Group
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//===----------------------------------------------------------------------===//
//
// This file implements a pass to perform OpenMP-based auto parallelization.
//
//===----------------------------------------------------------------------===//

#include "SharedMemoryAutoPar.h"
#include "tsar/Analysis/Clang/ASTDependenceAnalysis.h"
#include "tsar/Analysis/DFRegionInfo.h"
#include "tsar/Analysis/Passes.h"
#include "tsar/Analysis/Parallel/Passes.h"
#include "tsar/Core/Query.h"
#include "tsar/Core/TransformationContext.h"
#include "tsar/Transform/Clang/Passes.h"

using namespace llvm;
using namespace tsar;

#undef DEBUG_TYPE
#define DEBUG_TYPE "clang-openmp-parallel"

namespace {
/// This pass try to insert OpenMP directives into a source code to obtain
/// a parallel program.
class ClangOpenMPParallelization : public ClangSMParallelization {
public:
  static char ID;
  ClangOpenMPParallelization() : ClangSMParallelization(ID), mStub(0, true) {
    initializeClangOpenMPParallelizationPass(*PassRegistry::getPassRegistry());
  }
private:
  ParallelItem * exploitParallelism(const DFLoop &IR, const clang::ForStmt &AST,
    const FunctionAnalysis &Provider,
    tsar::ClangDependenceAnalyzer &ASTDepInfo, ParallelItem *PI) override;

  ParallelItem mStub;
};

struct ClausePrinter {
  /// Add clause for a `Trait` with variable names from a specified list to
  /// the end of `ParallelFor` pragma.
  template <class Trait> void operator()(
      const ClangDependenceAnalyzer::SortedVarListT &VarInfoList) {
    if (VarInfoList.empty())
      return;
    std::string Clause(Trait::tag::toString());
    Clause.erase(
        std::remove_if(Clause.begin(), Clause.end(), bcl::isWhitespace),
        Clause.end());
    ParallelFor += Clause;
    ParallelFor += '(';
    auto I = VarInfoList.begin(), EI = VarInfoList.end();
    ParallelFor += *I;
    for (++I; I != EI; ++I)
      ParallelFor += ", " + *I;
    ParallelFor += ')';
  }

  /// Add clauses for all reduction variables from a specified list to
  /// the end of `ParallelFor` pragma.
  template <class Trait> void operator()(
      const ClangDependenceAnalyzer::ReductionVarListT &VarInfoList) {
    unsigned I = trait::Reduction::RK_First;
    unsigned EI = trait::Reduction::RK_NumberOf;
    for (; I < EI; ++I) {
      if (VarInfoList[I].empty())
        continue;
      ParallelFor += "reduction";
      ParallelFor += '(';
      switch (static_cast<trait::Reduction::Kind>(I)) {
      case trait::Reduction::RK_Add: ParallelFor += "+:"; break;
      case trait::Reduction::RK_Mult: ParallelFor += "*:"; break;
      case trait::Reduction::RK_Or: ParallelFor += "|:"; break;
      case trait::Reduction::RK_And: ParallelFor += "&:"; break;
      case trait::Reduction::RK_Xor: ParallelFor + "^: "; break;
      case trait::Reduction::RK_Max: ParallelFor += "max:"; break;
      case trait::Reduction::RK_Min: ParallelFor += "min:"; break;
      default: llvm_unreachable("Unknown reduction kind!"); break;
      }
      auto VarItr = VarInfoList[I].begin(), VarItrE = VarInfoList[I].end();
      ParallelFor += *VarItr;
      for (++VarItr; VarItr != VarItrE; ++VarItr)
        ParallelFor += ", " + *VarItr;
      ParallelFor += ')';
    }
  }

  SmallString<128> &ParallelFor;
};
} // namespace

ParallelItem * ClangOpenMPParallelization::exploitParallelism(
    const DFLoop &IR, const clang::ForStmt &AST,
    const FunctionAnalysis &Provider,
    tsar::ClangDependenceAnalyzer &ASTDepInfo, ParallelItem *PI) {
  auto *M = IR.getLoop()->getHeader()->getModule();
  auto &TfmCtx = *getAnalysis<TransformationEnginePass>().getContext(*M);
  SmallString<128> ParallelFor("#pragma omp parallel for default(shared)");
  bcl::for_each(ASTDepInfo.getDependenceInfo(), ClausePrinter{ParallelFor});
  ParallelFor += '\n';
  auto &Rewriter = TfmCtx.getRewriter();
  Rewriter.InsertTextBefore(AST.getBeginLoc(), ParallelFor);
  return &mStub;
}

ModulePass *llvm::createClangOpenMPParallelization() {
  return new ClangOpenMPParallelization;
}

char ClangOpenMPParallelization::ID = 0;
INITIALIZE_SHARED_PARALLELIZATION(ClangOpenMPParallelization,
                                  "clang-openmp-parallel",
                                  "OpenMP Based Parallelization (Clang)")
