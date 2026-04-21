#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#include "omvll/ObfuscationConfig.hpp"
#include "omvll/PyConfig.hpp"
#include "omvll/log.hpp"
#include "omvll/passes/junk-code/JunkCode.hpp"
#include "omvll/utils.hpp"

using namespace llvm;

namespace omvll {

static Value *createOpaqueAlwaysFalse(IRBuilder<> &B, LLVMContext &Ctx) {
    auto *Int32Ty = Type::getInt32Ty(Ctx);

    auto *Slot = B.CreateAlloca(Int32Ty, nullptr, "omvll.opaque.slot");
    Slot->setAlignment(Align(4));

    auto *StoreVal = B.getInt32(0);
    auto *SI = B.CreateStore(StoreVal, Slot, /*isVolatile=*/true);

    auto *Loaded = B.CreateLoad(Int32Ty, Slot, /*isVolatile=*/true);
    Loaded->setName("omvll.opaque.val");

    auto *Sq = B.CreateMul(Loaded, Loaded, "omvll.sq");
    auto *Mod = B.CreateURem(Sq, B.getInt32(4), "omvll.mod");
    auto *Pred = B.CreateICmpEQ(Mod, B.getInt32(3), "omvll.pred");

    return Pred;
}

static Value *createOpaqueAlwaysTrue(IRBuilder<> &B, LLVMContext &Ctx) {
    auto *Int32Ty = Type::getInt32Ty(Ctx);

    auto *Slot = B.CreateAlloca(Int32Ty, nullptr, "omvll.opaque.slot.t");
    Slot->setAlignment(Align(4));

    auto *StoreVal = B.getInt32(RandomGenerator::generateRange(1, 0x7FFFFFFF));
    B.CreateStore(StoreVal, Slot, /*isVolatile=*/true);

    auto *Loaded = B.CreateLoad(Int32Ty, Slot, /*isVolatile=*/true);

    auto *OrOne = B.CreateOr(Loaded, B.getInt32(1), "omvll.or1");
    auto *Pred = B.CreateICmpNE(OrOne, B.getInt32(0), "omvll.pred.t");

    return Pred;
}

static void emitJunkArithChain(IRBuilder<> &B, LLVMContext &Ctx) {
    auto *Int64Ty = Type::getInt64Ty(Ctx);

    auto *Base = B.CreateAlloca(Int64Ty, nullptr, "omvll.junk.base");
    B.CreateStore(
        B.getInt64(RandomGenerator::generateRange(0x1000ULL, 0xFFFFFFFFULL)),
        Base, /*isVolatile=*/true
    );
    Value *V = B.CreateLoad(Int64Ty, Base, /*isVolatile=*/true);

    uint32_t ChainLen = RandomGenerator::generateRange(4, 8);
    for (uint32_t j = 0; j < ChainLen; ++j) {
        switch (RandomGenerator::generateRange(0, 6)) {
        case 0:
            V = B.CreateAdd(V, B.getInt64(RandomGenerator::generateRange(1, 0xFFFF)));
            break;
        case 1:
            V = B.CreateSub(V, B.getInt64(RandomGenerator::generateRange(1, 0xFFFF)));
            break;
        case 2:
            V = B.CreateXor(V, B.getInt64(RandomGenerator::generateRange(0, 0xFFFFFFFFULL)));
            break;
        case 3:
            V = B.CreateOr(V, B.getInt64(RandomGenerator::generateRange(0, 0xFF)));
            break;
        case 4:
            V = B.CreateAnd(V, B.getInt64(RandomGenerator::generateRange(0xFFFF, 0xFFFFFFFFULL)));
            break;
        case 5:
            V = B.CreateShl(V, B.getInt64(RandomGenerator::generateRange(1, 5)));
            break;
        case 6:
            V = B.CreateLShr(V, B.getInt64(RandomGenerator::generateRange(1, 5)));
            break;
        }
    }

    B.CreateStore(V, Base, /*isVolatile=*/true);
}

static void emitJunkFakeLoop(IRBuilder<> &B, Function &F, BasicBlock *ExitBB) {
    LLVMContext &Ctx = F.getContext();
    auto *Int32Ty = Type::getInt32Ty(Ctx);

    BasicBlock *LoopHeader = BasicBlock::Create(Ctx, "omvll.junk.loop.hdr", &F);
    BasicBlock *LoopBody   = BasicBlock::Create(Ctx, "omvll.junk.loop.body", &F);
    BasicBlock *LoopExit   = BasicBlock::Create(Ctx, "omvll.junk.loop.exit", &F);

    B.CreateBr(LoopHeader);

    IRBuilder<> HdrB(LoopHeader);
    auto *Counter = HdrB.CreateAlloca(Int32Ty, nullptr, "omvll.junk.ctr");
    HdrB.CreateStore(HdrB.getInt32(0), Counter, /*isVolatile=*/true);
    auto *CtrVal = HdrB.CreateLoad(Int32Ty, Counter, /*isVolatile=*/true);

    auto *BoundSlot = HdrB.CreateAlloca(Int32Ty, nullptr, "omvll.junk.bound");
    HdrB.CreateStore(HdrB.getInt32(0), BoundSlot, /*isVolatile=*/true);
    auto *Bound = HdrB.CreateLoad(Int32Ty, BoundSlot, /*isVolatile=*/true);

    auto *Cond = HdrB.CreateICmpSLT(CtrVal, Bound, "omvll.junk.loopcond");
    HdrB.CreateCondBr(Cond, LoopBody, LoopExit);

    IRBuilder<> BodyB(LoopBody);
    emitJunkArithChain(BodyB, Ctx);
    BodyB.CreateBr(LoopHeader);

    IRBuilder<> ExitB(LoopExit);
    ExitB.CreateBr(ExitBB);
}

static void emitJunkInlineAsm(IRBuilder<> &B, LLVMContext &Ctx) {
    auto *VoidTy = Type::getVoidTy(Ctx);
    auto *AsmFTy = FunctionType::get(VoidTy, false);

    #if defined(__aarch64__) || defined(_M_ARM64)
    const char *AsmStr = "nop\nnop\nnop";
    const char *Constraints = "";
    #elif defined(__x86_64__) || defined(_M_X64)
    const char *AsmStr = "nop\nnop\nnop";
    const char *Constraints = "";
    #elif defined(__arm__) || defined(_M_ARM)
    const char *AsmStr = "nop\nnop\nnop";
    const char *Constraints = "";
    #else
    emitJunkArithChain(B, Ctx);
    return;
    #endif

    auto *IA = InlineAsm::get(
        AsmFTy, AsmStr, Constraints,
        /*hasSideEffects=*/true,
        /*isAlignStack=*/false
    );
    B.CreateCall(IA);
}

static void emitJunkBranchCascade(IRBuilder<> &B, Function &F, BasicBlock *ExitBB) {
    LLVMContext &Ctx = F.getContext();
    auto *Int32Ty = Type::getInt32Ty(Ctx);

    uint32_t Depth = RandomGenerator::generateRange(2, 4);
    BasicBlock *CurrentExit = ExitBB;

    for (uint32_t d = 0; d < Depth; ++d) {
        BasicBlock *TrueBB  = BasicBlock::Create(Ctx, "omvll.junk.cascade.t", &F);
        BasicBlock *FalseBB = BasicBlock::Create(Ctx, "omvll.junk.cascade.f", &F);

        auto *Slot = B.CreateAlloca(Int32Ty);
        B.CreateStore(B.getInt32(RandomGenerator::generateRange(0, 100)), Slot, true);
        auto *Val = B.CreateLoad(Int32Ty, Slot, true);
        auto *Cmp = B.CreateICmpSGT(Val, B.getInt32(50), "omvll.junk.cascade.cmp");
        B.CreateCondBr(Cmp, TrueBB, FalseBB);

        {
            IRBuilder<> TB(TrueBB);
            emitJunkArithChain(TB, Ctx);
            TB.CreateBr(CurrentExit);
        }
        {
            IRBuilder<> FB(FalseBB);
            emitJunkArithChain(FB, Ctx);
            FB.CreateBr(CurrentExit);
        }

        BasicBlock *NewEntry = BasicBlock::Create(Ctx, "omvll.junk.cascade.entry", &F);
        CurrentExit = NewEntry;
        B.SetInsertPoint(NewEntry);
    }
}

static void emitJunkArrayOps(IRBuilder<> &B, LLVMContext &Ctx) {
    auto *Int32Ty = Type::getInt32Ty(Ctx);
    uint32_t ArraySize = RandomGenerator::generateRange(4, 8);

    auto *ArrTy = ArrayType::get(Int32Ty, ArraySize);
    auto *Arr = B.CreateAlloca(ArrTy, nullptr, "omvll.junk.arr");

    for (uint32_t i = 0; i < ArraySize; ++i) {
        auto *GEP = B.CreateConstInBoundsGEP2_32(ArrTy, Arr, 0, i);
        B.CreateStore(
            B.getInt32(RandomGenerator::generateRange(0, 0xFFFF)),
            GEP, /*isVolatile=*/true
        );
    }

    Value *Acc = B.getInt32(0);
    for (uint32_t i = 0; i < ArraySize; ++i) {
        auto *GEP = B.CreateConstInBoundsGEP2_32(ArrTy, Arr, 0, i);
        auto *Loaded = B.CreateLoad(Int32Ty, GEP, /*isVolatile=*/true);

        switch (RandomGenerator::generateRange(0, 2)) {
        case 0: Acc = B.CreateAdd(Acc, Loaded); break;
        case 1: Acc = B.CreateXor(Acc, Loaded); break;
        case 2: Acc = B.CreateOr(Acc, Loaded);  break;
        }
    }

    auto *Result = B.CreateAlloca(Int32Ty, nullptr, "omvll.junk.arr.result");
    B.CreateStore(Acc, Result, /*isVolatile=*/true);
}

bool JunkCode::process(Function &F) {
    bool Changed = false;
    LLVMContext &Ctx = F.getContext();

    SmallVector<BasicBlock *, 32> OrigBlocks;
    for (BasicBlock &BB : F) {
        if (BB.size() < 2 || BB.isLandingPad())
            continue;
        OrigBlocks.push_back(&BB);
    }

    for (BasicBlock *BB : OrigBlocks) {
        Instruction *InsertPt = BB->getFirstNonPHIOrDbgOrLifetime();
        if (!InsertPt || isa<LandingPadInst>(InsertPt))
            continue;

            if (BB->isEHPad())
            continue;

        uint32_t NumJunk = RandomGenerator::generateRange(1, 3);

        for (uint32_t i = 0; i < NumJunk; ++i) {
            BasicBlock *NextBB = BB->splitBasicBlock(InsertPt, "omvll.cont");
            if (!NextBB) break;

  
            BB->getTerminator()->eraseFromParent();

            BasicBlock *JunkBB = BasicBlock::Create(Ctx, "omvll.junk", &F);

            IRBuilder<> BBBuilder(BB);

            Value *GoToJunk;
            if (RandomGenerator::generateRange(0, 1) == 0) {
                GoToJunk = createOpaqueAlwaysFalse(BBBuilder, Ctx);
            } else {
                GoToJunk = createOpaqueAlwaysTrue(BBBuilder, Ctx);
                BBBuilder.CreateCondBr(GoToJunk, NextBB, JunkBB);
                goto junk_fill; 
            }

            BBBuilder.CreateCondBr(GoToJunk, JunkBB, NextBB);

            junk_fill:
            {
                IRBuilder<> JunkBuilder(JunkBB);

                uint32_t Pattern = RandomGenerator::generateRange(0, 3);
                switch (Pattern) {
                case 0:
                    emitJunkArithChain(JunkBuilder, Ctx);
                    JunkBuilder.CreateBr(NextBB);
                    break;
                case 1:
                    emitJunkInlineAsm(JunkBuilder, Ctx);
                    emitJunkArithChain(JunkBuilder, Ctx);
                    JunkBuilder.CreateBr(NextBB);
                    break;
                case 2:
                    emitJunkArrayOps(JunkBuilder, Ctx);
                    JunkBuilder.CreateBr(NextBB);
                    break;
                case 3:
                    emitJunkArithChain(JunkBuilder, Ctx);
                    emitJunkArrayOps(JunkBuilder, Ctx);
                    JunkBuilder.CreateBr(NextBB);
                    break;
                }
            }

            BB = NextBB;
            InsertPt = NextBB->getFirstNonPHIOrDbgOrLifetime();
            if (!InsertPt) break;

            Changed = true;
        }
    }

    return Changed;
}

PreservedAnalyses JunkCode::run(Module &M, ModuleAnalysisManager &MAM) {
    if (isModuleGloballyExcluded(&M))
        return PreservedAnalyses::all();

    bool Changed = false;
    PyConfig &Config = PyConfig::instance();
    ObfuscationConfig *UserConfig = Config.getUserConfig();

    SINFO("[JunkCode] Executing on module {}", M.getName());

    for (Function &F : M) {
        if (isFunctionGloballyExcluded(&F) || F.isDeclaration() ||
            F.isIntrinsic() || F.getName().starts_with("__omvll") ||
            F.getInstructionCount() < 20)
            continue;

        bool Enable = UserConfig->junkCode(&M, &F);
        if (!Enable) continue;

        SINFO("[JunkCode] Processing function: {}", F.getName().str());
        Changed |= process(F);
    }

    SINFO("[JunkCode] Changes {} applied", Changed ? "were" : "not");
    return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

} // namespace omvll