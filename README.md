# sm-rest

Asynchronous HTTP client extension for SourceMod. Streams uploads from disk and downloads to disk or to a chunk callback, reports progress, resumes downloads, retries transient failures and never blocks the game thread.

Build with `scripts/build.sh` (Steam Runtime sniper, Linux x86). Install the contents of `build/package` into the game directory and `#include <rest>`. See `pawn/scripting/rest-example.sp`.

## Streaming to a plugin

`RESTRequest.OnData` delivers the 2xx body in chunks. Each chunk is copied onto the plugin heap before the callback runs, so a plugin must reserve enough heap with `#pragma dynamic` for the largest chunk it accepts (cells are 4 bytes, so 1 MiB chunks need at least 262144 cells plus headroom). `ChunkSize` sets the receive buffer and the size streamed data is coalesced to before each callback, from 1 KiB to 10 MiB, default 16 KiB. Over HTTPS curl delivers one TLS record (16 KiB) at a time, so without coalescing a 1 GiB download would mean 65537 callbacks; at 1 MiB it is about 1024. The extension pauses the transfer when 4 MiB of chunks are waiting for the main thread and resumes below 1 MiB, so a slow plugin cannot grow memory unbounded.

`RESTRequest.SetOutputFile` streams to disk inside the extension without any plugin heap cost and is the faster option when the bytes only need to land in a file. `OnHeaders` and `OnProgress` still work with it. Progress is sampled on the worker thread and posted to the main thread at most once per `ProgressInterval` milliseconds (default 100), plus once when either direction completes, so status updates cost a few cells per event and never carry body data.

Set `SM_REST_VERBOSE=1` in the server environment for libcurl's verbose output.

## Test results

`sm_streamtest` from [sm-rest-stream-test](https://github.com/BadServersNet/sm-rest-stream-test) on Dev CSGO, streaming the 1 GiB Hetzner file through `OnData` with the plugin writing every chunk to disk, before chunk coalescing was added (every chunk was one 16 KiB TLS record):

```
[StreamTest] Streaming download started (data callback, chunk size 1048576): https://ash-speed.hetzner.com/1GB.bin -> addons/sourcemod/data/rest-stream-test.bin (request 1)
[StreamTest] Headers received. HTTP status: 200, Content-Length: -1 (-0.00 MiB)
[StreamTest] Streaming body to disk | Received: 48.17 MiB | chunks 3083 (last 16384 bytes) | 0.9s (50.84 MB/s)
[StreamTest] Streaming body to disk | Received: 310.04 MiB | chunks 19843 (last 16384 bytes) | 4.9s (62.66 MB/s)
[StreamTest] Streaming body to disk | Received: 617.04 MiB | chunks 39491 (last 16384 bytes) | 9.9s (62.03 MB/s)
[StreamTest] Streaming body to disk | Received: 994.54 MiB | chunks 63651 (last 16384 bytes) | 15.9s (62.36 MB/s)
[StreamTest] Download COMPLETE. HTTP status: 200, received 1024.00 / -0.00 MiB in 65537 chunks (largest 16384 bytes), file on disk 1073741824 bytes (match: yes), matches Content-Length: NO, time 16.3s (62.46 MB/s, extension reported 16385ms)
```

1 GiB in 16.3 s at 62 MB/s with the on-disk size matching the bytes received, against 6.9 MB/s for the same file through SteamWorks. The server answered with chunked encoding, so there was no `Content-Length` to compare against.
