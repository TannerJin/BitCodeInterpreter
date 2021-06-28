//===-- MCObjectFileInfo.cpp - Object File Information --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Triple.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCSection.h"
//#include "llvm/MC/MCSectionCOFF.h"
//#include "llvm/MC/MCSectionELF.h"
#include "llvm/MC/MCSectionMachO.h"
//#include "llvm/MC/MCSectionWasm.h"
//#include "llvm/MC/MCSectionXCOFF.h"

using namespace llvm;

static bool useCompactUnwind(const Triple &T) {
  // Only on darwin.
  if (!T.isOSDarwin())
    return false;

  // aarch64 always has it.
  if (T.getArch() == Triple::aarch64 || T.getArch() == Triple::aarch64_32)
    return true;

  // armv7k always has it.
  if (T.isWatchABI())
    return true;

  // Use it on newer version of OS X.
  if (T.isMacOSX() && !T.isMacOSXVersionLT(10, 6))
    return true;

  // And the iOS simulator.
  if (T.isiOS() && T.isX86())
    return true;

  return false;
}

void MCObjectFileInfo::initMachOMCObjectFileInfo(const Triple &T) {
  // MachO
  SupportsWeakOmittedEHFrame = false;

  EHFrameSection = Ctx->getMachOSection(
      "__TEXT", "__eh_frame",
      MachO::S_COALESCED | MachO::S_ATTR_NO_TOC |
          MachO::S_ATTR_STRIP_STATIC_SYMS | MachO::S_ATTR_LIVE_SUPPORT,
      SectionKind::getReadOnly());

  if (T.isOSDarwin() &&
      (T.getArch() == Triple::aarch64 || T.getArch() == Triple::aarch64_32))
    SupportsCompactUnwindWithoutEHFrame = true;

  if (T.isWatchABI())
    OmitDwarfIfHaveCompactUnwind = true;

  FDECFIEncoding = dwarf::DW_EH_PE_pcrel;

  // .comm doesn't support alignment before Leopard.
  if (T.isMacOSX() && T.isMacOSXVersionLT(10, 5))
    CommDirectiveSupportsAlignment = false;

  TextSection // .text
    = Ctx->getMachOSection("__TEXT", "__text",
                           MachO::S_ATTR_PURE_INSTRUCTIONS,
                           SectionKind::getText());
  DataSection // .data
      = Ctx->getMachOSection("__DATA", "__data", 0, SectionKind::getData());

  // BSSSection might not be expected initialized on msvc.
  BSSSection = nullptr;

  TLSDataSection // .tdata
      = Ctx->getMachOSection("__DATA", "__thread_data",
                             MachO::S_THREAD_LOCAL_REGULAR,
                             SectionKind::getData());
  TLSBSSSection // .tbss
    = Ctx->getMachOSection("__DATA", "__thread_bss",
                           MachO::S_THREAD_LOCAL_ZEROFILL,
                           SectionKind::getThreadBSS());

  // TODO: Verify datarel below.
  TLSTLVSection // .tlv
      = Ctx->getMachOSection("__DATA", "__thread_vars",
                             MachO::S_THREAD_LOCAL_VARIABLES,
                             SectionKind::getData());

  TLSThreadInitSection = Ctx->getMachOSection(
      "__DATA", "__thread_init", MachO::S_THREAD_LOCAL_INIT_FUNCTION_POINTERS,
      SectionKind::getData());

  CStringSection // .cstring
    = Ctx->getMachOSection("__TEXT", "__cstring",
                           MachO::S_CSTRING_LITERALS,
                           SectionKind::getMergeable1ByteCString());
  UStringSection
    = Ctx->getMachOSection("__TEXT","__ustring", 0,
                           SectionKind::getMergeable2ByteCString());
  FourByteConstantSection // .literal4
    = Ctx->getMachOSection("__TEXT", "__literal4",
                           MachO::S_4BYTE_LITERALS,
                           SectionKind::getMergeableConst4());
  EightByteConstantSection // .literal8
    = Ctx->getMachOSection("__TEXT", "__literal8",
                           MachO::S_8BYTE_LITERALS,
                           SectionKind::getMergeableConst8());

  SixteenByteConstantSection // .literal16
      = Ctx->getMachOSection("__TEXT", "__literal16",
                             MachO::S_16BYTE_LITERALS,
                             SectionKind::getMergeableConst16());

  ReadOnlySection  // .const
    = Ctx->getMachOSection("__TEXT", "__const", 0,
                           SectionKind::getReadOnly());

  // If the target is not powerpc, map the coal sections to the non-coal
  // sections.
  //
  // "__TEXT/__textcoal_nt" => section "__TEXT/__text"
  // "__TEXT/__const_coal"  => section "__TEXT/__const"
  // "__DATA/__datacoal_nt" => section "__DATA/__data"
  Triple::ArchType ArchTy = T.getArch();

  ConstDataSection  // .const_data
    = Ctx->getMachOSection("__DATA", "__const", 0,
                           SectionKind::getReadOnlyWithRel());

  if (ArchTy == Triple::ppc || ArchTy == Triple::ppc64) {
    TextCoalSection
      = Ctx->getMachOSection("__TEXT", "__textcoal_nt",
                             MachO::S_COALESCED |
                             MachO::S_ATTR_PURE_INSTRUCTIONS,
                             SectionKind::getText());
    ConstTextCoalSection
      = Ctx->getMachOSection("__TEXT", "__const_coal",
                             MachO::S_COALESCED,
                             SectionKind::getReadOnly());
    DataCoalSection = Ctx->getMachOSection(
        "__DATA", "__datacoal_nt", MachO::S_COALESCED, SectionKind::getData());
    ConstDataCoalSection = DataCoalSection;
  } else {
    TextCoalSection = TextSection;
    ConstTextCoalSection = ReadOnlySection;
    DataCoalSection = DataSection;
    ConstDataCoalSection = ConstDataSection;
  }

  DataCommonSection
    = Ctx->getMachOSection("__DATA","__common",
                           MachO::S_ZEROFILL,
                           SectionKind::getBSS());
  DataBSSSection
    = Ctx->getMachOSection("__DATA","__bss", MachO::S_ZEROFILL,
                           SectionKind::getBSS());


  LazySymbolPointerSection
    = Ctx->getMachOSection("__DATA", "__la_symbol_ptr",
                           MachO::S_LAZY_SYMBOL_POINTERS,
                           SectionKind::getMetadata());
  NonLazySymbolPointerSection
    = Ctx->getMachOSection("__DATA", "__nl_symbol_ptr",
                           MachO::S_NON_LAZY_SYMBOL_POINTERS,
                           SectionKind::getMetadata());

  ThreadLocalPointerSection
    = Ctx->getMachOSection("__DATA", "__thread_ptr",
                           MachO::S_THREAD_LOCAL_VARIABLE_POINTERS,
                           SectionKind::getMetadata());

  // Exception Handling.
  LSDASection = Ctx->getMachOSection("__TEXT", "__gcc_except_tab", 0,
                                     SectionKind::getReadOnlyWithRel());

  COFFDebugSymbolsSection = nullptr;
  COFFDebugTypesSection = nullptr;
  COFFGlobalTypeHashesSection = nullptr;

  if (useCompactUnwind(T)) {
    CompactUnwindSection =
        Ctx->getMachOSection("__LD", "__compact_unwind", MachO::S_ATTR_DEBUG,
                             SectionKind::getReadOnly());

    if (T.isX86())
      CompactUnwindDwarfEHFrameOnly = 0x04000000;  // UNWIND_X86_64_MODE_DWARF
    else if (T.getArch() == Triple::aarch64 || T.getArch() == Triple::aarch64_32)
      CompactUnwindDwarfEHFrameOnly = 0x03000000;  // UNWIND_ARM64_MODE_DWARF
    else if (T.getArch() == Triple::arm || T.getArch() == Triple::thumb)
      CompactUnwindDwarfEHFrameOnly = 0x04000000;  // UNWIND_ARM_MODE_DWARF
  }

  // Debug Information.
  DwarfDebugNamesSection =
      Ctx->getMachOSection("__DWARF", "__debug_names", MachO::S_ATTR_DEBUG,
                           SectionKind::getMetadata(), "debug_names_begin");
  DwarfAccelNamesSection =
      Ctx->getMachOSection("__DWARF", "__apple_names", MachO::S_ATTR_DEBUG,
                           SectionKind::getMetadata(), "names_begin");
  DwarfAccelObjCSection =
      Ctx->getMachOSection("__DWARF", "__apple_objc", MachO::S_ATTR_DEBUG,
                           SectionKind::getMetadata(), "objc_begin");
  // 16 character section limit...
  DwarfAccelNamespaceSection =
      Ctx->getMachOSection("__DWARF", "__apple_namespac", MachO::S_ATTR_DEBUG,
                           SectionKind::getMetadata(), "namespac_begin");
  DwarfAccelTypesSection =
      Ctx->getMachOSection("__DWARF", "__apple_types", MachO::S_ATTR_DEBUG,
                           SectionKind::getMetadata(), "types_begin");

  DwarfSwiftASTSection =
      Ctx->getMachOSection("__DWARF", "__swift_ast", MachO::S_ATTR_DEBUG,
                           SectionKind::getMetadata());

  DwarfAbbrevSection =
      Ctx->getMachOSection("__DWARF", "__debug_abbrev", MachO::S_ATTR_DEBUG,
                           SectionKind::getMetadata(), "section_abbrev");
  DwarfInfoSection =
      Ctx->getMachOSection("__DWARF", "__debug_info", MachO::S_ATTR_DEBUG,
                           SectionKind::getMetadata(), "section_info");
  DwarfLineSection =
      Ctx->getMachOSection("__DWARF", "__debug_line", MachO::S_ATTR_DEBUG,
                           SectionKind::getMetadata(), "section_line");
  DwarfLineStrSection =
      Ctx->getMachOSection("__DWARF", "__debug_line_str", MachO::S_ATTR_DEBUG,
                           SectionKind::getMetadata(), "section_line_str");
  DwarfFrameSection =
      Ctx->getMachOSection("__DWARF", "__debug_frame", MachO::S_ATTR_DEBUG,
                           SectionKind::getMetadata());
  DwarfPubNamesSection =
      Ctx->getMachOSection("__DWARF", "__debug_pubnames", MachO::S_ATTR_DEBUG,
                           SectionKind::getMetadata());
  DwarfPubTypesSection =
      Ctx->getMachOSection("__DWARF", "__debug_pubtypes", MachO::S_ATTR_DEBUG,
                           SectionKind::getMetadata());
  DwarfGnuPubNamesSection =
      Ctx->getMachOSection("__DWARF", "__debug_gnu_pubn", MachO::S_ATTR_DEBUG,
                           SectionKind::getMetadata());
  DwarfGnuPubTypesSection =
      Ctx->getMachOSection("__DWARF", "__debug_gnu_pubt", MachO::S_ATTR_DEBUG,
                           SectionKind::getMetadata());
  DwarfStrSection =
      Ctx->getMachOSection("__DWARF", "__debug_str", MachO::S_ATTR_DEBUG,
                           SectionKind::getMetadata(), "info_string");
  DwarfStrOffSection =
      Ctx->getMachOSection("__DWARF", "__debug_str_offs", MachO::S_ATTR_DEBUG,
                           SectionKind::getMetadata(), "section_str_off");
  DwarfAddrSection =
      Ctx->getMachOSection("__DWARF", "__debug_addr", MachO::S_ATTR_DEBUG,
                           SectionKind::getMetadata(), "section_info");
  DwarfLocSection =
      Ctx->getMachOSection("__DWARF", "__debug_loc", MachO::S_ATTR_DEBUG,
                           SectionKind::getMetadata(), "section_debug_loc");
  DwarfLoclistsSection =
      Ctx->getMachOSection("__DWARF", "__debug_loclists", MachO::S_ATTR_DEBUG,
                           SectionKind::getMetadata(), "section_debug_loc");

  DwarfARangesSection =
      Ctx->getMachOSection("__DWARF", "__debug_aranges", MachO::S_ATTR_DEBUG,
                           SectionKind::getMetadata());
  DwarfRangesSection =
      Ctx->getMachOSection("__DWARF", "__debug_ranges", MachO::S_ATTR_DEBUG,
                           SectionKind::getMetadata(), "debug_range");
  DwarfRnglistsSection =
      Ctx->getMachOSection("__DWARF", "__debug_rnglists", MachO::S_ATTR_DEBUG,
                           SectionKind::getMetadata(), "debug_range");
  DwarfMacinfoSection =
      Ctx->getMachOSection("__DWARF", "__debug_macinfo", MachO::S_ATTR_DEBUG,
                           SectionKind::getMetadata(), "debug_macinfo");
  DwarfMacroSection =
      Ctx->getMachOSection("__DWARF", "__debug_macro", MachO::S_ATTR_DEBUG,
                           SectionKind::getMetadata(), "debug_macro");
  DwarfDebugInlineSection =
      Ctx->getMachOSection("__DWARF", "__debug_inlined", MachO::S_ATTR_DEBUG,
                           SectionKind::getMetadata());
  DwarfCUIndexSection =
      Ctx->getMachOSection("__DWARF", "__debug_cu_index", MachO::S_ATTR_DEBUG,
                           SectionKind::getMetadata());
  DwarfTUIndexSection =
      Ctx->getMachOSection("__DWARF", "__debug_tu_index", MachO::S_ATTR_DEBUG,
                           SectionKind::getMetadata());
  StackMapSection = Ctx->getMachOSection("__LLVM_STACKMAPS", "__llvm_stackmaps",
                                         0, SectionKind::getMetadata());

  FaultMapSection = Ctx->getMachOSection("__LLVM_FAULTMAPS", "__llvm_faultmaps",
                                         0, SectionKind::getMetadata());

  RemarksSection = Ctx->getMachOSection(
      "__LLVM", "__remarks", MachO::S_ATTR_DEBUG, SectionKind::getMetadata());

  TLSExtraDataSection = TLSTLVSection;
}

