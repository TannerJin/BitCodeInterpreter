//===- lib/MC/MCContext.cpp - Machine Code Context ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCContext.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/BinaryFormat/XCOFF.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCCodeView.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFragment.h"
#include "llvm/MC/MCLabel.h"
#include "llvm/MC/MCObjectFileInfo.h"
//#include "llvm/MC/MCSectionCOFF.h"
//#include "llvm/MC/MCSectionELF.h"
#include "llvm/MC/MCSectionMachO.h"
//#include "llvm/MC/MCSectionWasm.h"
//#include "llvm/MC/MCSectionXCOFF.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
//#include "llvm/MC/MCSymbolCOFF.h"
//#include "llvm/MC/MCSymbolELF.h"
#include "llvm/MC/MCSymbolMachO.h"
//#include "llvm/MC/MCSymbolWasm.h"
//#include "llvm/MC/MCSymbolXCOFF.h"
#include "llvm/MC/SectionKind.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdlib>
#include <tuple>
#include <utility>

using namespace llvm;

static cl::opt<char*>
AsSecureLogFileName("as-secure-log-file-name",
        cl::desc("As secure log file name (initialized from "
                 "AS_SECURE_LOG_FILE env variable)"),
        cl::init(getenv("AS_SECURE_LOG_FILE")), cl::Hidden);

MCContext::MCContext(const MCAsmInfo *mai, const MCRegisterInfo *mri,
                     const MCObjectFileInfo *mofi, const SourceMgr *mgr,
                     MCTargetOptions const *TargetOpts, bool DoAutoReset)
    : SrcMgr(mgr), InlineSrcMgr(nullptr), MAI(mai), MRI(mri), MOFI(mofi),
      Symbols(Allocator), UsedNames(Allocator),
      InlineAsmUsedLabelNames(Allocator),
      CurrentDwarfLoc(0, 0, 0, DWARF2_FLAG_IS_STMT, 0, 0),
      AutoReset(DoAutoReset), TargetOptions(TargetOpts) {
  SecureLogFile = AsSecureLogFileName;

  if (SrcMgr && SrcMgr->getNumBuffers())
    MainFileName = std::string(SrcMgr->getMemoryBuffer(SrcMgr->getMainFileID())
                                   ->getBufferIdentifier());
}

MCContext::~MCContext() {
  if (AutoReset)
    reset();

  // NOTE: The symbols are all allocated out of a bump pointer allocator,
  // we don't need to free them here.
}

//===----------------------------------------------------------------------===//
// Module Lifetime Management
//===----------------------------------------------------------------------===//

void MCContext::reset() {
  // Call the destructors so the fragments are freed
//  COFFAllocator.DestroyAll();
//  ELFAllocator.DestroyAll();
  MachOAllocator.DestroyAll();
//  XCOFFAllocator.DestroyAll();
  MCInstAllocator.DestroyAll();

  MCSubtargetAllocator.DestroyAll();
  InlineAsmUsedLabelNames.clear();
  UsedNames.clear();
  Symbols.clear();
  Allocator.Reset();
  Instances.clear();
  CompilationDir.clear();
  MainFileName.clear();
  MCDwarfLineTablesCUMap.clear();
  SectionsForRanges.clear();
  MCGenDwarfLabelEntries.clear();
  DwarfDebugFlags = StringRef();
  DwarfCompileUnitID = 0;
  CurrentDwarfLoc = MCDwarfLoc(0, 0, 0, DWARF2_FLAG_IS_STMT, 0, 0);

  CVContext.reset();

  MachOUniquingMap.clear();
//  ELFUniquingMap.clear();
//  COFFUniquingMap.clear();
//  WasmUniquingMap.clear();
//  XCOFFUniquingMap.clear();

  ELFEntrySizeMap.clear();
  ELFSeenGenericMergeableSections.clear();

  NextID.clear();
  AllowTemporaryLabels = true;
  DwarfLocSeen = false;
  GenDwarfForAssembly = false;
  GenDwarfFileNumber = 0;

  HadError = false;
}

