# Security

## Supported versions

| Version | Supported |
|---|---|
| 1.0.x | yes |

## Reporting a vulnerability

Please report security problems privately, through GitHub's **"Report a vulnerability"** button on this
repository's *Security* tab, not in a public issue. Include what you found, how to reproduce it, and what an
attacker could do with it. You can expect a first answer within a week.

## Security model

Outpost is a demonstration of edge/origin reliability patterns. It assumes a **trusted network**, and several
things are open by design:

- **No authentication or TLS** on any endpoint, including `PUT /key/<k>` and `POST /purge/key/<k>` on the
  router.
- **Spoofable client identity:** the edges trust the first `X-Forwarded-For` hop to identify a client for rate
  limiting, so a client can choose its own identity.
- **The simulated-outage switch:** write-api's `/admin/outage` exists only when `ADMIN_ENABLED=1` (the compose
  default, for the dashboard). The dashboard on port 8090 forwards to it, so anyone who can reach port 8090 can
  start a simulated outage.
- **Published ports:** Docker Compose publishes ports 8080 (router) and 8090 (dashboard) on all interfaces. On a
  shared network, bind them to localhost instead (`"127.0.0.1:8080:8080"` and `"127.0.0.1:8090:8090"` in
  `docker-compose.yml`) and set `ADMIN_ENABLED=0` when you don't need the outage switch.

Edges, write-api and storage are reachable only inside the compose network. Request sizes are bounded (key
≤ 256 bytes, value ≤ 1 MiB), and every external call has a timeout.
