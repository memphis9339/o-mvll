#pragma once
#include "llvm/IR/PassManager.h"

namespace omvll {

struct JunkCode : llvm::PassInfoMixin<JunkCode> {
  llvm::PreservedAnalyses run(llvm::Module &M,
                              llvm::ModuleAnalysisManager &MAM);
  bool process(llvm::Function &F);
};

} // end namespace omvll