void MCObjectFileInfo::InitMCObjectFileInfo(const Triple &TheTriple, bool PIC,
                                            MCContext &ctx,
                                            bool LargeCodeModel) {
  PositionIndependent = PIC;
  Ctx = &ctx;

  // Common.
  CommDirectiveSupportsAlignment = true;
  SupportsWeakOmittedEHFrame = true;
  SupportsCompactUnwindWithoutEHFrame = false;
  OmitDwarfIfHaveCompactUnwind = false;

  FDECFIEncoding = dwarf::DW_EH_PE_absptr;

  CompactUnwindDwarfEHFrameOnly = 0;

  EHFrameSection = nullptr;             // Created on demand.
  CompactUnwindSection = nullptr;       // Used only by selected targets.
  DwarfAccelNamesSection = nullptr;     // Used only by selected targets.
  DwarfAccelObjCSection = nullptr;      // Used only by selected targets.
  DwarfAccelNamespaceSection = nullptr; // Used only by selected targets.
  DwarfAccelTypesSection = nullptr;     // Used only by selected targets.

  TT = TheTriple;

  switch (TT.getObjectFormat()) {
  case Triple::MachO:
    Env = IsMachO;
    initMachOMCObjectFileInfo(TT);
    break;
  case Triple::UnknownObjectFormat:
    report_fatal_error("Cannot initialize MC for unknown object file format.");
    break;
  }
}


