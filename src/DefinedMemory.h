//===--- DefinedMemory.h --- Defined Memory Analysis ------------*- C++ -*-===//
//
//                       Traits Static Analyzer (SAPFOR)
//
//===----------------------------------------------------------------------===//
//
// This file defines passes to determine must/may defined locations for each
// data-flow region. We use data-flow framework to implement this kind of
// analysis. This filecontains elements which is necessary to determine this
// framework.
// The following articles can be helpful to understand it:
//  * "Automatic Array Privatization" Peng Tu and David Padua
//  * "Array Privatization for Parallel Execution of Loops" Zhiyuan Li.
//
//===----------------------------------------------------------------------===//

#ifndef TSAR_DEFINED_MEMORY_H
#define TSAR_DEFINED_MEMORY_H

#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/ADT/DenseMap.h>
#include <llvm/Analysis/AliasSetTracker.h>
#include <llvm/Analysis/MemoryLocation.h>
#ifdef DEBUG
#include <llvm/IR/Instruction.h>
#endif//DEBUG
#include <llvm/Pass.h>
#include <utility.h>
#include "tsar_df_location.h"
#include "DFRegionInfo.h"
#include "tsar_data_flow.h"

namespace llvm {
class Value;
class Instruction;
class StoreInst;
}

namespace tsar {
/// \brief This contains locations which have outward exposed definitions or
/// uses in a data-flow node.
///
/// Let us use definitions from the article "Automatic Array Privatization"
/// written by Peng Tu and David Padua (page 6):
/// "A definition of variable v in a basic block S is said to be outward
/// exposed if it is the last definition of v in S. A use of v is outward
/// exposed if S does not contain a definition of v before this use". Note that
/// in case of loops locations which have outward exposed uses can get value
/// not only outside the loop but also from previous loop iterations.
class DefUseSet {
public:
  /// Set of pointers to locations.
  typedef llvm::SmallPtrSet<llvm::Value *, 64> PointerSet;

  /// Set of instructions.
  typedef llvm::SmallPtrSet<llvm::Instruction *, 64> InstructionSet;

  /// Constructor.
  DefUseSet(llvm::AliasAnalysis &AA) : mExplicitAccesses(AA) {}

  /// Returns set of the must defined locations.
  const LocationSet & getDefs() const { return mDefs; }

  /// Returns true if a location have definition in a data-flow node.
  ///
  /// \attention This method does not use alias information.
  bool hasDef(const llvm::MemoryLocation &Loc) const {
    return mDefs.contain(Loc);
  }

  /// Specifies that a location has definition in a data-flow node.
  ///
  /// \return False if it has been already specified.
  bool addDef(const llvm::MemoryLocation &Loc) {
    return mDefs.insert(Loc).second;
  }

  /// Specifies that a stored location have definition in a data-flow node.
  ///
  /// \return True if a new alias set has been created.
  bool addDef(llvm::Instruction *I) {
    assert(I && "Instruction must not be null!");
    assert(llvm::isa<llvm::StoreInst>(I) &&
      "Only store instructions produce must defined locations!");
    return addDef(llvm::MemoryLocation::get(I));
  }

  /// Returns set of the may defined locations.
  const LocationSet & getMayDefs() const { return mMayDefs; }

  /// Returns true if a location may have definition in a data-flow node.
  ///
  /// May define locations arise in following cases:
  /// - a data-flow node is a region and encapsulates other nodes.
  /// It is necessary to use this conservative assumption due to complexity of
  /// CFG analysis.
  /// - a location may overlap (may alias) or partially overlaps (partial alias)
  /// with another location which is must/may define locations.
  /// \attention
  /// - This method does not use alias information.
  /// - This method returns true even if only part of the location may have
  /// definition.
  bool hasMayDef(const llvm::MemoryLocation &Loc) const {
    return mMayDefs.overlap(Loc);
  }

  /// Specifies that a location may have definition in a data-flow node.
  ///
  /// \return False if it has been already specified.
  bool addMayDef(const llvm::MemoryLocation &Loc) {
    return mMayDefs.insert(Loc).second;
  }

  /// Specifies that a modified location may have definition in a data-flow node.
  ///
  /// \return False if it has been already specified.
  /// \pre The specified instruction may modify memory.
  bool addMayDef(llvm::Instruction *I) {
    assert(I && "Instruction must not be null!");
    assert(I->mayWriteToMemory() && "Instruction does not modify memory!");
    return addMayDef(llvm::MemoryLocation::get(I));
  }

  /// Returns set of the locations which get values outside a data-flow node.
  const LocationSet & getUses() const { return mUses; }

  /// Returns true if a location gets value outside a data-flow node.
  ///
  /// May use locations should be also counted because conservativeness
  /// of analysis must be preserved.
  /// \attention
  /// - This method does not use alias information.
  /// - This method returns true even if only part of the location
  /// get values outside a data-flow node.
  bool hasUse(const llvm::MemoryLocation &Loc) const {
    return mUses.overlap(Loc);
  }

