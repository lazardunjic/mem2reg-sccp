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


