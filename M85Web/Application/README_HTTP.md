# Onboard lwIP HTTP / control API

The integrated CPU0 image contains lwIP HTTPD, a static-file bundle, and a control API. The DHCP task starts HTTPD only after it receives a lease. This is a build/source fact, not proof that DHCP or HTTP works on a physical board; those checks remain open in `../../docs/HARDWARE_TEST.md`.

## Shipped routes and assets

- `GET /` serves the UI generated from `mini-4wd-webapp/index.html`, `style.css`, and `js/*.js` and embedded in `web/fsdata.h`.
- `POST /api/control` accepts the fixed-schema JSON command, stores the request and receive time in the controller interface, and returns the current response JSON through the HTTPD custom-file response.
- `GET /api/control` is not a control request and returns `405`. There is no separate `/api/telemetry` route.
- `/video_feed` is not implemented by this firmware and returns `404`. The shipped UI displays an explicit unavailable-camera placeholder; it does not request this route.
- The shipped manual UI exposes forward and left/right steering only. It has no reverse button or reverse keyboard mapping. The protocol wire type can represent negative throttle, but current vehicle-control logic clamps reverse and the rear is not monitored.

## Source ownership

- `app_main_httpd.c` initializes lwIP and DHCP; HTTPD is started after a DHCP lease.
- `http/http_server.c` initializes the mailbox/API and calls `httpd_init()` from `tcpip_thread` using `tcpip_callback()`.
- `http/control_api.c` implements `POST /api/control`, bounded JSON reception, and custom HTTP responses.
- `interface/controller_if.c` owns the request/response mailbox shared with the M85 gateway.
- M33 `control_runtime` and the board motor-output driver own safety decisions and motor GPIO/PWM. Selecting MANUAL or AUTO at boot does not cause automatic drive; a valid deadman and local start conditions are required.
- The former loopback test task (`control_test_task`) was removed; the M85 gateway is the only consumer of the control mailbox. PC mock/sample code under `mini-4wd-webapp` is not part of the embedded asset bundle.

## Regenerating embedded UI

From the package root, run:

```powershell
.\M85Web\script\generate_web.ps1
```

The script uses the bundled lwIP `makefsdata` Perl source and Git Bash/MSYS2, stages files under the system temporary directory, then replaces `M85Web/Application/web/fsdata.h`. It includes only the HTML, CSS, JS, optional browser assets, and the 404 page. The reference lwIP generator source is not modified. The checked-in `fsdata.h` is already generated and normal firmware builds do not require Perl or Git Bash.

## Verification boundary

Offline source/host/ELF checks do not verify an Ethernet link, DHCP lease, real browser/API exchange, camera sensor output, or video streaming. No board download, motor-power connection, or hardware test is performed by the package build. Follow the staged VM-OFF / VM-ON procedure in `../../docs/HARDWARE_TEST.md`; keep those hardware checks explicitly unverified until physically performed.
