#include <vector>

#include "llvm/IR/Instructions.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h" 
#include "llvm/Support/CommandLine.h"

using namespace llvm;

static cl::opt<bool> CustomVerbose(
    "custom-verbose", cl::desc("Print logs for analyzed allocas"),
    cl::init(false));

namespace{
    struct CustomMem2Reg : public FunctionPass{
        static char ID;

        CustomMem2Reg() : FunctionPass(ID) {}

        bool runOnFunction(Function &F) override {
            std::vector<AllocaInst *> Allocas;
            for(BasicBlock &bb: F){
                for(Instruction &i: bb){
                    if(auto *ai = dyn_cast<AllocaInst>(&i))
                        Allocas.push_back(ai);
                }
            }

            if(CustomVerbose)
                errs() << "CustomMem2Reg: " << F.getName() <<  " " << Allocas.size() << " alloca\n";

            return false;
        }
    };
}

char CustomMem2Reg::ID = 0;
static RegisterPass<CustomMem2Reg> X("custom-mem2reg", "Custom mem2reg pass");
