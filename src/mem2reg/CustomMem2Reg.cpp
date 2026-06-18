#include "llvm/IR/Function.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h" 

using namespace llvm;

namespace{
    struct CustomMem2Reg : public FunctionPass{
        static char ID;
        CustomMem2Reg() : FunctionPass(ID) {}
        bool runOnFunction(Function &F) override {
            return false;
        }
    };
}

char CustomMem2Reg::ID = 0;
static RegisterPass<CustomMem2Reg> X("custom-mem2reg", "Custom mem2reg pass");
