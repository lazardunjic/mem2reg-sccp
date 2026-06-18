#include "llvm/IR/Function.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h" 

using namespace llvm;

namespace{
    struct CustomSCCP : public FunctionPass{
        static char ID;
        CustomSCCP() : FunctionPass(ID) {}
        bool runOnFunction(Function &F) override {
            return false;
        }
    };
}

char CustomSCCP::ID = 0;
static RegisterPass<CustomSCCP> X("custom-sccp", "Custom sccp pass");