//===----------------------------------------------------------------------===//
// MCInst Management
//===----------------------------------------------------------------------===//

MCInst *MCContext::createMCInst() {
  return new (MCInstAllocator.Allocate()) MCInst;
}

//===----------------------------------------------------------------------===//
// Symbol Manipulation
//===----------------------------------------------------------------------===//

MCSymbol *MCContext::getOrCreateSymbol(const Twine &Name) {
  SmallString<128> NameSV;
  StringRef NameRef = Name.toStringRef(NameSV);

  assert(!NameRef.empty() && "Normal symbols cannot be unnamed!");

  MCSymbol *&Sym = Symbols[NameRef];
  if (!Sym)
    Sym = createSymbol(NameRef, false, false);

  return Sym;
}

MCSymbol *MCContext::getOrCreateFrameAllocSymbol(StringRef FuncName,
                                                 unsigned Idx) {
  return getOrCreateSymbol(Twine(MAI->getPrivateGlobalPrefix()) + FuncName +
                           "$frame_escape_" + Twine(Idx));
}

MCSymbol *MCContext::getOrCreateParentFrameOffsetSymbol(StringRef FuncName) {
  return getOrCreateSymbol(Twine(MAI->getPrivateGlobalPrefix()) + FuncName +
                           "$parent_frame_offset");
}

MCSymbol *MCContext::getOrCreateLSDASymbol(StringRef FuncName) {
  return getOrCreateSymbol(Twine(MAI->getPrivateGlobalPrefix()) + "__ehtable$" +
                           FuncName);
}

MCSymbol *MCContext::createSymbolImpl(const StringMapEntry<bool> *Name,
                                      bool IsTemporary) {
//  static_assert(std::is_trivially_destructible<MCSymbolCOFF>(),
//                "MCSymbol classes must be trivially destructible");
//  static_assert(std::is_trivially_destructible<MCSymbolELF>(),
//                "MCSymbol classes must be trivially destructible");
  static_assert(std::is_trivially_destructible<MCSymbolMachO>(),
                "MCSymbol classes must be trivially destructible");
//  static_assert(std::is_trivially_destructible<MCSymbolWasm>(),
//                "MCSymbol classes must be trivially destructible");
//  static_assert(std::is_trivially_destructible<MCSymbolXCOFF>(),
//                "MCSymbol classes must be trivially destructible");
  if (MOFI) {
    switch (MOFI->getObjectFileType()) {
//    case MCObjectFileInfo::IsCOFF:
//      return new (Name, *this) MCSymbolCOFF(Name, IsTemporary);
//    case MCObjectFileInfo::IsELF:
//      return new (Name, *this) MCSymbolELF(Name, IsTemporary);
    case MCObjectFileInfo::IsMachO:
      return new (Name, *this) MCSymbolMachO(Name, IsTemporary);
//    case MCObjectFileInfo::IsWasm:
//      return new (Name, *this) MCSymbolWasm(Name, IsTemporary);
//    case MCObjectFileInfo::IsXCOFF:
//      return createXCOFFSymbolImpl(Name, IsTemporary);
    }
  }
  return new (Name, *this) MCSymbol(MCSymbol::SymbolKindUnset, Name,
                                    IsTemporary);
}

