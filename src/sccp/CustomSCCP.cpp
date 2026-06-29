#include "llvm/IR/Function.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
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
            Constant *C = ConstantExpr::get(BO->getOpcode(),
                                            LHS.getConstant(), RHS.getConstant());
            if (C)
                updateState(BO, LatticeValue::makeConstant(C));
            else
                markOverdefined(BO); 
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
            Constant *C = ConstantExpr::getCast(CI->getOpcode(),
                                                Op.getConstant(), CI->getType());
            if (C)
                updateState(CI, LatticeValue::makeConstant(C));
            else
                markOverdefined(CI);
        }
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
            Constant *C = ConstantExpr::getCompare(Cmp->getPredicate(),
                                                   LHS.getConstant(), RHS.getConstant());
            if (C)
                updateState(Cmp, LatticeValue::makeConstant(C));
            else
                markOverdefined(Cmp);
        }
    }

    void visitInstruction(Instruction *I) {
        if (auto *BO = dyn_cast<BinaryOperator>(I))
            visitBinaryOperator(BO);
        else if (auto *CI = dyn_cast<CastInst>(I))
            visitCastInstruction(CI);
        else if (auto *Cmp = dyn_cast<CmpInst>(I))
            visitCmpInstruction(Cmp);
        else if (!I->getType()->isVoidTy())
            markOverdefined(I); 
    }

    void visit(Instruction &I) {
        if (auto *PN = dyn_cast<PHINode>(&I))
            visitPHINode(PN);
        else if (I.isTerminator())
            visitTerminator(&I);
        else
            visitInstruction(&I);
    }

    void solve(Function &F) {
        initialize(F);
        while (!CFGWorklist.empty() || !SSAWorklist.empty()) {
            while (!CFGWorklist.empty()) {
                CFGEdge E = CFGWorklist.pop_back_val();
                for (Instruction &I : *E.second)
                    visit(I);
            }

            while (!SSAWorklist.empty()) {
                Instruction *I = SSAWorklist.pop_back_val();
                if (isBlockExecutable(I->getParent()))
                    visit(*I);
            }
        }
    }

    void visitTerminator(Instruction *TI) {
        BasicBlock *BB = TI->getParent();

        if (auto *BI = dyn_cast<BranchInst>(TI)) {
            if (BI->isUnconditional()) {
                markEdgeExecutable(BB, BI->getSuccessor(0));
                return;
            }
            LatticeValue Cond = getValueState(BI->getCondition());
            if (Cond.isUndef())
                return; 
            if (Cond.isOverdefined()) {
                markEdgeExecutable(BB, BI->getSuccessor(0));
                markEdgeExecutable(BB, BI->getSuccessor(1));
                return;
            }

            auto *CI = dyn_cast<ConstantInt>(Cond.getConstant());
            if (!CI) { 
                markEdgeExecutable(BB, BI->getSuccessor(0));
                markEdgeExecutable(BB, BI->getSuccessor(1));
                return;
            }

            BasicBlock *Live =
                CI->isZero() ? BI->getSuccessor(1) : BI->getSuccessor(0);
            markEdgeExecutable(BB, Live);
            return;
        }

        if (auto *SI = dyn_cast<SwitchInst>(TI)) {
            LatticeValue Cond = getValueState(SI->getCondition());
            if (Cond.isUndef())
                return;
            if (Cond.isConstant()) {
                if (auto *CI = dyn_cast<ConstantInt>(Cond.getConstant())) {
                    markEdgeExecutable(BB, SI->findCaseValue(CI)->getCaseSuccessor());
                    return;
                }
            }

            for (BasicBlock *Succ : successors(BB))
                markEdgeExecutable(BB, Succ);
            return;
        }

        for (BasicBlock *Succ : successors(BB))
            markEdgeExecutable(BB, Succ);
    }

    void visitPHINode(PHINode *PN) {
        BasicBlock *PhiBB = PN->getParent();

        LatticeValue Result = LatticeValue::makeUndef();
        for (unsigned i = 0, e = PN->getNumIncomingValues(); i < e; ++i) {
            BasicBlock *InBB = PN->getIncomingBlock(i);
            if (!isEdgeExecutable(InBB, PhiBB))
                continue;
            meet(Result, getValueState(PN->getIncomingValue(i)));
            if (Result.isOverdefined())
                break; 
        }
        updateState(PN, Result);
    }

    bool rewrite(Function &F) {
        bool Changed = false;

        SmallVector<Instruction *, 32> ToErase;
        for (BasicBlock &BB : F) {
            if (!isBlockExecutable(&BB))
                continue;
            for (Instruction &I : BB) {
                if (I.isTerminator() || I.getType()->isVoidTy() || isa<Constant>(&I))
                    continue;
                LatticeValue LV = getValueState(&I);
                if (!LV.isConstant())
                    continue;
                I.replaceAllUsesWith(LV.getConstant());
                ToErase.push_back(&I);
                Changed = true;
            }
        }
        for (Instruction *I : ToErase)
            I->eraseFromParent();

        for (BasicBlock &BB : F) {
            if (!isBlockExecutable(&BB))
                continue;
            Instruction *TI = BB.getTerminator();
            if (TI->getNumSuccessors() < 2)
                continue;

            SmallPtrSet<BasicBlock *, 4> Live, Dead;
            for (BasicBlock *S : successors(&BB))
                (isEdgeExecutable(&BB, S) ? Live : Dead).insert(S);
     
                for (BasicBlock *S : Live)
                Dead.erase(S);

            if (Live.size() != 1 || Dead.empty())
                continue; 

            BasicBlock *LiveSucc = *Live.begin();
            for (BasicBlock *D : Dead)
                D->removePredecessor(&BB);
            TI->eraseFromParent();
            BranchInst::Create(LiveSucc, &BB);
            Changed = true;
        }

        SmallVector<BasicBlock *, 16> DeadBlocks;
        for (BasicBlock &BB : F)
            if (!isBlockExecutable(&BB))
                DeadBlocks.push_back(&BB);

        for (BasicBlock *BB : DeadBlocks)
            for (BasicBlock *S : successors(BB))
                if (isBlockExecutable(S))
                    S->removePredecessor(BB);

        for (BasicBlock *BB : DeadBlocks)
            BB->dropAllReferences();
        for (BasicBlock *BB : DeadBlocks) {
            BB->eraseFromParent();
            Changed = true;
        }

        return Changed;
    }

    void dump(Function &F, raw_ostream &OS) const {
        OS << "=== SCCP rezultat za @" << F.getName() << " ===\n";
        for (BasicBlock &BB : F)
            OS << "  blok %" << BB.getName() << " : "
               << (isBlockExecutable(&BB) ? "EXECUTABLE" : "DEAD (unreachable)")
               << "\n";
        OS << "  -- ivice --\n";
        for (BasicBlock &BB : F)
            for (BasicBlock *Succ : successors(&BB))
                OS << "    " << BB.getName() << " -> " << Succ->getName() << " : "
                   << (isEdgeExecutable(&BB, Succ) ? "live" : "dead") << "\n";
        OS << "  -- lattice vrednosti --\n";
        for (BasicBlock &BB : F)
            for (Instruction &I : BB) {
                auto It = ValueState.find(&I);
                if (It == ValueState.end())
                    continue;
                const LatticeValue &LV = It->second;
                OS << "   " << I << "   =>   ";
                if (LV.isUndef())
                    OS << "undef";
                else if (LV.isConstant()) {
                    OS << "const ";
                    LV.getConstant()->printAsOperand(OS, /*PrintType=*/false);
                } else
                    OS << "overdefined";
                OS << "\n";
            }
    }
};

static cl::opt<bool>
    Verbose("custom-sccp-verbose",
            cl::desc("Ispisi executable blokove/ivice nakon SCCP analize"),
            cl::init(false));

namespace {
struct CustomSCCPLegacyPass : public FunctionPass {
    static char ID;
    CustomSCCPLegacyPass() : FunctionPass(ID) {}

    bool runOnFunction(Function &F) override {
        if (F.isDeclaration())
            return false;
        SCCPSolver Solver;
        Solver.solve(F);
        if (Verbose)
            Solver.dump(F, errs());
        return Solver.rewrite(F);
    }

    StringRef getPassName() const override { return "Custom SCCP"; }
};
}

char CustomSCCPLegacyPass::ID = 0;

static RegisterPass<CustomSCCPLegacyPass>
    X("custom-sccp", "Custom Sparse Conditional Constant Propagation",
      false, false);
