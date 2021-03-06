//===- FuzzerMutate.cpp - Mutate a test input -----------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// Mutate a test input.
//===----------------------------------------------------------------------===//

#include <cstring>
#include <unordered_set>

#include "FuzzerCorpus.h"
#include "FuzzerDefs.h"
#include "FuzzerExtFunctions.h"
#include "FuzzerMutate.h"
#include "FuzzerOptions.h"

namespace fuzzer {

const size_t Dictionary::kMaxDictSize;

static void PrintASCII(const Word &W, const char *PrintAfter) {
  PrintASCII(W.data(), W.size(), PrintAfter);
}

MutationDispatcher::MutationDispatcher(Random &Rand,
                                       const FuzzingOptions &Options)
    : Rand(Rand), Options(Options) {
  DefaultMutators.insert(
      DefaultMutators.begin(),
      {
          {&MutationDispatcher::Mutate_EraseBytes, "EraseBytes"},
          {&MutationDispatcher::Mutate_InsertByte, "InsertByte"},
          {&MutationDispatcher::Mutate_InsertRepeatedBytes,
           "InsertRepeatedBytes"},
          {&MutationDispatcher::Mutate_ChangeByte, "ChangeByte"},
          {&MutationDispatcher::Mutate_ChangeBit, "ChangeBit"},
          {&MutationDispatcher::Mutate_ShuffleBytes, "ShuffleBytes"},
          {&MutationDispatcher::Mutate_ChangeASCIIInteger, "ChangeASCIIInt"},
          {&MutationDispatcher::Mutate_ChangeBinaryInteger, "ChangeBinInt"},
          {&MutationDispatcher::Mutate_CopyPart, "CopyPart"},
          {&MutationDispatcher::Mutate_CrossOver, "CrossOver"},
          {&MutationDispatcher::Mutate_AddWordFromManualDictionary,
           "AddFromManualDict"},
          {&MutationDispatcher::Mutate_AddWordFromTemporaryAutoDictionary,
           "AddFromTempAutoDict"},
          {&MutationDispatcher::Mutate_AddWordFromPersistentAutoDictionary,
           "AddFromPersAutoDict"},
      });

  if (EF->LLVMFuzzerCustomMutator)
    Mutators.push_back({&MutationDispatcher::Mutate_Custom, "Custom"});
  else
    Mutators = DefaultMutators;

  if (EF->LLVMFuzzerCustomCrossOver)
    Mutators.push_back(
        {&MutationDispatcher::Mutate_CustomCrossOver, "CustomCrossOver"});
}

static char RandCh(Random &Rand) {
  if (Rand.RandBool()) return Rand(256);
  const char *Special = "!*'();:@&=+$,/?%#[]012Az-`~.\xff\x00";
  return Special[Rand(sizeof(Special) - 1)];
}

size_t MutationDispatcher::Mutate_Custom(uint8_t *Data, size_t Size,
                                         size_t MaxSize) {
  return EF->LLVMFuzzerCustomMutator(Data, Size, MaxSize, Rand.Rand());
}

size_t MutationDispatcher::Mutate_CustomCrossOver(uint8_t *Data, size_t Size,
                                                  size_t MaxSize) {
  if (!Corpus || Corpus->size() < 2 || Size == 0)
    return 0;
  size_t Idx = Rand(Corpus->size());
  const Unit &Other = (*Corpus)[Idx];
  if (Other.empty())
    return 0;
  MutateInPlaceHere.resize(MaxSize);
  auto &U = MutateInPlaceHere;
  size_t NewSize = EF->LLVMFuzzerCustomCrossOver(
      Data, Size, Other.data(), Other.size(), U.data(), U.size(), Rand.Rand());
  if (!NewSize)
    return 0;
  assert(NewSize <= MaxSize && "CustomCrossOver returned overisized unit");
  memcpy(Data, U.data(), NewSize);
  return NewSize;
}

size_t MutationDispatcher::Mutate_ShuffleBytes(uint8_t *Data, size_t Size,
                                               size_t MaxSize) {
  assert(Size);
  size_t ShuffleAmount =
      Rand(std::min(Size, (size_t)8)) + 1; // [1,8] and <= Size.
  size_t ShuffleStart = Rand(Size - ShuffleAmount);
  assert(ShuffleStart + ShuffleAmount <= Size);
  std::random_shuffle(Data + ShuffleStart, Data + ShuffleStart + ShuffleAmount,
                      Rand);
  return Size;
}

size_t MutationDispatcher::Mutate_EraseBytes(uint8_t *Data, size_t Size,
                                             size_t MaxSize) {
  assert(Size);
  if (Size == 1) return 0;
  size_t N = Rand(Size / 2) + 1;
  assert(N < Size);
  size_t Idx = Rand(Size - N + 1);
  // Erase Data[Idx:Idx+N].
  memmove(Data + Idx, Data + Idx + N, Size - Idx - N);
  // Printf("Erase: %zd %zd => %zd; Idx %zd\n", N, Size, Size - N, Idx);
  return Size - N;
}

size_t MutationDispatcher::Mutate_InsertByte(uint8_t *Data, size_t Size,
                                             size_t MaxSize) {
  if (Size == MaxSize) return 0;
  size_t Idx = Rand(Size + 1);
  // Insert new value at Data[Idx].
  memmove(Data + Idx + 1, Data + Idx, Size - Idx);
  Data[Idx] = RandCh(Rand);
  return Size + 1;
}

size_t MutationDispatcher::Mutate_InsertRepeatedBytes(uint8_t *Data,
                                                      size_t Size,
                                                      size_t MaxSize) {
  const size_t kMinBytesToInsert = 3;
  if (Size + kMinBytesToInsert >= MaxSize) return 0;
  size_t MaxBytesToInsert = std::min(MaxSize - Size, (size_t)128);
  size_t N = Rand(MaxBytesToInsert - kMinBytesToInsert + 1) + kMinBytesToInsert;
  assert(Size + N <= MaxSize && N);
  size_t Idx = Rand(Size + 1);
  // Insert new values at Data[Idx].
  memmove(Data + Idx + N, Data + Idx, Size - Idx);
  // Give preference to 0x00 and 0xff.
  uint8_t Byte = Rand.RandBool() ? Rand(256) : (Rand.RandBool() ? 0 : 255);
  for (size_t i = 0; i < N; i++)
    Data[Idx + i] = Byte;
  return Size + N;
}

size_t MutationDispatcher::Mutate_ChangeByte(uint8_t *Data, size_t Size,
                                             size_t MaxSize) {
  size_t Idx = Rand(Size);
  Data[Idx] = RandCh(Rand);
  return Size;
}

size_t MutationDispatcher::Mutate_ChangeBit(uint8_t *Data, size_t Size,
                                            size_t MaxSize) {
  size_t Idx = Rand(Size);
  Data[Idx] ^= 1 << Rand(8);
  return Size;
}

size_t MutationDispatcher::Mutate_AddWordFromManualDictionary(uint8_t *Data,
                                                              size_t Size,
                                                              size_t MaxSize) {
  return AddWordFromDictionary(ManualDictionary, Data, Size, MaxSize);
}

size_t MutationDispatcher::Mutate_AddWordFromTemporaryAutoDictionary(
    uint8_t *Data, size_t Size, size_t MaxSize) {
  return AddWordFromDictionary(TempAutoDictionary, Data, Size, MaxSize);
}

size_t MutationDispatcher::Mutate_AddWordFromPersistentAutoDictionary(
    uint8_t *Data, size_t Size, size_t MaxSize) {
  return AddWordFromDictionary(PersistentAutoDictionary, Data, Size, MaxSize);
}

size_t MutationDispatcher::AddWordFromDictionary(Dictionary &D, uint8_t *Data,
                                                 size_t Size, size_t MaxSize) {
  if (D.empty()) return 0;
  DictionaryEntry &DE = D[Rand(D.size())];
  const Word &W = DE.GetW();
  bool UsePositionHint = DE.HasPositionHint() &&
                         DE.GetPositionHint() + W.size() < Size && Rand.RandBool();
  if (Rand.RandBool()) {  // Insert W.
    if (Size + W.size() > MaxSize) return 0;
    size_t Idx = UsePositionHint ? DE.GetPositionHint() : Rand(Size + 1);
    memmove(Data + Idx + W.size(), Data + Idx, Size - Idx);
    memcpy(Data + Idx, W.data(), W.size());
    Size += W.size();
  } else {  // Overwrite some bytes with W.
    if (W.size() > Size) return 0;
    size_t Idx = UsePositionHint ? DE.GetPositionHint() : Rand(Size - W.size());
    memcpy(Data + Idx, W.data(), W.size());
  }
  DE.IncUseCount();
  CurrentDictionaryEntrySequence.push_back(&DE);
  return Size;
}

// Overwrites part of To[0,ToSize) with a part of From[0,FromSize).
// Returns ToSize.
size_t MutationDispatcher::CopyPartOf(const uint8_t *From, size_t FromSize,
                                      uint8_t *To, size_t ToSize) {
  // Copy From[FromBeg, FromBeg + CopySize) into To[ToBeg, ToBeg + CopySize).
  size_t ToBeg = Rand(ToSize);
  size_t CopySize = Rand(ToSize - ToBeg) + 1;
  assert(ToBeg + CopySize <= ToSize);
  CopySize = std::min(CopySize, FromSize);
  size_t FromBeg = Rand(FromSize - CopySize + 1);
  assert(FromBeg + CopySize <= FromSize);
  memmove(To + ToBeg, From + FromBeg, CopySize);
  return ToSize;
}

// Inserts part of From[0,ToSize) into To.
// Returns new size of To on success or 0 on failure.
size_t MutationDispatcher::InsertPartOf(const uint8_t *From, size_t FromSize,
                                        uint8_t *To, size_t ToSize,
                                        size_t MaxToSize) {
  if (ToSize >= MaxToSize) return 0;
  size_t AvailableSpace = MaxToSize - ToSize;
  size_t MaxCopySize = std::min(AvailableSpace, FromSize);
  size_t CopySize = Rand(MaxCopySize) + 1;
  size_t FromBeg = Rand(FromSize - CopySize + 1);
  assert(FromBeg + CopySize <= FromSize);
  size_t ToInsertPos = Rand(ToSize + 1);
  assert(ToInsertPos + CopySize <= MaxToSize);
  size_t TailSize = ToSize - ToInsertPos;
  if (To == From) {
    MutateInPlaceHere.resize(MaxToSize);
    memcpy(MutateInPlaceHere.data(), From + FromBeg, CopySize);
    memmove(To + ToInsertPos + CopySize, To + ToInsertPos, TailSize);
    memmove(To + ToInsertPos, MutateInPlaceHere.data(), CopySize);
  } else {
    memmove(To + ToInsertPos + CopySize, To + ToInsertPos, TailSize);
    memmove(To + ToInsertPos, From + FromBeg, CopySize);
  }
  return ToSize + CopySize;
}

size_t MutationDispatcher::Mutate_CopyPart(uint8_t *Data, size_t Size,
                                           size_t MaxSize) {
  if (Rand.RandBool())
    return CopyPartOf(Data, Size, Data, Size);
  else
    return InsertPartOf(Data, Size, Data, Size, MaxSize);
}

size_t MutationDispatcher::Mutate_ChangeASCIIInteger(uint8_t *Data, size_t Size,
                                                     size_t MaxSize) {
  size_t B = Rand(Size);
  while (B < Size && !isdigit(Data[B])) B++;
  if (B == Size) return 0;
  size_t E = B;
  while (E < Size && isdigit(Data[E])) E++;
  assert(B < E);
  // now we have digits in [B, E).
  // strtol and friends don't accept non-zero-teminated data, parse it manually.
  uint64_t Val = Data[B] - '0';
  for (size_t i = B + 1; i < E; i++)
    Val = Val * 10 + Data[i] - '0';

  // Mutate the integer value.
  switch(Rand(5)) {
    case 0: Val++; break;
    case 1: Val--; break;
    case 2: Val /= 2; break;
    case 3: Val *= 2; break;
    case 4: Val = Rand(Val * Val); break;
    default: assert(0);
  }
  // Just replace the bytes with the new ones, don't bother moving bytes.
  for (size_t i = B; i < E; i++) {
    size_t Idx = E + B - i - 1;
    assert(Idx >= B && Idx < E);
    Data[Idx] = (Val % 10) + '0';
    Val /= 10;
  }
  return Size;
}

uint8_t  Bswap(uint8_t x)  { return x; }
uint16_t Bswap(uint16_t x) { return __builtin_bswap16(x); }
uint32_t Bswap(uint32_t x) { return __builtin_bswap32(x); }
uint64_t Bswap(uint64_t x) { return __builtin_bswap64(x); }

template<class T>
size_t ChangeBinaryInteger(uint8_t *Data, size_t Size, Random &Rand) {
  if (Size < sizeof(T)) return 0;
  size_t Off = Rand(Size - sizeof(T) + 1);
  assert(Off + sizeof(T) <= Size);
  T Val;
  memcpy(&Val, Data + Off, sizeof(Val));
  T Add = Rand(21);
  Add -= 10;
  if (Rand.RandBool())
    Val = Bswap(T(Bswap(Val) + Add));  // Add assuming different endiannes.
  else
    Val = Val + Add;                   // Add assuming current endiannes.
  if (Add == 0 || Rand.RandBool())     // Maybe negate.
    Val = -Val;
  memcpy(Data + Off, &Val, sizeof(Val));
  return Size;
}

size_t MutationDispatcher::Mutate_ChangeBinaryInteger(uint8_t *Data,
                                                      size_t Size,
                                                      size_t MaxSize) {
  switch (Rand(4)) {
    case 3: return ChangeBinaryInteger<uint64_t>(Data, Size, Rand);
    case 2: return ChangeBinaryInteger<uint32_t>(Data, Size, Rand);
    case 1: return ChangeBinaryInteger<uint16_t>(Data, Size, Rand);
    case 0: return ChangeBinaryInteger<uint8_t>(Data, Size, Rand);
    default: assert(0);
  }
  return 0;
}

size_t MutationDispatcher::Mutate_CrossOver(uint8_t *Data, size_t Size,
                                            size_t MaxSize) {
  if (!Corpus || Corpus->size() < 2 || Size == 0) return 0;
  size_t Idx = Rand(Corpus->size());
  const Unit &O = (*Corpus)[Idx];
  if (O.empty()) return 0;
  MutateInPlaceHere.resize(MaxSize);
  auto &U = MutateInPlaceHere;
  size_t NewSize = 0;
  switch(Rand(3)) {
    case 0:
      NewSize = CrossOver(Data, Size, O.data(), O.size(), U.data(), U.size());
      break;
    case 1:
      NewSize = InsertPartOf(O.data(), O.size(), U.data(), U.size(), MaxSize);
      if (NewSize)
        break;
      // LLVM_FALLTHROUGH;
    case 2:
      NewSize = CopyPartOf(O.data(), O.size(), U.data(), U.size());
      break;
    default: assert(0);
  }
  assert(NewSize > 0 && "CrossOver returned empty unit");
  assert(NewSize <= MaxSize && "CrossOver returned overisized unit");
  memcpy(Data, U.data(), NewSize);
  return NewSize;
}

void MutationDispatcher::StartMutationSequence() {
  CurrentMutatorSequence.clear();
  CurrentDictionaryEntrySequence.clear();
}

// Copy successful dictionary entries to PersistentAutoDictionary.
void MutationDispatcher::RecordSuccessfulMutationSequence() {
  for (auto DE : CurrentDictionaryEntrySequence) {
    // PersistentAutoDictionary.AddWithSuccessCountOne(DE);
    DE->IncSuccessCount();
    // Linear search is fine here as this happens seldom.
    if (!PersistentAutoDictionary.ContainsWord(DE->GetW()))
      PersistentAutoDictionary.push_back({DE->GetW(), 1});
  }
}

void MutationDispatcher::PrintRecommendedDictionary() {
  std::vector<DictionaryEntry> V;
  for (auto &DE : PersistentAutoDictionary)
    if (!ManualDictionary.ContainsWord(DE.GetW()))
      V.push_back(DE);
  if (V.empty()) return;
  Printf("###### Recommended dictionary. ######\n");
  for (auto &DE: V) {
    Printf("\"");
    PrintASCII(DE.GetW(), "\"");
    Printf(" # Uses: %zd\n", DE.GetUseCount());
  }
  Printf("###### End of recommended dictionary. ######\n");
}

void MutationDispatcher::PrintMutationSequence() {
  Printf("MS: %zd ", CurrentMutatorSequence.size());
  for (auto M : CurrentMutatorSequence)
    Printf("%s-", M.Name);
  if (!CurrentDictionaryEntrySequence.empty()) {
    Printf(" DE: ");
    for (auto DE : CurrentDictionaryEntrySequence) {
      Printf("\"");
      PrintASCII(DE->GetW(), "\"-");
    }
  }
}

size_t MutationDispatcher::Mutate(uint8_t *Data, size_t Size, size_t MaxSize) {
  return MutateImpl(Data, Size, MaxSize, Mutators);
}

size_t MutationDispatcher::DefaultMutate(uint8_t *Data, size_t Size,
                                         size_t MaxSize) {
  return MutateImpl(Data, Size, MaxSize, DefaultMutators);
}

// Mutates Data in place, returns new size.
size_t MutationDispatcher::MutateImpl(uint8_t *Data, size_t Size,
                                      size_t MaxSize,
                                      const std::vector<Mutator> &Mutators) {
  assert(MaxSize > 0);
  assert(Size <= MaxSize);
  if (Size == 0) {
    for (size_t i = 0; i < MaxSize; i++)
      Data[i] = RandCh(Rand);
    if (Options.OnlyASCII)
      ToASCII(Data, MaxSize);
    return MaxSize;
  }
  assert(Size > 0);
  // Some mutations may fail (e.g. can't insert more bytes if Size == MaxSize),
  // in which case they will return 0.
  // Try several times before returning un-mutated data.
  for (int Iter = 0; Iter < 10; Iter++) {
    auto M = Mutators[Rand(Mutators.size())];
    size_t NewSize = (this->*(M.Fn))(Data, Size, MaxSize);
    if (NewSize) {
      if (Options.OnlyASCII)
        ToASCII(Data, NewSize);
      CurrentMutatorSequence.push_back(M);
      return NewSize;
    }
  }
  return Size;
}

void MutationDispatcher::AddWordToManualDictionary(const Word &W) {
  ManualDictionary.push_back(
      {W, std::numeric_limits<size_t>::max()});
}

void MutationDispatcher::AddWordToAutoDictionary(DictionaryEntry DE) {
  static const size_t kMaxAutoDictSize = 1 << 14;
  if (TempAutoDictionary.size() >= kMaxAutoDictSize) return;
  TempAutoDictionary.push_back(DE);
}

void MutationDispatcher::ClearAutoDictionary() {
  TempAutoDictionary.clear();
}

}  // namespace fuzzer
