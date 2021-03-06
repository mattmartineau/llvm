//===- TypeVisitorCallbackPipeline.h -------------------------- *- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_TYPEVISITORCALLBACKPIPELINE_H
#define LLVM_DEBUGINFO_CODEVIEW_TYPEVISITORCALLBACKPIPELINE_H

#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/TypeRecord.h"
#include "llvm/DebugInfo/CodeView/TypeVisitorCallbacks.h"

#include <vector>

namespace llvm {
namespace codeview {
class TypeVisitorCallbackPipeline : public TypeVisitorCallbacks {
public:
  TypeVisitorCallbackPipeline() {}

  virtual Error visitUnknownType(CVRecord<TypeLeafKind> &Record) override {
    for (auto Visitor : Pipeline) {
      if (auto EC = Visitor->visitUnknownType(Record))
        return EC;
    }
    return Error::success();
  }

  virtual Error visitUnknownMember(CVMemberRecord &Record) override {
    for (auto Visitor : Pipeline) {
      if (auto EC = Visitor->visitUnknownMember(Record))
        return EC;
    }
    return Error::success();
  }

  virtual Error visitTypeBegin(CVType &Record) override {
    for (auto Visitor : Pipeline) {
      if (auto EC = Visitor->visitTypeBegin(Record))
        return EC;
    }
    return Error::success();
  }
  virtual Error visitTypeEnd(CVType &Record) override {
    for (auto Visitor : Pipeline) {
      if (auto EC = Visitor->visitTypeEnd(Record))
        return EC;
    }
    return Error::success();
  }

  virtual Error visitMemberBegin(CVMemberRecord &Record) override {
    for (auto Visitor : Pipeline) {
      if (auto EC = Visitor->visitMemberBegin(Record))
        return EC;
    }
    return Error::success();
  }
  virtual Error visitMemberEnd(CVMemberRecord &Record) override {
    for (auto Visitor : Pipeline) {
      if (auto EC = Visitor->visitMemberEnd(Record))
        return EC;
    }
    return Error::success();
  }

  void addCallbackToPipeline(TypeVisitorCallbacks &Callbacks) {
    Pipeline.push_back(&Callbacks);
  }

#define TYPE_RECORD(EnumName, EnumVal, Name)                                   \
  Error visitKnownRecord(CVType &CVR, Name##Record &Record) override {         \
    for (auto Visitor : Pipeline) {                                            \
      if (auto EC = Visitor->visitKnownRecord(CVR, Record))                    \
        return EC;                                                             \
    }                                                                          \
    return Error::success();                                                   \
  }
#define MEMBER_RECORD(EnumName, EnumVal, Name)                                 \
  Error visitKnownMember(CVMemberRecord &CVMR, Name##Record &Record)           \
      override {                                                               \
    for (auto Visitor : Pipeline) {                                            \
      if (auto EC = Visitor->visitKnownMember(CVMR, Record))                   \
        return EC;                                                             \
    }                                                                          \
    return Error::success();                                                   \
  }
#define TYPE_RECORD_ALIAS(EnumName, EnumVal, Name, AliasName)
#define MEMBER_RECORD_ALIAS(EnumName, EnumVal, Name, AliasName)
#include "llvm/DebugInfo/CodeView/TypeRecords.def"

private:
  std::vector<TypeVisitorCallbacks *> Pipeline;
};
}
}

#endif
