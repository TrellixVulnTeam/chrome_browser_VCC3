// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/platform/heap/heap_compact.h"

#include <memory>

#include "base/debug/alias.h"
#include "base/memory/ptr_util.h"
#include "third_party/blink/renderer/platform/heap/heap.h"
#include "third_party/blink/renderer/platform/heap/heap_stats_collector.h"
#include "third_party/blink/renderer/platform/heap/sparse_heap_bitmap.h"
#include "third_party/blink/renderer/platform/histogram.h"
#include "third_party/blink/renderer/platform/runtime_enabled_features.h"
#include "third_party/blink/renderer/platform/wtf/allocator.h"
#include "third_party/blink/renderer/platform/wtf/hash_map.h"
#include "third_party/blink/renderer/platform/wtf/time.h"

namespace blink {

bool HeapCompact::force_compaction_gc_ = false;

// The real worker behind heap compaction, recording references to movable
// objects ("slots".) When the objects end up being compacted and moved,
// relocate() will adjust the slots to point to the new location of the
// object along with handling fixups for interior pointers.
//
// The "fixups" object is created and maintained for the lifetime of one
// heap compaction-enhanced GC.
class HeapCompact::MovableObjectFixups final {
  USING_FAST_MALLOC(HeapCompact::MovableObjectFixups);

 public:
  explicit MovableObjectFixups(ThreadHeap* heap) : heap_(heap) {}
  ~MovableObjectFixups() = default;

  // For the arenas being compacted, record all pages belonging to them.
  // This is needed to handle 'interior slots', pointers that themselves
  // can move (independently from the reference the slot points to.)
  void AddCompactingPage(BasePage* page) {
    DCHECK(!page->IsLargeObjectPage());
    relocatable_pages_.insert(page);
  }

  void Add(MovableReference* slot) {
    MovableReference reference = *slot;
    CHECK(reference);

    // All slots and references are part of Oilpan's heap.
    CHECK(heap_->LookupPageForAddress(reinterpret_cast<Address>(slot)));
    CHECK(heap_->LookupPageForAddress(reinterpret_cast<Address>(reference)));

    BasePage* const reference_page = PageFromObject(reference);
    // The following cases are not compacted and do not require recording:
    // - Backings in large pages.
    // - Inline backings that are part of a non-backing arena.
    if (reference_page->IsLargeObjectPage() ||
        !HeapCompact::IsCompactableArena(reference_page->Arena()->ArenaIndex()))
      return;

    // Slots may have been recorded already but must point to the same
    // reference. Example: Ephemeron iterations may register slots multiple
    // times.
    auto fixup_it = fixups_.find(reference);
    if (UNLIKELY(fixup_it != fixups_.end())) {
      CHECK_EQ(slot, fixup_it->second);
      return;
    }

    // References must always point to live objects at this point.
    // Slots must reside in live objects

    // Add regular fixup.
    fixups_.insert({reference, slot});

    BasePage* const slot_page = PageFromObject(slot);

    // Slots must reside in and references must point to live objects at this
    // point, with the exception of slots in eagerly swept arenas where objects
    // have already been processed. |reference| usually points to a separate
    // backing store but can also point to inlined storage which is why the
    // dynamic header lookup is required.
    CHECK(reference_page->Arena()->ArenaIndex() !=
          BlinkGC::kEagerSweepArenaIndex);
    CHECK(static_cast<NormalPage*>(reference_page)
              ->FindHeaderFromAddress(reinterpret_cast<Address>(reference))
              ->IsMarked());
    CHECK(slot_page->Arena()->ArenaIndex() == BlinkGC::kEagerSweepArenaIndex ||
          static_cast<NormalPage*>(slot_page)
              ->FindHeaderFromAddress(reinterpret_cast<Address>(slot))
              ->IsMarked());

    // Check whether the slot itself resides on a page that is compacted.
    if (LIKELY(!relocatable_pages_.Contains(slot_page)))
      return;

    // Interior slots are recorded as follows:
    // - Storing it in the interior map, which maps the slot to its (eventual)
    //   location. Initially nullptr.
    // - Mark it as being interior pointer within the page's interior bitmap.
    //   This bitmap is used when moving a backing store to check whether an
    //   interior slot is to be redirected.
    auto interior_it = interior_fixups_.find(slot);
    // Repeated registrations have been filtered above.
    CHECK(interior_fixups_.end() == interior_it);
    interior_fixups_.insert({slot, nullptr});
    LOG_HEAP_COMPACTION() << "Interior slot: " << slot;
    Address slot_address = reinterpret_cast<Address>(slot);
    if (!interiors_) {
      interiors_ = std::make_unique<SparseHeapBitmap>(slot_address);
      return;
    }
    interiors_->Add(slot_address);
  }

