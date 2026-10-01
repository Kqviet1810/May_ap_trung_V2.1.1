# MAYAP account + tenant isolation (Draft, Web 12.2.0)

Branch starts at `feat/web-fast-connect` commit `37d8035`. No production deployment,
remote migration, merge, credential rotation or real OTA was performed for this change.

## Account and data model

Google authorization code OIDC runs on the Worker, with a random state cookie,
single-use 10-minute D1 transaction, nonce and PKCE S256. Worker exchanges the code
with Google's token endpoint, verifies RS256 through Google's cached/rotating JWKS,
issuer, audience, authorized party, expiry, issued-at, nonce and subject. Google
`sub` is the identity key; email/name are display fields. No Google access/refresh
token is stored. No password registration is introduced.

Migration `0004_accounts.sql` adds `users`, `user_sessions`, `user_devices` and
`oauth_transactions`, and binds push subscriptions to account/session. A partial
unique index enforces one owner per device; membership roles are owner/operator/viewer.
Claim uses authenticated account + CSRF + existing device PIN hash/pepper, atomic
attempt reservation and conditional insert. Wrong PIN is rejected; a valid PIN
cannot take over someone else's device. Owner transfer is deliberately unavailable
until a verified transfer workflow is added. A reset PIN at HMI does not transfer ownership.

MAYAP session: random opaque 256-bit token, only its peppered hash in D1,
7-day absolute expiry, revocation row, cookie `__Host-mayap_session` with
HttpOnly/Secure/SameSite=Lax/Path=/. CSRF secret is another HttpOnly cookie and
returned to same-origin JS RAM by session API; writes require Origin + CSRF header.
Logout revokes the current session, deletes its push links, clears this account's
local cache/control keys and disconnects MQTT. Other tabs receive BroadcastChannel
logout. Settings can revoke another session. Other gateways detect revocation at
the next 60s batch check; failed checks shut sockets within 90s of the last success.

Web/static assets are served through Worker ASSETS **on the same origin**. This avoids
third-party-cookie dependence between github.io and workers.dev, especially on iOS.
The account gate hides both dashboard and landing while checking. Valid session goes
straight to owned devices; 401 shows landing; network failure keeps the check/retry
screen. A cold offline reload cannot validate ownership and fails closed. A running
authenticated tab retains its RAM/cache and existing MQTT during temporary HTTP failure.

Cache keys: `mayap.account.<Google sub>.runtime.v1.<Device ID>`, plus selection and
batch preferences under the same account namespace. Cache is explicitly old data,
never live. Legacy browser pairing secrets are discarded, not migrated into ownership.
`pairingToken='account-session'` inside the existing app object is a **non-secret
RAM/UI compatibility marker** for unchanged V2 signing guards, never an authorization
credential; Worker strips/ignores it and uses cookie + D1 ownership. No old token
can authorize API or MQTT. Device firmware identity/provisioning remains unchanged.

`/api/account/session` validates all owned devices together, once per 5 minutes while
visible and when resuming after that interval. No per-device session-check/name/status
poll loop. Push linked counts come in the same account response. API status/history/
config and MQTT/control ticket issuance check membership. Full realtime config/history
remain lazy over authorized MQTT. Viewer tickets contain read rights and lease/session
rights, but no control grant or command/config/reminder/history write rights.

## MQTT gateway and commercial cutover

HiveMQ Serverless topic permissions exist, but its management capabilities differ
from Starter and it cannot validate these account tickets directly. Shared browser
username/password never provided read isolation, even with per-device command HMAC.

This change supplies a small Node WSS MQTT gateway (`mqtt-gateway/`) placed near the
broker. Browser receives a Worker-signed Ed25519 JWT valid at most 15 minutes,
bound to account session, per-tab client ID, read devices and writable devices.
The gateway verifies JWT and current account rights before opening one HiveMQ WSS
connection per browser (about 20 at target size). Broker password stays server-side.
Commands and signed ACK are forwarded unchanged; gateway does not sign or execute them.