MCSymbol *MCContext::createSymbol(StringRef Name, bool AlwaysAddSuffix,
                                  bool CanBeUnnamed) {
  if (CanBeUnnamed && !UseNamesOnTempLabels)
    return createSymbolImpl(nullptr, true);

  // Determine whether this is a user written assembler temporary or normal
  // label, if used.
  bool IsTemporary = CanBeUnnamed;
  if (AllowTemporaryLabels && !IsTemporary)
    IsTemporary = Name.startswith(MAI->getPrivateGlobalPrefix());

  SmallString<128> NewName = Name;
  bool AddSuffix = AlwaysAddSuffix;
  unsigned &NextUniqueID = NextID[Name];
  while (true) {
    if (AddSuffix) {
      NewName.resize(Name.size());
      raw_svector_ostream(NewName) << NextUniqueID++;
    }
    auto NameEntry = UsedNames.insert(std::make_pair(NewName, true));
    if (NameEntry.second || !NameEntry.first->second) {
      // Ok, we found a name.
      // Mark it as used for a non-section symbol.
      NameEntry.first->second = true;
      // Have the MCSymbol object itself refer to the copy of the string that is
      // embedded in the UsedNames entry.
      return createSymbolImpl(&*NameEntry.first, IsTemporary);
    }
    assert(IsTemporary && "Cannot rename non-temporary symbols");
    AddSuffix = true;
  }
  llvm_unreachable("Infinite loop");
}

MCSymbol *MCContext::createTempSymbol(const Twine &Name, bool AlwaysAddSuffix) {
  SmallString<128> NameSV;
  raw_svector_ostream(NameSV) << MAI->getPrivateGlobalPrefix() << Name;
  return createSymbol(NameSV, AlwaysAddSuffix, true);
}

MCSymbol *MCContext::createNamedTempSymbol(const Twine &Name) {
  SmallString<128> NameSV;
  raw_svector_ostream(NameSV) << MAI->getPrivateGlobalPrefix() << Name;
  return createSymbol(NameSV, true, false);
}

MCSymbol *MCContext::createLinkerPrivateTempSymbol() {
  SmallString<128> NameSV;
  raw_svector_ostream(NameSV) << MAI->getLinkerPrivateGlobalPrefix() << "tmp";
  return createSymbol(NameSV, true, false);
}

MCSymbol *MCContext::createTempSymbol() { return createTempSymbol("tmp"); }

MCSymbol *MCContext::createNamedTempSymbol() {
  return createNamedTempSymbol("tmp");
}

unsigned MCContext::NextInstance(unsigned LocalLabelVal) {
  MCLabel *&Label = Instances[LocalLabelVal];
  if (!Label)
    Label = new (*this) MCLabel(0);
  return Label->incInstance();
}

unsigned MCContext::GetInstance(unsigned LocalLabelVal) {
  MCLabel *&Label = Instances[LocalLabelVal];
  if (!Label)
    Label = new (*this) MCLabel(0);
  return Label->getInstance();
}

MCSymbol *MCContext::getOrCreateDirectionalLocalSymbol(unsigned LocalLabelVal,
                                                       unsigned Instance) {
  MCSymbol *&Sym = LocalSymbols[std::make_pair(LocalLabelVal, Instance)];
  if (!Sym)
    Sym = createNamedTempSymbol();
  return Sym;
}

MCSymbol *MCContext::createDirectionalLocalSymbol(unsigned LocalLabelVal) {
  unsigned Instance = NextInstance(LocalLabelVal);
  return getOrCreateDirectionalLocalSymbol(LocalLabelVal, Instance);
}

MCSymbol *MCContext::getDirectionalLocalSymbol(unsigned LocalLabelVal,
                                               bool Before) {
  unsigned Instance = GetInstance(LocalLabelVal);
  if (!Before)
    ++Instance;
  return getOrCreateDirectionalLocalSymbol(LocalLabelVal, Instance);
}

MCSymbol *MCContext::lookupSymbol(const Twine &Name) const {
  SmallString<128> NameSV;
  StringRef NameRef = Name.toStringRef(NameSV);
  return Symbols.lookup(NameRef);
}

void MCContext::setSymbolValue(MCStreamer &Streamer,
                              StringRef Sym,
                              uint64_t Val) {
  auto Symbol = getOrCreateSymbol(Sym);
  Streamer.emitAssignment(Symbol, MCConstantExpr::create(Val, *this));
}

void MCContext::registerInlineAsmLabel(MCSymbol *Sym) {
  InlineAsmUsedLabelNames[Sym->getName()] = Sym;
}


