//===-- LLVMModuleObfuscation.cpp - LTO Module obfuscatior ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file provides helper classes to perform string obfuscation
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/ADT/Triple.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/TypeFinder.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/Obfuscation.h"
#include "llvm/Support/Options.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/Utils/ModuleObfuscator.h"
#include "llvm/Pass.h"

#include <string>

using namespace llvm;

// StringSet holds all the special symbols.
static ManagedStatic<llvm::StringSet<>> SpecialSymbolSet;

// Options that have extra controls over the pass
static cl::opt<std::string>
    SymbolFileOut("obfuscation-symbol-map",
                  cl::ValueRequired,
                  cl::desc("Specify the symbol_map output"),
                  cl::value_desc("filename.bcsymbolmap"));
static cl::list<std::string> PreserveSymbols("obfuscate-preserve",
                                            cl::ZeroOrMore, cl::ValueRequired,
                                            cl::desc("<sym name>"));

namespace {
/// \brief This pass obfuscate the strings inside the module.
struct ObfuscateModule : public ModulePass {
  static char ID; // Pass identification, replacement for typeid
  ObfuscateModule() : ModulePass(ID) {
    initializeObfuscateModulePass(*PassRegistry::getPassRegistry());
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }

  bool runOnModule(Module &M) override {
    return obfuscateModule(M);
  }
};

/// \brief Helper class to recursively gather and modify all MDStrings from
/// given entry points
///
/// Will only ever visit a given MDNode once. User can feed in MDNodes with
/// "addEntryPoint". User can then incrementally, at any time call "run" to
/// start recursively modifying reachable MDStrings from un-visited entry
/// points.
class MDSRecursiveModify {
  SmallVector<MDNode *, 16> Worklist;
  DenseSet<const MDNode *> Visited;

  LLVMContext &Ctx;
  std::function<StringRef(StringRef)> F;

public:
  MDSRecursiveModify(LLVMContext &Context,
                     std::function<StringRef(StringRef)> NewStr)
      : Ctx(Context), F(NewStr) {}

  /// \brief Add a new entry point into the metadata graph to traverse from
  void addEntryPoint(MDNode *N) {
    if (!N || !Visited.count(N))
      Worklist.push_back(N);
  }
  void addEntryPoint(NamedMDNode *NMD) {
    for (auto I : NMD->operands())
      addEntryPoint(I);
  }
  void addEntryPoint(DbgInfoIntrinsic *DII) {
    if (auto DDI = dyn_cast<DbgDeclareInst>(DII))
      addEntryPoint(DDI->getVariable());
    else if (auto DVI = dyn_cast<DbgValueInst>(DII))
      addEntryPoint(DVI->getVariable());
    else
      llvm_unreachable("invalid debug info intrinsic");
  }

  /// \brief Recursively modify reachable, unvisited MDStrings
  inline void run();
};
} // End llvm namespace

void MDSRecursiveModify::run() {
  while (!Worklist.empty()) {
    auto N = Worklist.pop_back_val();
    if (!N || Visited.count(N))
      continue;
    Visited.insert(N);
    for (unsigned i = 0; i < N->getNumOperands(); ++i) {
      Metadata *MD = N->getOperand(i);
      if (!MD)
        continue;
      if (auto MDS = dyn_cast<MDString>(MD)) {
        assert(N->isResolved() && "Unexpected forward reference");
        N->replaceOperandWith(i, MDString::get(Ctx, F(MDS->getString())));
      } else if (auto NN = dyn_cast<MDNode>(MD))
        Worklist.push_back(NN);
    }
  }
}

static bool obfuscateModuleModify(Module &M, Obfuscator &obfs,
                                  std::function<bool(Value &)> preserve) {
  // Obfuscate value names
  {
    typedef std::pair<GlobalValue*, SmallString<64>> WorkEntry;
    SmallVector<WorkEntry, 64> Worklist;
    auto hasName = [](const Value &V) { return V.getName() != ""; };
    for (auto &F : M) {
      if (hasName(F) && !preserve(F))
        Worklist.push_back({&F, F.getName()});
      // While we're here, drop parameter names if they exist
      for (auto &Arg : F.args())
        Arg.setName("");
    }
    for (auto &I : M.globals())
      if (hasName(I) && !preserve(I))
        Worklist.push_back({&I, I.getName()});
    for (auto &I : M.aliases())
      if (hasName(I) && !preserve(I))
        Worklist.push_back({&I, I.getName()});
    // Obfuscate all the symbols in the list.
    // Run two passes across the worklist, first time rename to a temp name
    // to avoid conflicts.
    for (auto &I : Worklist)
      // Just set to something but don't really care what it becames.
      I.first->setName("__obfs_tmp#");
    for (auto &I : Worklist)
      I.first->setName(obfs.obfuscate(I.second, true));
  }

  // Drop type names
  TypeFinder TF;
  TF.run(M, true);
  for (auto I : TF)
    // typename is not in the symbol table so temporary conflict is ok.
    I->setName(obfs.obfuscate(I->getName()));

  // Obfuscate debug info strings
  MDSRecursiveModify Modify(M.getContext(),
                            [&obfs](StringRef S) {
                              return obfs.obfuscate(S, true);
                            });

  // llvm.dbg.cu and llvm.gcov  needs to be obfuscated
  if (auto DBG = M.getNamedMetadata("llvm.dbg.cu"))
    Modify.addEntryPoint(DBG);
  if (auto GCOV = M.getNamedMetadata("llvm.gcov"))
    Modify.addEntryPoint(GCOV);

  Modify.run();

  // llvm.dbg.value/declare's "variable" argument needs to be obfuscated
  auto addDbgIntrinsicCalls = [&](StringRef Name) {
    auto F = M.getFunction(Name);
    if (!F)
      return;
    for (auto Usr : F->users())
      Modify.addEntryPoint(cast<DbgInfoIntrinsic>(Usr));
  };
  addDbgIntrinsicCalls("llvm.dbg.declare");
  addDbgIntrinsicCalls("llvm.dbg.value");
  Modify.run();

  // Every insruction's DebugLoc needs to be obfuscated
  for (auto &F : M.functions())
    for (auto &BB : F)
      for (auto &I : BB) {
        // While we're here, go ahead and drop its name if it has one
        I.setName("");
        Modify.addEntryPoint(I.getDebugLoc().getAsMDNode());
      }
  Modify.run();

  // We only rename stuff, never really change IR.
  return false;
}

