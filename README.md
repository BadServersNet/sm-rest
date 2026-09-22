# sm-rest

Asynchronous HTTP client extension for SourceMod. Streams uploads from disk and downloads to disk or to a chunk callback, reports progress, resumes downloads, retries transient failures and never blocks the game thread.

Build with `scripts/build.sh` (Steam Runtime sniper, Linux x86). Install the contents of `build/package` into the game directory and `#include <rest>`. See `pawn/scripting/rest-example.sp`.