//===----------------------------------------------------------------------===//
// Section Management
//===----------------------------------------------------------------------===//

MCSectionMachO *MCContext::getMachOSection(StringRef Segment, StringRef Section,
                                           unsigned TypeAndAttributes,
                                           unsigned Reserved2, SectionKind Kind,
                                           const char *BeginSymName) {
  // We unique sections by their segment/section pair.  The returned section
  // may not have the same flags as the requested section, if so this should be
  // diagnosed by the client as an error.

  // Form the name to look up.
  assert(Section.size() <= 16 && "section name is too long");
  assert(!memchr(Section.data(), '\0', Section.size()) &&
         "section name cannot contain NUL");

  // Do the lookup, if we have a hit, return it.
  auto R = MachOUniquingMap.try_emplace((Segment + Twine(',') + Section).str());
  if (!R.second)
    return R.first->second;

  MCSymbol *Begin = nullptr;
  if (BeginSymName)
    Begin = createTempSymbol(BeginSymName, false);

  // Otherwise, return a new section.
  StringRef Name = R.first->first();
  R.first->second = new (MachOAllocator.Allocate())
      MCSectionMachO(Segment, Name.substr(Name.size() - Section.size()),
                     TypeAndAttributes, Reserved2, Kind, Begin);
  return R.first->second;
}


MCSubtargetInfo &MCContext::getSubtargetCopy(const MCSubtargetInfo &STI) {
  return *new (MCSubtargetAllocator.Allocate()) MCSubtargetInfo(STI);
}

void MCContext::addDebugPrefixMapEntry(const std::string &From,
                                       const std::string &To) {
  DebugPrefixMap.insert(std::make_pair(From, To));
}

void MCContext::RemapDebugPaths() {
  const auto &DebugPrefixMap = this->DebugPrefixMap;
  if (DebugPrefixMap.empty())
    return;

  const auto RemapDebugPath = [&DebugPrefixMap](std::string &Path) {
    SmallString<256> P(Path);
    for (const auto &Entry : DebugPrefixMap) {
      if (llvm::sys::path::replace_path_prefix(P, Entry.first, Entry.second)) {
        Path = P.str().str();
        break;
      }
    }
  };

  // Remap compilation directory.
  std::string CompDir = std::string(CompilationDir.str());
  RemapDebugPath(CompDir);
  CompilationDir = CompDir;

  // Remap MCDwarfDirs in all compilation units.
  for (auto &CUIDTablePair : MCDwarfLineTablesCUMap)
    for (auto &Dir : CUIDTablePair.second.getMCDwarfDirs())
      RemapDebugPath(Dir);
}

//===----------------------------------------------------------------------===//
// Dwarf Management
//===----------------------------------------------------------------------===//

void MCContext::setGenDwarfRootFile(StringRef InputFileName, StringRef Buffer) {
  // MCDwarf needs the root file as well as the compilation directory.
  // If we find a '.file 0' directive that will supersede these values.
  Optional<MD5::MD5Result> Cksum;
  if (getDwarfVersion() >= 5) {
    MD5 Hash;
    MD5::MD5Result Sum;
    Hash.update(Buffer);
    Hash.final(Sum);
    Cksum = Sum;
  }
  // Canonicalize the root filename. It cannot be empty, and should not
  // repeat the compilation dir.
  // The MCContext ctor initializes MainFileName to the name associated with
  // the SrcMgr's main file ID, which might be the same as InputFileName (and
  // possibly include directory components).
  // Or, MainFileName might have been overridden by a -main-file-name option,
  // which is supposed to be just a base filename with no directory component.
  // So, if the InputFileName and MainFileName are not equal, assume
  // MainFileName is a substitute basename and replace the last component.
  SmallString<1024> FileNameBuf = InputFileName;
  if (FileNameBuf.empty() || FileNameBuf == "-")
    FileNameBuf = "<stdin>";
  if (!getMainFileName().empty() && FileNameBuf != getMainFileName()) {
    llvm::sys::path::remove_filename(FileNameBuf);
    llvm::sys::path::append(FileNameBuf, getMainFileName());
  }
  StringRef FileName = FileNameBuf;
  if (FileName.consume_front(getCompilationDir()))
    if (llvm::sys::path::is_separator(FileName.front()))
      FileName = FileName.drop_front();
  assert(!FileName.empty());
  setMCLineTableRootFile(
      /*CUID=*/0, getCompilationDir(), FileName, Cksum, None);
}