  void AddFixupCallback(MovableReference* slot,
                        MovingObjectCallback callback,
                        void* callback_data) {
    DCHECK(!fixup_callbacks_.Contains(slot));
    fixup_callbacks_.insert(
        slot, std::pair<void*, MovingObjectCallback>(callback_data, callback));
  }

  void RelocateInteriorFixups(Address from, Address to, size_t size) {
    SparseHeapBitmap* range = interiors_->HasRange(from, size);
    if (LIKELY(!range))
      return;

    // Scan through the payload, looking for interior pointer slots
    // to adjust. If the backing store of such an interior slot hasn't
    // been moved already, update the slot -> real location mapping.
    // When the backing store is eventually moved, it'll use that location.
    for (size_t offset = 0; offset < size; offset += sizeof(void*)) {
      MovableReference* slot =
          reinterpret_cast<MovableReference*>(from + offset);

      // Early bailout.
      if (!range->IsSet(reinterpret_cast<Address>(slot)))
        continue;

      auto it = interior_fixups_.find(slot);
      if (it == interior_fixups_.end())
        continue;

      // If |slot|'s mapping is set, then the slot has been adjusted already.
      if (it->second)
        continue;

      Address fixup = to + offset;
      LOG_HEAP_COMPACTION() << "Range interior fixup: " << (from + offset)
                            << " " << it->second << " " << fixup;
      // Fill in the relocated location of the original slot at |slot|.
      // when the backing store corresponding to |slot| is eventually
      // moved/compacted, it'll update |to + offset| with a pointer to the
      // moved backing store.
      interior_fixups_[slot] = fixup;

      // If the |slot|'s content is pointing into the region [from, from + size)
      // we are dealing with an interior pointer that does not point to a valid
      // HeapObjectHeader. Such references need to be fixed up immediately.
      Address fixup_contents = *reinterpret_cast<Address*>(fixup);
      if (fixup_contents > from && fixup_contents < (from + size)) {
        *reinterpret_cast<Address*>(fixup) = fixup_contents - from + to;
        continue;
      }
    }
  }