Exact topic ACL excludes `#`, `+`, `$share`, `$SYS`, other machines and arbitrary
topics. Core reads: presence/bootstrap/snapshot/ack. Lazy reads:
config/reported, reminders/reported, history/reported, log. Writes:
session, command, config/set, reminders/set, history/request with appropriate roles.
Only V2 control envelopes, QoS 0/1, no retained writes, normal payload <1536 bytes.
Upstream PUBLISH is checked too, so a broker misroute cannot leak another tenant.
Parser fragments, queues and backpressure are bounded; MQTT v3.1.1 clean sessions
only, no browser LWT. Gateway upstream TLS certificate verification stays enabled.

Ticket renewal goes through reserved `mayap/auth/renew` QoS1, consumed by gateway.
It updates the ACL/expiry and acknowledges locally while retaining the same socket;
no renewal reaches ESP32/broker. Worker reissues ticket during normal control-grant
prefetch. Account revocation/ownership is checked by one signed gateway batch every
60s, never an HTTP call per packet/click. Session ticket expiry and fail-closed check
deadline stop stale authorization. The gateway host/reverse proxy is an additional
operational dependency; colocate it to keep command latency low and monitor it.

**No production isolation claim until old shared credentials AND old live broker
connections are revoked.** Deleting a HiveMQ credential alone does not terminate
connections already established with it. If ESP32 currently shares that credential
with old browsers, provision a separate broker credential per ESP32 (read its own
incoming topics, publish only its own outgoing topics), plus a server-only gateway
credential with the required 30-device ACL. Rotate the old credential and drain old
browser connections in a coordinated maintenance window. Existing local safety/control
continues during network downtime. Do not enable `MQTT_ISOLATION_READY=1` beforehand.

Account mode defaults **fail closed** (`MQTT_ISOLATION_READY=0`). Login/claim can work
once Google/session secrets are set; MQTT session returns 503 with a clear UI message
until cutover is complete. No shared-password fallback exists. Old verify-pin,
session-check, sign-mqtt routes return 410; V1 signing handlers are removed and
firmware rejects V1 control. Old pages must reload/login; local machine control is
not bricked. Old `device_clients`/browser_limit are retained audit/schema records,
revoked in migration, with their authorization code removed. `device_inventory`
remains factory admission and account access disable list.

## Realtime and fleet request budget

Cloud heartbeat 60s instead of 15s: 30 machines = 30 requests/minute, 43,200/day
instead of 172,800/day. Offline cloud threshold remains 180s, with cron detection
up to another 60s; MQTT LWT/freshness remains independent and faster. Alarm traffic
and retry/safety paths are unchanged.

Snapshots: any foreground lease = 400ms; only WARM hidden leases = 3000ms;
no active lease = 6000ms. Web hidden remains WARM for 300s, refreshes 45s TTL every
15s bounded to its real hidden deadline. Any active lease keeps PERFORMANCE. After
all leases expire/idle, existing nonblocking 25s grace permits SAVE. Browser/OS
freeze is handled by actual elapsed timestamps and ESP lease TTL. Return immediately
publishes foreground active + runtime sync if stale, restoring fast snapshots.

MQTT socket is kept in WARM and IDLE while healthy; resume/pageshow/online are
coalesced and packet/PINGRESP liveness is independent of device snapshots. Expired
gateway tickets are renewed outside click handlers before MQTT retry. Grant expiry
shows “Đang chuẩn bị quyền điều khiển”. Grant stays 5 minutes and is prefetched
at <=60s remaining, single flight/backoff; no renewal after WARM becomes IDLE.

20 active browsers: account batch ~4 requests/minute; grant/ticket renewal roughly
5/minute; gateway introspection 1/minute (up to two D1 queries/session).
MQTT control click remains local HMAC V2 → gateway MQTT → HiveMQ → ESP32
→ controller → signed ACK. No Cloudflare HTTP per click. Extra gateway hop needs
real latency validation; target remains 100–500ms command receipt, ACK usually <1s.
PID/safety/turning/alarm/HMI/EEPROM/ATtiny/batch recovery are unchanged.

