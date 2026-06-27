# subconverter

Utility to convert between various proxy subscription formats.

Original upstream: https://github.com/tindy2013/subconverter  
Current repository: https://github.com/leroy20317/subconverter

[![Docker Image](https://github.com/leroy20317/subconverter/actions/workflows/docker.yml/badge.svg)](https://github.com/leroy20317/subconverter/actions/workflows/docker.yml)
[![GitHub tag](https://img.shields.io/github/tag/leroy20317/subconverter.svg)](https://github.com/leroy20317/subconverter/tags)
[![GitHub release](https://img.shields.io/github/release/leroy20317/subconverter.svg)](https://github.com/leroy20317/subconverter/releases)
[![GitHub license](https://img.shields.io/github/license/leroy20317/subconverter.svg)](https://github.com/leroy20317/subconverter/blob/master/LICENSE)

[Docker README](./README-docker.md)

[中文文档](./README-cn.md)

## Quick Start

Run with Docker:

```bash
docker run -d --restart=always -p 25500:25500 ghcr.io/leroy20317/subconverter:latest
curl http://127.0.0.1:25500/version
```

If the service is running, `/version` returns:

```txt
subconverter vx.x.x backend
```

Run from source:

```bash
cmake -DCMAKE_BUILD_TYPE=Release .
cmake --build . -j
./subconverter
```

On first start, the program will prefer `pref.toml`, then `pref.yml`, then `pref.ini`. If none exists, it copies `pref.example.toml`, `pref.example.yml`, or `pref.example.ini` in that order.

## HTTP Endpoints

Main endpoints exposed by the current code:

- `GET /version`
- `GET /sub`
- `HEAD /sub`
- `GET /sub2clashr`
- `GET /surge2clash`
- `GET /getruleset`
- `GET /getprofile`
- `GET /render`
- `GET /refreshrules`
- `GET /readconf`
- `POST /updateconf`
- `GET /flushcache`

When `api_mode=false`, two extra local/debug endpoints are available:

- `GET /get`
- `GET /getlocal`

## Supported Targets

| Type                             | As Source | As Target | `target` value |
| -------------------------------- | :-------: | :-------: | -------------- |
| Clash                            |     ✓     |     ✓     | `clash`        |
| ClashR                           |     ✓     |     ✓     | `clashr`       |
| Quantumult                       |     ✓     |     ✓     | `quan`         |
| Quantumult X                     |     ✓     |     ✓     | `quanx`        |
| Loon                             |     ✓     |     ✓     | `loon`         |
| Mellow                           |     ✓     |     ✓     | `mellow`       |
| Surfboard                        |     ✓     |     ✓     | `surfboard`    |
| Surge                            |     ✓     |     ✓     | `surge`        |
| SS (SIP002)                      |     ✓     |     ✓     | `ss`           |
| SS Android / SIP008              |     ✓     |     ✓     | `sssub`        |
| SSD                              |     ✓     |     ✓     | `ssd`          |
| SSR                              |     ✓     |     ✓     | `ssr`          |
| Trojan                           |     ✓     |     ✓     | `trojan`       |
| V2Ray                            |     ✓     |     ✓     | `v2ray`        |
| sing-box                         |     ✓     |     ✓     | `singbox`      |
| Telegram-like HTTP / Socks links |     ✓     |     ×     | Source only    |
| Mixed single-node subscription   |     ×     |     ✓     | `mixed`        |
| Auto by `User-Agent`             |     ×     |     ✓     | `auto`         |

Notes:

1. `auto` is resolved from the request `User-Agent` in `src/handler/interfaces.cpp`.
2. `mixed` outputs a regular Base64 subscription composed of supported single-node links.
3. Shadowrocket users should usually use `ss`, `ssr`, `v2ray`, or `mixed`.
4. Surge output is controlled by `target=surge`.
5. Quantumult X `AnyTLS` export uses `tls-host` for standard TLS and automatically emits `reality-base64-pubkey` / `reality-hex-shortid` when those fields exist on the node.

## Basic Usage

Generate a subscription with the default runtime configuration:

```txt
http://127.0.0.1:25500/sub?target=%TARGET%&url=%URL%&config=%CONFIG%
```

Parameters:

- `target`: required. Output format.
- `url`: required unless `default_url` is configured. URL-encoded subscription URL or node link. Multiple entries can be joined with `|` before encoding. `tag:name,link` marks all nodes from one source with a dedicated tag field that is separate from legacy source-provided `group` metadata; untagged nodes no longer emit an empty `group` field on export.
- `config`: optional. External config path or URL.

Tag-aware group rules supported by the current code:

- `!!UNTAGGED`: match only nodes without a `tag:` prefix.
- `%TAG%`: auto-expand one group per discovered tag.
- `!!TAG`: inside a `%TAG%` template group, match nodes of the current tag.
- `!!IN=ParentA,ParentB`: inside a `%TAG%` template group, attach each generated tag group to one or more parent groups.

Example:

```ini
custom_proxy_group=Default`select`!!UNTAGGED
custom_proxy_group=🌍 Media`select`[]Default`[]♻️ Auto`[]🎯 DIRECT
custom_proxy_group=%TAG%`select`!!TAG`!!IN=🌍 Media
```

Example:

```txt
http://127.0.0.1:25500/sub?target=clash&url=https%3A%2F%2Fexample.com%2Fsub
```

### Clash/mihomo Managed Rule Providers

When a Clash/mihomo managed configuration is generated, remote `ruleset` entries are emitted as `rule-providers` with `behavior: classical`. Rules are no longer split into separate `domain` and `ipcidr` providers, so classical rules such as `DOMAIN-KEYWORD`, `DOMAIN-SUFFIX`, and `IP-CIDR` remain in the same provider.

Example output:

```yaml
rules:
  - RULE-SET,Example,Proxy

rule-providers:
  Example:
    type: http
    behavior: classical
    url: http://127.0.0.1:25500/getruleset?type=6&url=...
    path: ./providers/xxx.yaml
```

Example provider payload:

```yaml
payload:
  - DOMAIN-SUFFIX,google.com
  - DOMAIN-KEYWORD,google
  - IP-CIDR,1.1.1.1/32,no-resolve
```

Quick conversion helpers:

```txt
http://127.0.0.1:25500/sub2clashr?sublink=...
http://127.0.0.1:25500/surge2clash?link=...
```

## Profile And Template Helpers

Profile endpoint:

```txt
http://127.0.0.1:25500/getprofile?name=%NAME%&token=%TOKEN%
```

- `name` points to a local profile file.
- `token` is always required.
- If the profile contains `profile_token`, that token is accepted for direct profile access; otherwise `api_access_token` is required.

Template debug endpoint:

```txt
http://127.0.0.1:25500/render?path=%TEMPLATE_PATH%
```

`path` must stay within the configured `template_path` scope.

## Generator Mode

The binary also supports local artifact generation:

```bash
./subconverter -g
./subconverter -g --artifact "artifact1,artifact2"
```

Generator settings are read from `generate.ini` in the current working directory. An example file is provided at `base/generate.ini`.

## Auto Upload

To upload generated content to Gist automatically:

1. Put your Personal Access Token into `gistconf.ini` in the current working directory. An example file is provided at `base/gistconf.ini`.
2. Add `&upload=true` to your local request URL.

## Thanks

[tindy2013](https://github.com/tindy2013)
[Upstream repository](https://github.com/tindy2013/subconverter)
