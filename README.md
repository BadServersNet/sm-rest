# sm-rest

Asynchronous HTTP client extension for SourceMod. Streams uploads from disk and downloads to disk or to a chunk callback, reports progress, resumes downloads, retries transient failures and never blocks the game thread.

Build with `scripts/build.sh` (Steam Runtime sniper, Linux x86). Install the contents of `build/package` into the game directory and `#include <rest>`. See `pawn/scripting/rest-example.sp`.

## Streaming to a plugin

`RESTRequest.OnData` delivers the 2xx body in chunks. Each chunk is copied onto the plugin heap before the callback runs, so a plugin must reserve enough heap with `#pragma dynamic` for the largest chunk it accepts (cells are 4 bytes, so 1 MiB chunks need at least 262144 cells plus headroom). `ChunkSize` sets the receive buffer that bounds chunk size, from 1 KiB to 10 MiB, default 16 KiB. Larger chunks mean fewer callbacks per transfer. The extension pauses the transfer when 4 MiB of chunks are waiting for the main thread and resumes below 1 MiB, so a slow plugin cannot grow memory unbounded.

`RESTRequest.SetOutputFile` streams to disk inside the extension without any plugin heap cost and is the faster option when the bytes only need to land in a file.

Set `SM_REST_VERBOSE=1` in the server environment for libcurl's verbose output.
