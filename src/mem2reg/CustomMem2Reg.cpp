#include <vector>
#include <utility>

#include "llvm/IR/Instructions.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/CFG.h"
#include "llvm/ADT/SmallPtrSet.h"

using namespace llvm;

static cl::opt<bool> CustomVerbose(
    "custom-verbose", cl::desc("Print logs for analyzed allocas"),
    cl::init(false));

static cl::opt<bool> CustomPhi(
    "custom-phi", cl::desc("Enable restricted diamond phi insertion"),
    cl::init(false));

namespace {
    struct CustomMem2Reg : public FunctionPass {
        static char ID;

        CustomMem2Reg() : FunctionPass(ID) {}

        static bool collectUses(AllocaInst *ai, std::vector<StoreInst *> &stores,
                                std::vector<LoadInst *> &loads) {
            stores.clear();
            loads.clear();

            for (User *user : ai->users()) {
                if (auto *li = dyn_cast<LoadInst>(user)) {
                    if (li->isVolatile())
                        return false;

                    loads.push_back(li);

                } else if (auto *si = dyn_cast<StoreInst>(user)) {
                    if (si->isVolatile())
                        return false;

                    if (si->getValueOperand() == ai)
                        return false;

                    stores.push_back(si);

                } else {
                    return false;
                }
            }

            return true;
        }

        static BasicBlock *findTwoPredMerge(BasicBlock *b1, BasicBlock *b2) {
            if (b1 == b2)
                return nullptr;

            for (BasicBlock *succ : successors(b1)) {
                SmallPtrSet<BasicBlock *, 4> preds;
                for (BasicBlock *p : predecessors(succ))
                    preds.insert(p);

                if (preds.size() == 2 && preds.count(b1) && preds.count(b2))
                    return succ;
            }
            return nullptr;
        }

        bool runOnFunction(Function &F) override {
            DominatorTree &domTree = getAnalysis<DominatorTreeWrapperPass>().getDomTree();
            bool changed = false;

            std::vector<AllocaInst *> allocas;
            for (BasicBlock &bb : F) {
                for (Instruction &i : bb) {
                    if (auto *ai = dyn_cast<AllocaInst>(&i))
                        allocas.push_back(ai);
                }
            }

            if (CustomVerbose)
                errs() << "CustomMem2Reg: " << F.getName() << " "
                       << allocas.size() << " alloca\n";

            for (AllocaInst *ai : allocas) {
                if (!ai->isStaticAlloca())
                    continue;

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

                if (stores.empty()) {
                    for (LoadInst *li : loads) {
                        li->replaceAllUsesWith(UndefValue::get(li->getType()));
                        li->eraseFromParent();
                    }

                    ai->eraseFromParent();

                    if (CustomVerbose)
                        errs() << "  [zero-store] loads replaced with undef\n";

                    changed = true;
                    continue;
                }

                if (loads.empty()) {
                    for (StoreInst *si : stores)
                        si->eraseFromParent();

                    ai->eraseFromParent();

                    if (CustomVerbose)
                        errs() << "  [dead-store] stores removed\n";

                    changed = true;
                    continue;
                }

                if (CustomPhi && stores.size() == 2) {
                    StoreInst *s1 = stores[0];
                    StoreInst *s2 = stores[1];
                    BasicBlock *b1 = s1->getParent();
                    BasicBlock *b2 = s2->getParent();

                    BasicBlock *merge = findTwoPredMerge(b1, b2);
                    if (!merge)
                        continue;

                    Value *v1 = s1->getValueOperand();
                    Value *v2 = s2->getValueOperand();

                    PHINode *phi = PHINode::Create(ai->getAllocatedType(), 2, "",
                                                merge->getFirstNonPHI());
                    phi->addIncoming(v1, b1);
                    phi->addIncoming(v2, b2);

                    bool allCovered = true;
                    std::vector<std::pair<LoadInst *, Value *>> repl;
                    for (LoadInst *li : loads) {
                        if (domTree.dominates(s1, li))
                            repl.push_back({li, v1});
                        else if (domTree.dominates(s2, li))
                            repl.push_back({li, v2});
                        else if (domTree.dominates(phi, li))
                            repl.push_back({li, phi});
                        else {
                            allCovered = false;
                            break;
                        }
                    }

                    if (!allCovered) {
                        phi->eraseFromParent();
                        continue;
                    }

                    for (auto &pr : repl) {
                        pr.first->replaceAllUsesWith(pr.second);
                        pr.first->eraseFromParent();
                    }
                    s1->eraseFromParent();
                    s2->eraseFromParent();
                    ai->eraseFromParent();

                    if (CustomVerbose)
                        errs() << "  [diamond-phi] promoted\n";

                    changed = true;
                    continue;
                }

                if (stores.size() == 1) {
                    StoreInst *onlyStore = stores[0];
                    Value *storedVal = onlyStore->getValueOperand();

                    bool allDominated = true;
                    for (LoadInst *li : loads) {
                        if (!domTree.dominates(onlyStore, li)) {
                            allDominated = false;
                            break;
                        }
                    }

                    if (!allDominated)
                        continue;

                    for (LoadInst *li : loads) {
                        li->replaceAllUsesWith(storedVal);
                        li->eraseFromParent();
                    }
                    
                    onlyStore->eraseFromParent();
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