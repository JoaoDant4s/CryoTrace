# CryoTrace relayer

Subscribes to `coldchain/+/anomaly`, and for each event the ESP32 signed calls
`CryoTrace.recordEvent` on-chain, paying the gas. It holds no device key: it can
delay or drop an event, but cannot forge or alter one — the contract checks the
device signature.

For each message:

| Outcome | When | Action |
|---|---|---|
| `recorded` | valid event, transaction mined | ack |
| `duplicate` | already on-chain (e.g. MQTT QoS 1 redelivery) | ack, no transaction |
| `rejected` | the contract refuses it (`InvalidSigner`, `BatchNotOnBox`, ...) | ack, logged; detected by simulating the call, so no gas is spent |
| `invalid` | malformed payload, or `boxId` does not match the topic | ack, logged |

Network/RPC errors are retried with backoff until one of the outcomes above.
Messages are acknowledged to the broker only after that (persistent session,
manual ack), so a crash mid-way makes the broker redeliver them.

## Setup

Requires Python 3.10+ and a contract ABI built with `forge build` in `../contracts`.

```sh
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt -r requirements-dev.txt
```

## Run

```sh
cp .env.example .env      # fill in RPC, contract, relayer key and MQTT broker
set -a; . ./.env; set +a
.venv/bin/python -m cryotrace_relayer
```

## Test

```sh
.venv/bin/python -m pytest
```

- `tests/test_payload.py`: parsing of the exact JSON the firmware publishes.
- `tests/test_relayer_anvil.py`: end to end against a local chain (anvil, from
  Foundry) running the real contract, with events signed independently by
  `eth_account`: records a valid event, skips duplicates, and rejects tampered
  events, foreign keys, wrong batches and mismatched topics without spending gas.
