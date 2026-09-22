# sm-rest

Asynchronous HTTP client extension for SourceMod. Streams uploads from disk and downloads to disk or to a chunk callback, reports progress, resumes downloads, retries transient failures and never blocks the game thread.

Build with `scripts/build.sh` (Steam Runtime sniper, Linux x86). Install the contents of `build/package` into the game directory and `#include <rest>`. See `pawn/scripting/rest-example.sp`.

## Streaming to a plugin

`RESTRequest.OnData` delivers the 2xx body in chunks. Each chunk is copied onto the plugin heap before the callback runs, so a plugin must reserve enough heap with `#pragma dynamic` for the largest chunk it accepts (cells are 4 bytes, so 1 MiB chunks need at least 262144 cells plus headroom). `ChunkSize` sets the receive buffer and the size streamed data is coalesced to before each callback, from 1 KiB to 10 MiB, default 16 KiB. Over HTTPS curl delivers one TLS record (16 KiB) at a time, so without coalescing a 1 GiB download would mean 65537 callbacks; at 1 MiB it is about 1024. The extension pauses the transfer when 4 MiB of chunks are waiting for the main thread and resumes below 1 MiB, so a slow plugin cannot grow memory unbounded.

`RESTRequest.SetOutputFile` streams to disk inside the extension without any plugin heap cost and is the faster option when the bytes only need to land in a file. `OnHeaders` and `OnProgress` still work with it. Progress is sampled on the worker thread and posted to the main thread at most once per `ProgressInterval` milliseconds (default 100), plus once when either direction completes, so status updates cost a few cells per event and never carry body data.

Set `SM_REST_VERBOSE=1` in the server environment for libcurl's verbose output.

## Test results

`sm_streamtest` from [sm-rest-stream-test](https://github.com/BadServersNet/sm-rest-stream-test) on Dev CSGO, streaming the 1 GiB Hetzner file through `OnData` with a 1 MiB `ChunkSize` and the plugin writing every chunk to disk:

```
sm_streamtest
[StreamTest] Streaming download started (data callback, chunk size 1048576, progress interval 100ms): https://ash-speed.hetzner.com/1GB.bin -> addons/sourcemod/data/rest-stream-test.bin (request 1)
[StreamTest] Headers received. HTTP status: 200, Content-Length: -1 (-0.00 MiB, -1 means chunked or unknown)
[StreamTest] Streaming body to disk | Received: 43.01 MiB | chunks 43 (last 1048576 bytes) | progress updates 0 | 0.9s (46.94 MB/s)
[StreamTest] Streaming body to disk | Received: 103.01 MiB | chunks 103 (last 1048576 bytes) | progress updates 0 | 1.9s (53.76 MB/s)
[StreamTest] Streaming body to disk | Received: 164.01 MiB | chunks 164 (last 1048576 bytes) | progress updates 0 | 2.9s (56.24 MB/s)
[StreamTest] Streaming body to disk | Received: 225.01 MiB | chunks 225 (last 1048576 bytes) | progress updates 0 | 3.9s (57.45 MB/s)
[StreamTest] Streaming body to disk | Received: 286.01 MiB | chunks 286 (last 1048576 bytes) | progress updates 0 | 4.9s (58.17 MB/s)
[StreamTest] Streaming body to disk | Received: 347.01 MiB | chunks 347 (last 1048576 bytes) | progress updates 0 | 5.9s (58.65 MB/s)
[StreamTest] Streaming body to disk | Received: 408.01 MiB | chunks 408 (last 1048576 bytes) | progress updates 0 | 6.9s (58.99 MB/s)
[StreamTest] Streaming body to disk | Received: 469.01 MiB | chunks 469 (last 1048576 bytes) | progress updates 0 | 7.9s (59.24 MB/s)
[StreamTest] Streaming body to disk | Received: 530.01 MiB | chunks 530 (last 1048576 bytes) | progress updates 0 | 8.9s (59.44 MB/s)
[StreamTest] Streaming body to disk | Received: 591.01 MiB | chunks 591 (last 1048576 bytes) | progress updates 0 | 9.9s (59.60 MB/s)
[StreamTest] Streaming body to disk | Received: 652.01 MiB | chunks 652 (last 1048576 bytes) | progress updates 0 | 10.9s (59.72 MB/s)
[StreamTest] Streaming body to disk | Received: 713.01 MiB | chunks 713 (last 1048576 bytes) | progress updates 0 | 11.9s (59.83 MB/s)
[StreamTest] Streaming body to disk | Received: 775.01 MiB | chunks 775 (last 1048576 bytes) | progress updates 0 | 12.9s (60.00 MB/s)
[StreamTest] Streaming body to disk | Received: 836.01 MiB | chunks 836 (last 1048576 bytes) | progress updates 0 | 13.9s (60.07 MB/s)
[StreamTest] Streaming body to disk | Received: 897.01 MiB | chunks 897 (last 1048576 bytes) | progress updates 0 | 14.9s (60.13 MB/s)
[StreamTest] Streaming body to disk | Received: 958.01 MiB | chunks 958 (last 1048576 bytes) | progress updates 0 | 15.9s (60.19 MB/s)
[StreamTest] Streaming body to disk | Received: 1019.01 MiB | chunks 1019 (last 1048576 bytes) | progress updates 0 | 16.9s (60.23 MB/s)
[StreamTest] Download COMPLETE. HTTP status: 200, received 1024.00 / -0.00 MiB in 1024 chunks (largest 1064702 bytes) with 0 progress updates, file on disk 1073741824 bytes (match: yes), matches Content-Length: yes, time 16.9s (60.25 MB/s, extension reported 16978ms)
```

1 GiB in 16.9 s at 60 MB/s in 1024 callbacks, with the on-disk size matching the bytes received. The same file with 16 KiB chunks took 65537 callbacks at 62 MB/s, and 6.9 MB/s through SteamWorks, so the transfer is network-bound and the chunk size only changes callback overhead. The server answered with chunked encoding, so there was no `Content-Length` to compare against.