## Configuration and rollout (operator must perform later)

1. Back up D1. Apply pending migration **0004_accounts.sql** with existing migration
   tracking, after confirming 0001–0003 are applied. It clears old browser push links
   and revokes old browser sessions; customers login + claim once. Never assign an
   owner from an old pairing token. Fresh staging DB needs the existing base schema
   before migration tracking, since old migrations assume that base already exists.
2. Google Cloud OAuth client type **Web application**, consent screen and test users
   during draft. Authorized redirect URI exactly `<APP_ORIGIN>/auth/google/callback`.
   Configure authorized JavaScript origin `<APP_ORIGIN>` if the console requests it;
   no Google frontend SDK/token post endpoint is used. APP_ORIGIN must be HTTPS and
   exact origin, with no trailing slash. Workers.dev or custom domain is supported.
3. Worker vars: APP_ORIGIN, GOOGLE_CLIENT_ID, MQTT_GATEWAY_URL (`wss://…/mqtt`),
   MQTT_ISOLATION_READY (keep 0 until validated). Worker secrets: GOOGLE_CLIENT_SECRET,
   MAYAP_SESSION_PEPPER (separate random secret), MQTT_TICKET_PRIVATE_KEY (Ed25519
   PKCS8 PEM), MQTT_GATEWAY_CHECK_SECRET. Existing DEVICE_KEY_PEPPER/VAPID/OTA secrets
   stay unchanged; **do not rotate DEVICE_KEY_PEPPER as part of account migration**.
4. Gateway env: APP_ORIGIN, MQTT_TICKET_PUBLIC_KEY (Ed25519 SPKI PEM), same
   MQTT_GATEWAY_CHECK_SECRET, HIVEMQ_WSS_URL, HIVEMQ_GATEWAY_USERNAME/PASSWORD.
   Private ticket signing key stays only in Worker. Configure secure WSS reverse
   proxy `/mqtt` with Upgrade/Connection, idle timeout > MQTT keepalive, request
   size/rate limits. Container listens 8080 internally; do not expose plain WS publicly.
5. Run `node tools/build_account_site.mjs` before local Worker bundle/deploy. Only
   explicit public files are copied. Web moves from github.io to APP_ORIGIN; update
   bookmarks/PWA installation and old Pages redirect in the coordinated rollout.
6. Test account A/B, browser credential denial against broker directly, gateway
   revocation, physical command/ACK, background/resume and iOS cookie/PWA behavior.
   Then operator may enable isolation gate. This Draft does not execute these steps.

Primary references: [Google OIDC](https://developers.google.com/identity/openid-connect/openid-connect),
[Worker assets](https://developers.cloudflare.com/workers/static-assets/binding/),
[HiveMQ auth/topic permissions](https://docs.hivemq.com/hivemq-cloud/authn-authz.html),
[HiveMQ REST availability](https://docs.hivemq.com/hivemq-cloud/rest-api.html).

## Validation

Run Node 24 tests (`node --test tests/*.test.cjs`) with frozen Worker/gateway packages
installed. Account tests use SQLite constraints and server Worker routes, RS256 tokens
and actual JWT verification. Gateway tests use real WebSocket client packets and
production parser/ACL with a simulated broker; no customer/device I/O.
Browser scripts test real DOM/WebCrypto at mobile/desktop sizes, login gate, logout,
cache/live separation, retained bootstrap, local command/ACK, WARM and no click HTTP.
ESP32/ATtiny build, protected safety fingerprints and Linux ASan/UBSan remain CI gates.
Android/iOS suspension, Google production login/consent, actual HiveMQ connection drain,
30 physical machines, reverse-proxy latency and real Push delivery require hardware/
staging validation. Host/fixture tests do not prove production tenant isolation.
