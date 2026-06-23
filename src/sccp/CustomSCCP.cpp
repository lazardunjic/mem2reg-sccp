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

