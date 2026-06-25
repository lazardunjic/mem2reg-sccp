#include "llvm/IR/Function.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h" 
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"


using namespace llvm;


struct LatticeValue {
    enum class State : uint8_t { Undef, Constant, Overdefined };

    State St  = State::Undef;
    Constant *Val = nullptr;   

    static LatticeValue makeUndef() {
        return {State::Undef, nullptr};
    }
    static LatticeValue makeOverdefined() {
        return {State::Overdefined, nullptr};
    }
    static LatticeValue makeConstant(Constant *C) {
        assert(C && "Constant ne sme biti null");
        return {State::Constant, C};
    }

    bool isUndef()       const { return St == State::Undef; }
    bool isConstant()    const { return St == State::Constant; }
    bool isOverdefined() const { return St == State::Overdefined; }

    Constant *getConstant() const {
        assert(isConstant() && "LatticeValue nije u Constant stanju");
        return Val;
    }
};


static bool meet(LatticeValue &Into, const LatticeValue &Other) {
    
    if (Into.isOverdefined())
        return false;
   
    if (Other.isUndef())
        return false;
    
    if (Into.isUndef()) {
        Into = Other;
        return true;
    }

    if (Into.isConstant() && Other.isConstant()) {
        if (Into.getConstant() == Other.getConstant())
            return false;                        
        Into = LatticeValue::makeOverdefined();   
        return true;
    }

    Into = LatticeValue::makeOverdefined();
    return true;
}

using CFGEdge = std::pair<BasicBlock *, BasicBlock *>;

struct SCCPSolver {

    DenseMap<Value *, LatticeValue> ValueState;

    DenseSet<CFGEdge>    ExecutableEdges;
    DenseSet<BasicBlock *> ExecutableBlocks;

    SmallVector<CFGEdge, 64>      CFGWorklist;

    SmallVector<Instruction *, 64> SSAWorklist;


    LatticeValue &getValueState(Value *V) {
        auto It = ValueState.find(V);
        if (It != ValueState.end())
            return It->second;

        if (auto *C = dyn_cast<Constant>(V)) {
            ValueState[V] = LatticeValue::makeConstant(C);
            return ValueState[V];
        }

        ValueState[V] = LatticeValue::makeUndef();
        return ValueState[V];
    }

    bool markEdgeExecutable(BasicBlock *From, BasicBlock *To) {
        if (!ExecutableEdges.insert({From, To}).second)
            return false;                        
        ExecutableBlocks.insert(To);
        CFGWorklist.push_back({From, To});
        return true;
    }

    bool isEdgeExecutable(BasicBlock *From, BasicBlock *To) const {
        return ExecutableEdges.count({From, To}) > 0;
    }

    bool isBlockExecutable(BasicBlock *BB) const {
        return ExecutableBlocks.count(BB) > 0;
    }

    void updateState(Instruction *I, LatticeValue NewVal) {
        LatticeValue &Old = getValueState(I);
        if (!meet(Old, NewVal))
            return;                          
        for (User *U : I->users())
            if (auto *UI = dyn_cast<Instruction>(U))
                SSAWorklist.push_back(UI);
    }

    void markOverdefined(Value *V) {
        LatticeValue &LV = getValueState(V);
        if (LV.isOverdefined())
            return;
        LV = LatticeValue::makeOverdefined();
        if (auto *I = dyn_cast<Instruction>(V))
            for (User *U : I->users())
                if (auto *UI = dyn_cast<Instruction>(U))
                    SSAWorklist.push_back(UI);
    }

    void initialize(Function &F) {
        for (Argument &Arg : F.args())
            markOverdefined(&Arg);

        BasicBlock *Entry = &F.getEntryBlock();
        ExecutableBlocks.insert(Entry);
        CFGWorklist.push_back({nullptr, Entry});
    }
    
    void visitBinaryOperator(BinaryOperator *BO) {
        if (getValueState(BO).isOverdefined())
            return;

        LatticeValue &LHS = getValueState(BO->getOperand(0));
        LatticeValue &RHS = getValueState(BO->getOperand(1));

        if (LHS.isOverdefined() || RHS.isOverdefined()) {
            markOverdefined(BO);
            return;
        }

        if (LHS.isConstant() && RHS.isConstant()) {
            Constant *C = ConstantExpr::get(
                BO->getOpcode(),
                LHS.getConstant(),
                RHS.getConstant()
            );
            if (C) {
                updateState(BO, LatticeValue::makeConstant(C));
                return;
            }
            // Fold nije uspeo (npr. deljenje nulom) -> konzervativno Overdefined
            markOverdefined(BO);
            return;
        }
    }

