#include <vector>

#include "llvm/IR/Instructions.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h" 
#include "llvm/Support/CommandLine.h"
#include "llvm/IR/Dominators.h"

using namespace llvm;

static cl::opt<bool> CustomVerbose(
    "custom-verbose", cl::desc("Print logs for analyzed allocas"),
    cl::init(false));

namespace{
    struct CustomMem2Reg : public FunctionPass{
        static char ID;

        CustomMem2Reg() : FunctionPass(ID) {}
        
        static bool collectUses(AllocaInst *ai, std::vector<StoreInst *> &stores, std::vector<LoadInst *> &loads){
            stores.clear();
            loads.clear();

            for (User *user : ai->users()) {
                if (auto *li = dyn_cast<LoadInst>(user)) {
                    if (li->isVolatile()) 
                        return false;
                    
                    loads.push_back(li);
                
                }else if (auto *si = dyn_cast<StoreInst>(user)) {
                    if (si->isVolatile()) 
                        return false;
      
                    if (si->getValueOperand() == ai) 
                        return false;
      
                    stores.push_back(si);
                
                }else {
                    return false;
                }
            }
        
            return true;
        }

        bool runOnFunction(Function &F) override {
            DominatorTree &DomTree = getAnalysis<DominatorTreeWrapperPass>().getDomTree();
            bool changed = false;

            std::vector<AllocaInst *> Allocas;
            for(BasicBlock &bb: F){
                for(Instruction &i: bb){
                    if(auto *ai = dyn_cast<AllocaInst>(&i))
                        Allocas.push_back(ai);
                }
            }

            if(CustomVerbose)
                errs() << "CustomMem2Reg: " << F.getName() <<  " " << Allocas.size() << " alloca\n";

            for (AllocaInst *ai : Allocas) {
                if (!ai->isStaticAlloca()) continue;

                std::vector<StoreInst *> stores;
                std::vector<LoadInst *> loads;

                if (!collectUses(ai, stores, loads)) {
                    if (CustomVerbose) 
                        errs() << "  [ESCAPE] " << *ai << "\n";
                        continue;
                    }

                    if (CustomVerbose)
                        errs() << "  [PROMOTABLE] " << *ai
                               << "  (loads=" << loads.size()
                               << ", stores=" << stores.size() << ")\n";

                if (stores.size() == 1) {
                    StoreInst *OnlyStore = stores[0];
                    Value *StoredVal = OnlyStore->getValueOperand();

                    bool AllDominated = true;
                    for (LoadInst *li : loads) {
                        if (!DomTree.dominates(OnlyStore, li)) {
                        AllDominated = false;
                        break;
                        }
                    }

                    if (!AllDominated)
                        continue;

                    for (LoadInst *li : loads) {
                        li->replaceAllUsesWith(StoredVal);
                        li->eraseFromParent();
                    }
                    
                    OnlyStore->eraseFromParent();
                    ai->eraseFromParent();

                    if (CustomVerbose)
                        errs() << "  [single-store] promoted\n";
                    
                    changed = true;
                    continue;
                }
            }
                    
            return changed;
        }

        void getAnalysisUsage(AnalysisUsage &au) const override {
            au.addRequired<DominatorTreeWrapperPass>();
        }
    };
}

char CustomMem2Reg::ID = 0;
static RegisterPass<CustomMem2Reg> X("custom-mem2reg", "Custom mem2reg pass");