  /// Specifies that a location gets values outside a data-flow node.
  ///
  /// \return False if it has been already specified.
  bool addUse(const llvm::MemoryLocation &Loc) {
    return mUses.insert(Loc).second;
  }

  /// Specifies that a location gets values outside a data-flow node.
  ///
  /// \return False if it has been already specified.
  /// \pre The specified instruction may read memory.
  bool addUse(llvm::Instruction *I) {
    assert(I && "Instruction must not be null!");
    assert(I->mayReadFromMemory() && "Instruction does not read memory!");
    return addUse(llvm::MemoryLocation::get(I));
  }

  /// Returns locations accesses to which are performed explicitly.
  ///
  /// For example, if p = &x and to access x, *p is used, let us assume that
  /// access to x is performed implicitly and access to *p is performed
  /// explicitly.
  const llvm::AliasSetTracker & getExplicitAccesses() const {
    return mExplicitAccesses;
  }

  /// Returns true if there are an explicit access to a location in the node.
  ///
  /// \attention This method returns true even if only part of the location
  /// has explicit access.
  bool hasExplicitAccess(const llvm::MemoryLocation &Loc) const;

  /// Specifies that there are an explicit access to a location in the node.
  ///
  /// \return True if a new alias set has been created.
  bool addExplicitAccess(const llvm::MemoryLocation &Loc) {
    assert(Loc.Ptr && "Pointer to memory location must not be null!");
    return mExplicitAccesses.add(
      const_cast<llvm::Value *>(Loc.Ptr), Loc.Size, Loc.AATags);
  }

  /// Specifies that there are an explicit access to a location in the node.
  ///
  /// \return True if a new alias set has been created.
  /// \pre The specified instruction may read or modify memory.
  bool addExplicitAccess(llvm::Instruction *I) {
    assert(I && "Instruction must not be null!");
    assert(I->mayReadOrWriteMemory() &&
      "Instruction does not read nor write memory!");
    return mExplicitAccesses.add(I);
  }

  /// Specifies that accesses to all locations from AST are performed
  /// explicitly.
  void addExplicitAccesses(const llvm::AliasSetTracker &AST) {
    mExplicitAccesses.add(AST);
  }

  /// Returns locations addresses of which are explicitly evaluated in the node.
  ///
  /// For example, if &x expression occurs in the node then address of
  /// the x 'alloca' is evaluated. It means that regardless of whether the
  /// location will be privatized the original location address should be
  /// available.
  const PointerSet & getAddressAccesses() const { return mAddressAccesses; }

  /// Returns true if there are evaluation of a location address in the node.
  bool hasAddressAccess(llvm::Value *Ptr) const {
    assert(Ptr && "Pointer to memory location must not be null!");
    return mAddressAccesses.count(Ptr) != 0;
  }

  /// Specifies that there are evaluation of a location address in the node.
  ///
  /// \return False if it has been already specified.
  bool addAddressAccess(llvm::Value *Ptr) {
    assert(Ptr && "Pointer to memory location must not be null!");
    return mAddressAccesses.insert(Ptr).second;
  }

  /// Returns unknown instructions which are evaluated in the node.
  ///
  /// An unknown instruction is a instruction which accessed memory with unknown
  /// description. For example, in general case call instruction is an unknown
  /// instruction.
  const InstructionSet & getUnknownInsts() const { return mUnknownInsts; }

  /// Returns true if there are an unknown instructions in the node.
  bool hasUnknownInst(llvm::Instruction *I) const {
    assert(I && "Instruction must not be null!");
    return mUnknownInsts.count(I) != 0;
  }

  /// Specifies that there are unknown instructions in the node.
  ///
  /// \return False if it has been already specified.
  bool addUnknownInst(llvm::Instruction *I) {
    assert(I && "Instruction must not be null!");
    return mUnknownInsts.insert(I).second;
  }

private:
  LocationSet mDefs;
  LocationSet mMayDefs;
  LocationSet mUses;
  llvm::AliasSetTracker mExplicitAccesses;
  PointerSet mAddressAccesses;
  InstructionSet mUnknownInsts;
};

/// This attribute is associated with DefUseSet and
/// can be added to a node in a data-flow graph.
BASE_ATTR_DEF(DefUseAttr, DefUseSet)

/// \brief Data-flow framework which is used to find must defined locations
/// for each natural loops.
///
/// The data-flow problem is solved in forward direction.
/// The analysis is performed for loop bodies only.
///
/// Two kinds of attributes for each nodes in a data-flow graph are available
/// after this analysis. The first kind, is DefUseAttr and the second one is
/// PrivateDFAttr.
/// \attention Note that analysis which is performed for base locations is not
/// the same as analysis which is performed for variables form a source code.
/// For example, the base location for (short&)X is a memory location with
/// a size equal to size_of(short) regardless the size of X which might have
/// type int. Be careful when results of this analysis is propagated for
/// variables from a source code.
/// for (...) { (short&X) = ... ;} ... = X;
/// The short part of X will be recognized as last private, but the whole
/// variable X must be also set to first private to preserve the value
/// obtained before the loop.
class PrivateDFFwk : private bcl::Uncopyable {
public:
  /// Creates data-flow framework.
  explicit PrivateDFFwk(llvm::AliasSetTracker *AST) :
    mAliasTracker(AST) {
    assert(mAliasTracker && "AliasSetTracker must not be null!");
  }