MCSection *MCObjectFileInfo::getDwarfComdatSection(const char *Name,
                                                   uint64_t Hash) const {
  switch (TT.getObjectFormat()) {
  case Triple::ELF:
//    return Ctx->getELFSection(Name, ELF::SHT_PROGBITS, ELF::SHF_GROUP, 0,
//                              utostr(Hash));
//  case Triple::Wasm:
//    return Ctx->getWasmSection(Name, SectionKind::getMetadata(), utostr(Hash),
//                               MCContext::GenericSectionID);
  case Triple::MachO:
  case Triple::COFF:
  case Triple::GOFF:
  case Triple::XCOFF:
  case Triple::UnknownObjectFormat:
    report_fatal_error("Cannot get DWARF comdat section for this object file "
                       "format: not implemented.");
    break;
  }
  llvm_unreachable("Unknown ObjectFormatType");
}

MCSection *
MCObjectFileInfo::getPseudoProbeSection(const MCSection *TextSec) const {
//  if (Env == IsELF) {
//    const auto *ElfSec = static_cast<const MCSectionELF *>(TextSec);
//    // Create a separate section for probes that comes with a comdat function.
//    if (const MCSymbol *Group = ElfSec->getGroup()) {
//      auto *S = static_cast<MCSectionELF *>(PseudoProbeSection);
//      auto Flags = S->getFlags() | ELF::SHF_GROUP;
//      return Ctx->getELFSection(S->getName(), S->getType(), Flags,
//                                S->getEntrySize(), Group->getName());
//    }
//  }
  return PseudoProbeSection;
}
