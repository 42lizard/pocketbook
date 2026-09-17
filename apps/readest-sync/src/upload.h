#pragma once
#include "progress.h"
namespace readest {
struct UploadPosition {
    NativePosition native;
    std::string error;
    bool unsupported=false;
};
struct UploadResult {
    SyncAction action=SyncAction::None;
    std::string warning;
    bool pending=false;
};
// One explicit upload/retry, with durable account-scoped checkpoints. Uses the
// same reconciliation boundary as ordinary sync; never applies native progress.
UploadResult upload_book(Cloud& cloud,State& state,const VerifiedManagedBook& book,
    const UploadPosition& position,const HttpTransport& transport,
    const std::string& ca,const std::string& root,const std::atomic<bool>& cancel);
}
