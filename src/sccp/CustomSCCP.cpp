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
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"


using namespace llvm;


//============================================================================//
//  [Nedeljko]  lattice + solver stanje. NE menjati bez dogovora.
//============================================================================//

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

    //  [Filip]  control-flow + rewrite. Koristi lattice/state gore.


    // [C TODO] obicna instrukcija (add/sub/mul/icmp/cast...). Dok C ne doda
    // pravi folding, konzervativno -> overdefined.
    void visitInstruction(Instruction *I) {
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

    // Glavna petlja (edge-based CFG worklist + SSA worklist) do fiksne tacke.
    void solve(Function &F) {
        initialize(F);
        while (!CFGWorklist.empty() || !SSAWorklist.empty()) {
            // nova executable ivica -> obidji instrukcije ciljnog bloka
            while (!CFGWorklist.empty()) {
                CFGEdge E = CFGWorklist.pop_back_val();
                for (Instruction &I : *E.second)
                    visit(I);
            }
            // promenjena vrednost -> re-evaluiraj korisnika (u zivom bloku)
            while (!SSAWorklist.empty()) {
                Instruction *I = SSAWorklist.pop_back_val();
                if (isBlockExecutable(I->getParent()))
                    visit(*I);
            }
        }
    }

    //  granu markiramo executable SAMO ako je dostizna.
    //   const uslov -> jedna grana ; overdefined -> obe ; undef -> nijedna
    void visitTerminator(Instruction *TI) {
        BasicBlock *BB = TI->getParent();

        if (auto *BI = dyn_cast<BranchInst>(TI)) {
            if (BI->isUnconditional()) {
                markEdgeExecutable(BB, BI->getSuccessor(0));
                return;
            }
            LatticeValue Cond = getValueState(BI->getCondition());
            if (Cond.isUndef())
                return; // uslov jos nepoznat -> ne markiraj nista (cekaj)
            if (Cond.isOverdefined()) {
                markEdgeExecutable(BB, BI->getSuccessor(0));
                markEdgeExecutable(BB, BI->getSuccessor(1));
                return;
            }
            // Constant: tacno jedna grana je ziva
            auto *CI = dyn_cast<ConstantInt>(Cond.getConstant());
            if (!CI) { // npr. ConstantExpr -> konzervativno obe
                markEdgeExecutable(BB, BI->getSuccessor(0));
                markEdgeExecutable(BB, BI->getSuccessor(1));
                return;
            }
            // br i1: successor(0)=true grana, successor(1)=false grana
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
            // overdefined / nepoznat oblik -> svi successor-i
            for (BasicBlock *Succ : successors(BB))
                markEdgeExecutable(BB, Succ);
            return;
        }

        // ret / unreachable nemaju successor-e; ostalo konzervativno svi.
        for (BasicBlock *Succ : successors(BB))
            markEdgeExecutable(BB, Succ);
    }

    // vrednost phi = meet incoming vrednosti CIJE su ulazne ivice executable. 
    // Ivice iz mrtvih blokova se ignorisu -> zato je SCCP precizniji od obicnog folding-a
    // (npr. phi [10,%a],[20,%b] gde je %b mrtav => const 10).
    
    void visitPHINode(PHINode *PN) {
        BasicBlock *PhiBB = PN->getParent();

        LatticeValue Result = LatticeValue::makeUndef();
        for (unsigned i = 0, e = PN->getNumIncomingValues(); i < e; ++i) {
            BasicBlock *InBB = PN->getIncomingBlock(i);
            // Ulaz se racuna samo ako je ulazna ivica InBB -> PhiBB dostizna.
            if (!isEdgeExecutable(InBB, PhiBB))
                continue;
            meet(Result, getValueState(PN->getIncomingValue(i)));
            if (Result.isOverdefined())
                break; // ne moze nize, nema potrebe dalje
        }
        // updateState meet-uje Result u trenutno stanje (monotono) i,
        // ako se promenilo, gura korisnike na SSA worklist.
        updateState(PN, Result);
    }

    // KORAK 3 (TODO): constant replace + brisanje mrtvih blokova/ivica.
    bool rewrite(Function &F) {
        return false;
    }

    // Debug: koji su blokovi/ivice executable nakon solve().
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


//============================================================================//
//  Legacy FunctionPass + registracija (-custom-sccp).
//============================================================================//

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
} // namespace

char CustomSCCPLegacyPass::ID = 0;

static RegisterPass<CustomSCCPLegacyPass>
    X("custom-sccp", "Custom Sparse Conditional Constant Propagation",
      false, false);