  void Relocate(Address from, Address to) {
    auto it = fixups_.find(from);
    // This means that there is no corresponding slot for a live backing store.
    // This may happen because a mutator may change the slot to point to a
    // different backing store because e.g.:
    // - Incremental marking marked a backing store as live that was later on
    //   replaced.
    // - Backings were changed when being processed in
    //   EagerSweep/PreFinalizer/WeakProcessing.
    if (it == fixups_.end())
      return;

#if DCHECK_IS_ON()
    BasePage* from_page = PageFromObject(from);
    DCHECK(relocatable_pages_.Contains(from_page));
#endif

    // TODO(keishi): Code to determine if crash is related to interior fixups.
    // Remove when finished. crbug.com/918064
    enum DebugSlotType {
      kNormalSlot,
      kInteriorSlotPreMove,
      kInteriorSlotPostMove,
    };
    DebugSlotType slot_type = kNormalSlot;
    base::debug::Alias(&slot_type);

    // If the object is referenced by a slot that is contained on a compacted
    // area itself, check whether it can be updated already.
    MovableReference* slot = reinterpret_cast<MovableReference*>(it->second);
    auto interior = interior_fixups_.find(slot);
    if (interior != interior_fixups_.end()) {
      MovableReference* slot_location =
          reinterpret_cast<MovableReference*>(interior->second);
      if (!slot_location) {
        interior_fixups_[slot] = to;
        slot_type = kInteriorSlotPreMove;
      } else {
        LOG_HEAP_COMPACTION()
            << "Redirected slot: " << slot << " => " << slot_location;
        slot = slot_location;
        slot_type = kInteriorSlotPostMove;
      }
    }

    // If the slot has subsequently been updated, a prefinalizer or
    // a destructor having mutated and expanded/shrunk the collection,
    // do not update and relocate the slot -- |from| is no longer valid
    // and referenced.
    //
    // The slot's contents may also have been cleared during weak processing;
    // no work to be done in that case either.
    if (UNLIKELY(*slot != from)) {
      LOG_HEAP_COMPACTION()
          << "No relocation: slot = " << slot << ", *slot = " << *slot
          << ", from = " << from << ", to = " << to;
      VerifyUpdatedSlot(slot);
      return;
    }

    // Update the slots new value.
    *slot = to;

    size_t size = 0;

    // Execute potential fixup callbacks.
    MovableReference* callback_slot =
        reinterpret_cast<MovableReference*>(it->second);
    auto callback = fixup_callbacks_.find(callback_slot);
    if (UNLIKELY(callback != fixup_callbacks_.end())) {
      size = HeapObjectHeader::FromPayload(to)->PayloadSize();
      callback->value.second(callback->value.first, from, to, size);
    }

    if (!interiors_)
      return;

    if (!size)
      size = HeapObjectHeader::FromPayload(to)->PayloadSize();
    RelocateInteriorFixups(from, to, size);
  }

#if DEBUG_HEAP_COMPACTION
  void dumpDebugStats() {
    LOG_HEAP_COMPACTION() << "Fixups: pages=" << relocatable_pages_.size()
                          << " objects=" << fixups_.size()
                          << " callbacks=" << fixup_callbacks_.size()
                          << " interior-size="
                          << (interiors_ ? interiors_->IntervalCount() : 0,
                              interior_fixups_.size());
  }
#endif

 private:
  void VerifyUpdatedSlot(MovableReference* slot);

  ThreadHeap* const heap_;

  // Tracking movable and updatable references. For now, we keep a
  // map which for each movable object, recording the slot that
  // points to it. Upon moving the object, that slot needs to be
  // updated.
  //
  // (TODO: consider in-place updating schemes.)
  std::unordered_map<MovableReference, MovableReference*> fixups_;

  // Map from movable reference to callbacks that need to be invoked
  // when the object moves.
  HashMap<MovableReference*, std::pair<void*, MovingObjectCallback>>
      fixup_callbacks_;

  // Slot => relocated slot/final location.
  std::unordered_map<MovableReference*, Address> interior_fixups_;

  // All pages that are being compacted. The set keeps references to
  // BasePage instances. The void* type was selected to allow to check
  // arbitrary addresses.
  HashSet<void*> relocatable_pages_;

