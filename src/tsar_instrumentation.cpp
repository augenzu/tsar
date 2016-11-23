//===- tsar_instrumentation.cpp - TSAR Instrumentation Engine ---*- C++ -*-===//
//
//                       Traits Static Analyzer (SAPFOR)
//
// This file implements LLVM IR level instrumentation engine.
//
//===----------------------------------------------------------------------===//

#include <llvm/ADT/Statistic.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/Debug.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/IR/Function.h>
#ifdef FUNCTION_CALL_COUNTERS
#include <llvm/IR/IRBuilder.h>
#endif
#include "tsar_instrumentation.h"

using namespace llvm;
using namespace tsar;

#undef DEBUG_TYPE
#define DEBUG_TYPE "instrumentation"

STATISTIC(NumInstLoop, "Number of instrumented loops");

char InstrumentationPass::ID = 0;
INITIALIZE_PASS_BEGIN(InstrumentationPass, "instrumentation",
  "LLVM IR Instrumentation", false, false)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
INITIALIZE_PASS_END(InstrumentationPass, "instrumentation",
  "LLVM IR Instrumentation", false, false)

#ifdef FUNCTION_CALL_COUNTERS
void createPrintfCall(llvm::Module *module, llvm::BasicBlock *insertAtEnd, GlobalVariable *var);
#endif

bool InstrumentationPass::runOnFunction(Function &F) {
  releaseMemory();
  auto &LI = getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
  llvm::Module *M = F.getParent();

#ifdef FUNCTION_CALL_COUNTERS
    GlobalVariable *gvar_ptr_counter = new GlobalVariable(
            /*Module=*/*M,
            /*Type=*/Type::getInt32Ty(M->getContext()),
            /*isConstant=*/false,
            /*Linkage=*/GlobalValue::CommonLinkage,
            /*Initializer=*/0, // has initializer, specified below
            /*Name=*/Twine("__counter_") + F.getName());
    //gvar_ptr_counter->setAlignment(4);???
    ConstantInt *const_int_val = ConstantInt::get(M->getContext(), APInt(32, 0));
    gvar_ptr_counter->setInitializer(const_int_val);
    Instruction *lastInstruction;
    BasicBlock *lastBasicBlock = nullptr;
    for (auto &B : F) {
        lastBasicBlock = &B;
        for (auto &I : B) {
            lastInstruction = &I;
        }
    }
    errs() << "Adding counter to function " << F.getName() << "\n";

    //global++; instruction
    IRBuilder<> builder(lastInstruction);
    LoadInst *Load = builder.CreateLoad(gvar_ptr_counter);
    Value *Inc = builder.CreateAdd(builder.getInt32(1), Load);
    StoreInst *Store = builder.CreateStore(Inc, gvar_ptr_counter);
    Store->print(errs());
    errs() << "\n";

    if (F.getName().equals("main")) {
        if (lastBasicBlock != nullptr) {
            errs() << "Adding counters print to main function\n";
            createPrintfCall(M, lastBasicBlock, gvar_ptr_counter);
        } else {
            errs() << "!!! Cannot add counters print to main function - it has NO basic blocks\n";
        }
    }
#endif
  return true;
}

void InstrumentationPass::releaseMemory() {}

void InstrumentationPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<LoopInfoWrapperPass>();
}

FunctionPass * llvm::createInstrumentationPass() {
  return new InstrumentationPass();
}

#ifdef FUNCTION_CALL_COUNTERS
Function *getPrintfFunction(llvm::Module *mod) {
    //generated by output from
    //>llc -march=cpp loop1.ll
    PointerType *PointerTy_11 = PointerType::get(IntegerType::get(mod->getContext(), 8), 0);

    std::vector<Type *> FuncTy_13_args;
    FuncTy_13_args.push_back(PointerTy_11);
    FunctionType *FuncTy_13 = FunctionType::get(
            /*Result=*/IntegerType::get(mod->getContext(), 32),
            /*Params=*/FuncTy_13_args,
            /*isVarArg=*/true);

    Function *func_printf = mod->getFunction("printf");
    if (!func_printf) {
        func_printf = Function::Create(
                /*Type=*/FuncTy_13,
                /*Linkage=*/GlobalValue::ExternalLinkage,
                /*Name=*/"printf", mod); // (external, no body)
        func_printf->setCallingConv(CallingConv::C);
    }
    AttributeSet func_printf_PAL;
    {
        SmallVector<AttributeSet, 4> Attrs;
        AttributeSet PAS;
        {
            AttrBuilder B;
            PAS = AttributeSet::get(mod->getContext(), ~0U, B);
        }

        Attrs.push_back(PAS);
        func_printf_PAL = AttributeSet::get(mod->getContext(), Attrs);

    }
    func_printf->setAttributes(func_printf_PAL);
    return func_printf;
}

void createPrintfCall(llvm::Module *module, llvm::BasicBlock *insertAtEnd, GlobalVariable *var) {
    uint64_t symbolCount = 7 + 1;
    ArrayType *charArrayType = ArrayType::get(IntegerType::get(module->getContext(), 8), symbolCount);
    PointerType *pointerType = PointerType::get(charArrayType, 0);

    GlobalVariable *gvar_print_format_string = new GlobalVariable(/*Module=*/*module,
            /*Type=*/charArrayType,
            /*isConstant=*/true,
            /*Linkage=*/GlobalValue::PrivateLinkage,
            /*Initializer=*/0, // has initializer, specified below
            /*Name=*/".str");
    gvar_print_format_string->setAlignment(1);

    Constant *const_format_string = ConstantDataArray::getString(module->getContext(), "main=%d", true);
    ConstantInt *const_int32_zero = ConstantInt::get(module->getContext(), APInt(32, 0));

    std::vector<Constant *> const_ptr_indices;
    const_ptr_indices.push_back(const_int32_zero);
    const_ptr_indices.push_back(const_int32_zero);
    Constant *const_ptr_print_format = ConstantExpr::getGetElementPtr(nullptr, gvar_print_format_string, const_ptr_indices);

    gvar_print_format_string->setInitializer(const_format_string);

    LoadInst *loadVar = new LoadInst(var, "", false, insertAtEnd);
    loadVar->setAlignment(4);

    std::vector<Value *> printfParams;
    printfParams.push_back(const_ptr_print_format);
    printfParams.push_back(loadVar);

    Function *func_printf = getPrintfFunction(module);
    CallInst *printCall = CallInst::Create(func_printf, printfParams, "", insertAtEnd);
    printCall->setCallingConv(CallingConv::C);
    printCall->setTailCall(false);
    AttributeSet defaultSet;
    printCall->setAttributes(defaultSet);
}
#endif