bool llvm::obfuscateModule(Module &M) {
  IncrementObfuscator IncrObfuscate;
  StringMap<detail::DenseSetEmpty, BumpPtrAllocator &> Preserves(
      IncrObfuscate.getAllocator());
  // Add PreserveSymbols from commandline.
  for (auto & sym : PreserveSymbols)
    Preserves.insert({sym, {}});
  bool Changed = obfuscateModule(M, IncrObfuscate, Preserves);
  // Output SymbolFile if needed.
  if (!SymbolFileOut.empty()) {
    // Create output file
    std::error_code EC;
    raw_fd_ostream Out(StringRef(SymbolFileOut), EC, sys::fs::F_None);
    if (EC) {
      // Just return.
      llvm::errs() << "could not open obfuscation symbol map: " <<
        SymbolFileOut << "\n";
      return Changed;
    }

    // Write reverse map
    IncrObfuscate.writeReverseMap(Out);
    Out.close();
  }
  return Changed;
}

bool llvm::obfuscateModule(Module &M, Obfuscator &obfs,
                           SymbolSet &PreserveSymbols) {
  // Predicate to indicate if we should preserve the original name.
  TargetLibraryInfoImpl TLII(Triple(M.getTargetTriple()));
  TargetLibraryInfo TLI(TLII);
  auto isLibName = [&TLI](StringRef S) {
    LibFunc::Func F;
    return TLI.getLibFunc(S, F);
  };

  // Handle llvm special symbols.
  static const char* const SpecialSymbols[] = {
#define COMPILER_SYMBOL(Name) #Name,
#include "llvm/Transforms/Utils/CompilerRTSymbols.def"
#undef COMPILER_SYMBOL
      "objc_retain",
      "objc_release",
      "objc_autorelease",
      "objc_retainAutoreleasedReturnValue",
      "objc_retainBlock",
      "objc_autoreleaseReturnValue",
      "objc_autoreleasePoolPush",
      "objc_loadWeakRetained",
      "objc_loadWeak",
      "objc_destroyWeak",
      "objc_storeWeak",
      "objc_initWeak",
      "objc_moveWeak",
      "objc_copyWeak",
      "objc_retainedObject",
      "objc_unretainedObject",
      "objc_unretainedPointer"
  };
  static const unsigned NumSpecialSymbols = sizeof(SpecialSymbols) /
                                            sizeof(const char *);
  // Create and insert the symbols into StringSet.
  if (!SpecialSymbolSet.isConstructed()) {
    for (unsigned i = 0; i < NumSpecialSymbols; ++i)
      SpecialSymbolSet->insert(StringRef(SpecialSymbols[i]));
  }
  // Take the address of the StringSet so it can be captured.
  auto SymbolSet = &*SpecialSymbolSet;
  // Whether this is a special symbol that the compiler recognizes. This can
  // either be a compiler-internal symbol, or an external symbol that the
  // compiler special cases. Eitherway, checks based on name
  auto isSpecialSymbolName = [SymbolSet](StringRef Name) {
    if (Name.startswith("llvm.") || Name.startswith("__stack_chk") ||
        Name.startswith("clang.arc"))
      return true;

    // Special symbols supplied by ld64.
    if (Name.startswith("__dtrace") ||
        Name.startswith("\01section$start$") ||
        Name.startswith("\01section$end$") ||
        Name.startswith("\01segment$start$") ||
        Name.startswith("\01segment$end$"))
      return true;

    // Some special external names, which might of gotten dropped due to
    // optimizations
    if (SymbolSet->count(Name))
      return true;

    return false;
  };

  auto mustPreserve =
                [&PreserveSymbols, isLibName, isSpecialSymbolName](Value &V) {
    auto Name = V.getName();
    return PreserveSymbols.count(Name) || isLibName(Name) ||
           isSpecialSymbolName(Name);
  };

  return obfuscateModuleModify(M, obfs, mustPreserve);
}

char ObfuscateModule::ID = 0;
INITIALIZE_PASS(ObfuscateModule, "obfuscate-module",
                "Obfuscate all strings in the module", false, false)

ModulePass *llvm::createObfuscateModulePass() {
  return new ObfuscateModule();
}