/// getDwarfFile - takes a file name and number to place in the dwarf file and
/// directory tables.  If the file number has already been allocated it is an
/// error and zero is returned and the client reports the error, else the
/// allocated file number is returned.  The file numbers may be in any order.
Expected<unsigned> MCContext::getDwarfFile(StringRef Directory,
                                           StringRef FileName,
                                           unsigned FileNumber,
                                           Optional<MD5::MD5Result> Checksum,
                                           Optional<StringRef> Source,
                                           unsigned CUID) {
  MCDwarfLineTable &Table = MCDwarfLineTablesCUMap[CUID];
  return Table.tryGetFile(Directory, FileName, Checksum, Source, DwarfVersion,
                          FileNumber);
}

/// isValidDwarfFileNumber - takes a dwarf file number and returns true if it
/// currently is assigned and false otherwise.
bool MCContext::isValidDwarfFileNumber(unsigned FileNumber, unsigned CUID) {
  const MCDwarfLineTable &LineTable = getMCDwarfLineTable(CUID);
  if (FileNumber == 0)
    return getDwarfVersion() >= 5;
  if (FileNumber >= LineTable.getMCDwarfFiles().size())
    return false;

  return !LineTable.getMCDwarfFiles()[FileNumber].Name.empty();
}

/// Remove empty sections from SectionsForRanges, to avoid generating
/// useless debug info for them.
void MCContext::finalizeDwarfSections(MCStreamer &MCOS) {
  SectionsForRanges.remove_if(
      [&](MCSection *Sec) { return !MCOS.mayHaveInstructions(*Sec); });
}

CodeViewContext &MCContext::getCVContext() {
  if (!CVContext.get())
    CVContext.reset(new CodeViewContext);
  return *CVContext.get();
}

//===----------------------------------------------------------------------===//
// Error Reporting
//===----------------------------------------------------------------------===//

void MCContext::reportError(SMLoc Loc, const Twine &Msg) {
  HadError = true;

  // If we have a source manager use it. Otherwise, try using the inline source
  // manager.
  // If that fails, construct a temporary SourceMgr.
  if (SrcMgr)
    SrcMgr->PrintMessage(Loc, SourceMgr::DK_Error, Msg);
  else if (InlineSrcMgr)
    InlineSrcMgr->PrintMessage(Loc, SourceMgr::DK_Error, Msg);
  else
    SourceMgr().PrintMessage(Loc, SourceMgr::DK_Error, Msg);
}

void MCContext::reportWarning(SMLoc Loc, const Twine &Msg) {
  if (TargetOptions && TargetOptions->MCNoWarn)
    return;
  if (TargetOptions && TargetOptions->MCFatalWarnings)
    reportError(Loc, Msg);
  else {
    // If we have a source manager use it. Otherwise, try using the inline
    // source manager.
    if (SrcMgr)
      SrcMgr->PrintMessage(Loc, SourceMgr::DK_Warning, Msg);
    else if (InlineSrcMgr)
      InlineSrcMgr->PrintMessage(Loc, SourceMgr::DK_Warning, Msg);
  }
}

void MCContext::reportFatalError(SMLoc Loc, const Twine &Msg) {
  reportError(Loc, Msg);

  // If we reached here, we are failing ungracefully. Run the interrupt handlers
  // to make sure any special cleanups get done, in particular that we remove
  // files registered with RemoveFileOnSignal.
  sys::RunInterruptHandlers();
  exit(1);
}