    void visitCastInstruction(CastInst *CI) {
        if (getValueState(CI).isOverdefined())
            return;

        LatticeValue &Op = getValueState(CI->getOperand(0));

        if (Op.isOverdefined()) {
            markOverdefined(CI);
            return;
        }

        if (Op.isConstant()) {
            Constant *C = ConstantExpr::getCast(
                CI->getOpcode(),
                Op.getConstant(),
                CI->getType()
            );
            if (C) {
                updateState(CI, LatticeValue::makeConstant(C));
                return;
            }
            markOverdefined(CI);
            return;
        }
        // Operand je Undef -> ostajemo Undef
    }

    void visitCmpInstruction(CmpInst *Cmp) {
        if (getValueState(Cmp).isOverdefined())
            return;

        LatticeValue &LHS = getValueState(Cmp->getOperand(0));
        LatticeValue &RHS = getValueState(Cmp->getOperand(1));

        if (LHS.isOverdefined() || RHS.isOverdefined()) {
            markOverdefined(Cmp);
            return;
        }

        if (LHS.isConstant() && RHS.isConstant()) {
            Constant *C = ConstantExpr::getCompare(
                Cmp->getPredicate(),
                LHS.getConstant(),
                RHS.getConstant()
            );
            if (C) {
                updateState(Cmp, LatticeValue::makeConstant(C));
                return;
            }
            markOverdefined(Cmp);
            return;
        }
    }

    void visitInstruction(Instruction *I) {
        // Nikad ne visitujemo instrukcije u mrtvim blokovima
        if (!isBlockExecutable(I->getParent()))
            return;

        if (auto *BO = dyn_cast<BinaryOperator>(I))
            visitBinaryOperator(BO);
        else if (auto *CI = dyn_cast<CastInst>(I))
            visitCastInstruction(CI);
        else if (auto *Cmp = dyn_cast<CmpInst>(I))
            visitCmpInstruction(Cmp);
        // PHINode i BranchInst -> D implementira
        // Sve ostalo (call, load, store...) -> konzervativno Overdefined
        else if (!isa<PHINode>(I) && !isa<BranchInst>(I) &&
                 !isa<ReturnInst>(I) && !isa<UnreachableInst>(I)) {
            if (!I->getType()->isVoidTy())
                markOverdefined(I);
            }
        }

    void solve(Function &F) {
        initialize(F);

        while (!CFGWorklist.empty() || !SSAWorklist.empty()) {

            // Obradjujemo nove executable blokove
            while (!CFGWorklist.empty()) {
                CFGEdge Edge = CFGWorklist.pop_back_val();
                BasicBlock *BB = Edge.second;
                for (Instruction &I : *BB)
                    visitInstruction(&I);
            }

            // Obradjujemo instrukcije ciji operandi su se promenili
            while (!SSAWorklist.empty()) {
                Instruction *I = SSAWorklist.pop_back_val();
                visitInstruction(I);
            }
        }
    }
};

namespace {

    struct CustomSCCP : public FunctionPass {
        static char ID;
        CustomSCCP() : FunctionPass(ID) {}

        bool runOnFunction(Function &F) override {
            SCCPSolver Solver;
            Solver.solve(F);

            errs() << "=== CustomSCCP: " << F.getName() << " ===\n";
            for (auto &BB : F) {
                errs() << BB.getName() << ":\n";
                for (auto &I : BB) {
                    if (I.getType()->isVoidTy())
                        continue;
                    auto It = Solver.ValueState.find(&I);
                    if (It == Solver.ValueState.end())
                        continue;
                    const LatticeValue &LV = It->second;
                    errs() << "  ";
                    I.printAsOperand(errs(), false);
                    errs() << "  ->  ";
                    if (LV.isUndef())
                        errs() << "Undef\n";
                    else if (LV.isOverdefined())
                        errs() << "Overdefined\n";
                    else
                        errs() << "Constant(" << *LV.getConstant() << ")\n";
                }
            }

            return false; 
        }

        void getAnalysisUsage(AnalysisUsage &AU) const override {
            AU.setPreservesAll(); // skloniti kad D doda rewrite
        }
    };

}

char CustomSCCP::ID = 0;
static RegisterPass<CustomSCCP> X("custom-sccp", "Custom SCCP pass");