  std::unique_ptr<SparseHeapBitmap> interiors_;
};

void HeapCompact::MovableObjectFixups::VerifyUpdatedSlot(
    MovableReference* slot) {
// Verify that the already updated slot is valid, meaning:
//  - has been cleared.
//  - has been updated & expanded with a large object backing store.
//  - has been updated with a larger, freshly allocated backing store.
//    (on a fresh page in a compactable arena that is not being
//    compacted.)
#if DCHECK_IS_ON()
  if (!*slot)
    return;
  BasePage* slot_page =
      heap_->LookupPageForAddress(reinterpret_cast<Address>(*slot));
  // ref_page is null if *slot is pointing to an off-heap region. This may
  // happy if *slot is pointing to an inline buffer of HeapVector with
  // inline capacity.
  if (!slot_page)
    return;
  DCHECK(slot_page->IsLargeObjectPage() ||
         (HeapCompact::IsCompactableArena(slot_page->Arena()->ArenaIndex()) &&
          !relocatable_pages_.Contains(slot_page)));
#endif  // DCHECK_IS_ON()
}

HeapCompact::HeapCompact(ThreadHeap* heap) : heap_(heap) {
  // The heap compaction implementation assumes the contiguous range,
  //
  //   [Vector1ArenaIndex, HashTableArenaIndex]
  //
  // in a few places. Use static asserts here to not have that assumption
  // be silently invalidated by ArenaIndices changes.
  static_assert(BlinkGC::kVector1ArenaIndex + 3 == BlinkGC::kVector4ArenaIndex,
                "unexpected ArenaIndices ordering");
  static_assert(
      BlinkGC::kVector4ArenaIndex + 1 == BlinkGC::kInlineVectorArenaIndex,
      "unexpected ArenaIndices ordering");
  static_assert(
      BlinkGC::kInlineVectorArenaIndex + 1 == BlinkGC::kHashTableArenaIndex,
      "unexpected ArenaIndices ordering");
}

HeapCompact::~HeapCompact() = default;

HeapCompact::MovableObjectFixups& HeapCompact::Fixups() {
  if (!fixups_)
    fixups_ = std::make_unique<MovableObjectFixups>(heap_);
  return *fixups_;
}

bool HeapCompact::ShouldCompact(ThreadHeap* heap,
                                BlinkGC::StackState stack_state,
                                BlinkGC::MarkingType marking_type,
                                BlinkGC::GCReason reason) {
#if !ENABLE_HEAP_COMPACTION
  return false;
#else
  if (!RuntimeEnabledFeatures::HeapCompactionEnabled())
    return false;

  LOG_HEAP_COMPACTION() << "shouldCompact(): gc=" << static_cast<int>(reason)
                        << " count=" << gc_count_since_last_compaction_
                        << " free=" << free_list_size_;
  gc_count_since_last_compaction_++;

  // If the GCing thread requires a stack scan, do not compact.
  // Why? Should the stack contain an iterator pointing into its
  // associated backing store, its references wouldn't be
  // correctly relocated.
  if (stack_state == BlinkGC::kHeapPointersOnStack)
    return false;

  if (reason == BlinkGC::GCReason::kForcedGCForTesting) {
    UpdateHeapResidency();
    return force_compaction_gc_;
  }

  if (!RuntimeEnabledFeatures::HeapCompactionWhenIncrementalMarkingEnabled()) {
    if (reason != BlinkGC::GCReason::kPreciseGC)
      return false;

    // TODO(keishi): crbug.com/918064 Heap compaction for incremental marking
    // needs to be disabled until this crash is fixed.
    CHECK_NE(marking_type, BlinkGC::kIncrementalMarking);
  }

  // Compaction enable rules:
  //  - It's been a while since the last time.
  //  - "Considerable" amount of heap memory is bound up in freelist
  //    allocations. For now, use a fixed limit irrespective of heap
  //    size.
  //
  // As this isn't compacting all arenas, the cost of doing compaction
  // isn't a worry as it will additionally only be done by idle GCs.
  // TODO: add some form of compaction overhead estimate to the marking
  // time estimate.

  UpdateHeapResidency();

#if STRESS_TEST_HEAP_COMPACTION
  // Exercise the handling of object movement by compacting as
  // often as possible.
  return true;
#else
  return force_compaction_gc_ || (gc_count_since_last_compaction_ >
                                      kGCCountSinceLastCompactionThreshold &&
                                  free_list_size_ > kFreeListSizeThreshold);
#endif
#endif
}

void HeapCompact::Initialize(ThreadState* state) {
  DCHECK(RuntimeEnabledFeatures::HeapCompactionEnabled());
  LOG_HEAP_COMPACTION() << "Compacting: free=" << free_list_size_;
  do_compact_ = true;
  fixups_.reset();
  gc_count_since_last_compaction_ = 0;
  force_compaction_gc_ = false;
}

void HeapCompact::RegisterMovingObjectReference(MovableReference* slot) {
  CHECK(heap_->LookupPageForAddress(reinterpret_cast<Address>(slot)));

  if (!do_compact_)
    return;

  traced_slots_.insert(slot);
}

void HeapCompact::RegisterMovingObjectCallback(MovableReference* slot,
                                               MovingObjectCallback callback,
                                               void* callback_data) {
  DCHECK(heap_->LookupPageForAddress(reinterpret_cast<Address>(slot)));

  if (!do_compact_)
    return;

  Fixups().AddFixupCallback(slot, callback, callback_data);
}

void HeapCompact::UpdateHeapResidency() {
  size_t total_arena_size = 0;
  size_t total_free_list_size = 0;

  compactable_arenas_ = 0;
#if DEBUG_HEAP_FREELIST
  std::stringstream stream;
#endif
  for (int i = BlinkGC::kVector1ArenaIndex; i <= BlinkGC::kHashTableArenaIndex;
       ++i) {
    NormalPageArena* arena = static_cast<NormalPageArena*>(heap_->Arena(i));
    size_t arena_size = arena->ArenaSize();
    size_t free_list_size = arena->FreeListSize();
    total_arena_size += arena_size;
    total_free_list_size += free_list_size;
#if DEBUG_HEAP_FREELIST
    stream << i << ": [" << arena_size << ", " << free_list_size << "], ";
#endif
    // TODO: be more discriminating and consider arena
    // load factor, effectiveness of past compactions etc.
    if (!arena_size)
      continue;
    // Mark the arena as compactable.
    compactable_arenas_ |= 0x1u << i;
  }
#if DEBUG_HEAP_FREELIST
  LOG_HEAP_FREELIST() << "Arena residencies: {" << stream.str() << "}";
  LOG_HEAP_FREELIST() << "Total = " << total_arena_size
                      << ", Free = " << total_free_list_size;
#endif

  // TODO(sof): consider smoothing the reported sizes.
  free_list_size_ = total_free_list_size;
}

void HeapCompact::FinishedArenaCompaction(NormalPageArena* arena,
                                          size_t freed_pages,
                                          size_t freed_size) {
  if (!do_compact_)
    return;

  heap_->stats_collector()->IncreaseCompactionFreedPages(freed_pages);
  heap_->stats_collector()->IncreaseCompactionFreedSize(freed_size);
}

void HeapCompact::Relocate(Address from, Address to) {
  Fixups().Relocate(from, to);
}

void HeapCompact::StartThreadCompaction() {
  if (!do_compact_)
    return;

  // The mapping between the slots and the backing stores are created
  last_fixup_count_for_testing_ = 0;
  for (auto** slot : traced_slots_) {
    if (*slot) {
      Fixups().Add(slot);
      last_fixup_count_for_testing_++;
    }
  }
  traced_slots_.clear();
}

void HeapCompact::FinishThreadCompaction() {
  if (!do_compact_)
    return;

#if DEBUG_HEAP_COMPACTION
  if (fixups_)
    fixups_->dumpDebugStats();
#endif
  fixups_.reset();
  do_compact_ = false;
}

void HeapCompact::CancelCompaction() {
  if (!do_compact_)
    return;

  last_fixup_count_for_testing_ = 0;
  traced_slots_.clear();
  fixups_.reset();
  do_compact_ = false;
}

void HeapCompact::AddCompactingPage(BasePage* page) {
  DCHECK(do_compact_);
  DCHECK(IsCompactingArena(page->Arena()->ArenaIndex()));
  Fixups().AddCompactingPage(page);
}

bool HeapCompact::ScheduleCompactionGCForTesting(bool value) {
  bool current = force_compaction_gc_;
  force_compaction_gc_ = value;
  return current;
}

}  // namespace blink