  /// Returns a tracker for sets of aliases.
  llvm::AliasSetTracker * getTracker() const { return mAliasTracker; }

  /// Collapses a data-flow graph which represents a region to a one node
  /// in a data-flow graph of an outer region.
  void collapse(DFRegion *R);

private:
  llvm::AliasSetTracker *mAliasTracker;
};

/// This presents information whether a location has definition after a node
/// in a data-flow graph.
struct DefinitionInfo {
  LocationDFValue MustReach;
  LocationDFValue MayReach;
};

/// This covers IN and OUT value for a privatizability analysis.
typedef DFValue<PrivateDFFwk, DefinitionInfo> PrivateDFValue;

/// This attribute is associated with PrivateDFValue and
/// can be added to a node in a data-flow graph.
BASE_ATTR_DEF(PrivateDFAttr, PrivateDFValue)

/// Traits for a data-flow framework which is used to find candidates
/// in privatizable locations for each natural loops.
template<> struct DataFlowTraits<PrivateDFFwk *> {
  typedef Forward<DFRegion * > GraphType;
  typedef DefinitionInfo ValueType;
  static ValueType topElement(PrivateDFFwk *, GraphType) {
    DefinitionInfo DI;
    DI.MustReach = std::move(LocationDFValue::fullValue());
    DI.MayReach = std::move(LocationDFValue::emptyValue());
    return std::move(DI);
  }
  static ValueType boundaryCondition(PrivateDFFwk *, GraphType) {
    DefinitionInfo DI;
    DI.MustReach = std::move(LocationDFValue::emptyValue());
    DI.MayReach = std::move(LocationDFValue::emptyValue());
    return std::move(DI);
  }
  static void setValue(ValueType V, DFNode *N, PrivateDFFwk *) {
    assert(N && "Node must not be null!");
    PrivateDFValue *PV = N->getAttribute<PrivateDFAttr>();
    assert(PV && "Data-flow value must not be null!");
    PV->setOut(std::move(V));
  }
  static const ValueType & getValue(DFNode *N, PrivateDFFwk *) {
    assert(N && "Node must not be null!");
    PrivateDFValue *PV = N->getAttribute<PrivateDFAttr>();
    assert(PV && "Data-flow value must not be null!");
    return PV->getOut();
  }
  static void initialize(DFNode *, PrivateDFFwk *, GraphType);
  static void meetOperator(
    const ValueType &LHS, ValueType &RHS, PrivateDFFwk *, GraphType) {
    RHS.MustReach.intersect(LHS.MustReach);
    RHS.MayReach.merge(LHS.MayReach);
  }
  static bool transferFunction(ValueType, DFNode *, PrivateDFFwk *, GraphType);
};

/// Traits for a data-flow framework which is used to find candidates
/// in privatizable locations for each natural loops.
template<> struct RegionDFTraits<PrivateDFFwk *> :
  DataFlowTraits<PrivateDFFwk *> {
  static void expand(PrivateDFFwk *, GraphType) {}
  static void collapse(PrivateDFFwk *Fwk, GraphType G) {
    Fwk->collapse(G.Graph);
  }
  typedef DFRegion::region_iterator region_iterator;
  static region_iterator region_begin(GraphType G) {
    return G.Graph->region_begin();
  }
  static region_iterator region_end(GraphType G) {
    return G.Graph->region_end();
  }
};
}

namespace llvm {
class DefinedMemoryPass : public FunctionPass, private bcl::Uncopyable {
  typedef llvm::DenseMap<tsar::DFNode *, tsar::DefUseSet *> NodeToDUMap;
public:
  /// Pass identification, replacement for typeid.
  static char ID;

  /// Default constructor.
  DefinedMemoryPass() : FunctionPass(ID) {
    initializeDefinedMemoryPassPass(*PassRegistry::getPassRegistry());
  }

  tsar::DefUseSet & getDefUseFor(tsar::DFNode *N) {
    assert(N && "Node must not be null!");
    auto DU = mNodeToDU.find(N);
    assert(DU != mNodeToDU.end() && DU->second &&
      "Def-use set must be specified!");
    return *DU->second;
  }

  bool runOnFunction(Function &F) override;

  /// Specifies a list of analyzes  that are necessary for this pass.
  void getAnalysisUsage(AnalysisUsage &AU) const override;

  /// Releases memory.
  void releaseMemory() override {
    for (auto &Pair : mNodeToDU)
      delete Pair.second;
    mNodeToDU.clear();
  }

private:
  NodeToDUMap mNodeToDU;
};
}
#endif//TSAR_DEFINED_MEMORY_H