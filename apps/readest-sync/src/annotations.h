#pragma once
#include "progress.h"

namespace readest {
// Synchronize EPUB text highlights and attached notes for one verified local
// copy. Full per-book pulls avoid client-clock cursors. Retry state is durable
// before writes; conflicting edits fail without overwriting either side.
// Native writes require the validated firmware and an unchanged snapshot.
void sync_annotations(Cloud& cloud,State& state,const VerifiedManagedBook& verified,
    const NativePosition& native,const std::string& database,long long now,
    const std::string& model,const std::string& firmware,const std::atomic<bool>& cancel);
}
