"""Entry point: python -m cryotrace_relayer (configuration from environment variables)."""

import logging
import os
import sys
import time
from pathlib import Path

import paho.mqtt.client as mqtt
from web3 import Web3

from .chain import DEFAULT_ABI_PATH, ChainClient, load_abi
from .relayer import Outcome, Relayer

log = logging.getLogger("cryotrace_relayer")

MAX_BACKOFF_S = 60


def env(name: str, default: str | None = None) -> str:
    value = os.environ.get(name, default)
    if value is None or value == "":
        sys.exit(f"missing environment variable {name} (see relayer/.env.example)")
    return value


def process_with_retry(relayer: Relayer, topic: str, payload: bytes) -> None:
    """Retries network/RPC failures until the event reaches a final outcome."""
    backoff = 1
    while True:
        try:
            result = relayer.handle(topic, payload)
            break
        except Exception as exc:  # RPC or network: transient, try again
            log.warning("%s: %s; retrying in %d s", topic, exc, backoff)
            time.sleep(backoff)
            backoff = min(backoff * 2, MAX_BACKOFF_S)

    if result.outcome is Outcome.RECORDED:
        r = result.receipt
        log.info("%s: recorded %s (tx %s, block %d, gas %d)", topic, result.detail,
                 r.tx_hash, r.block_number, r.gas_used)
    elif result.outcome is Outcome.DUPLICATE:
        log.info("%s: skipped, %s", topic, result.detail)
    else:
        log.error("%s: %s, dropped: %s", topic, result.outcome.value, result.detail)


def main() -> None:
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")

    w3 = Web3(Web3.HTTPProvider(env("RPC_URL")))
    chain = ChainClient(
        w3,
        env("CONTRACT_ADDRESS"),
        load_abi(Path(env("CONTRACT_ABI", str(DEFAULT_ABI_PATH)))),
        env("RELAYER_PRIVATE_KEY"),
    )
    relayer = Relayer(chain)
    topic = env("MQTT_TOPIC", "coldchain/+/anomaly")
    log.info("relayer account %s, contract %s", chain.account.address, chain.contract.address)

    # Persistent session + manual ack: a message is only acknowledged after it
    # reaches a final outcome, so a crash mid-way makes the broker redeliver it.
    client = mqtt.Client(
        callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
        client_id=env("MQTT_CLIENT_ID", "cryotrace-relayer"),
        clean_session=False,
        manual_ack=True,
    )
    if os.environ.get("MQTT_USERNAME"):
        client.username_pw_set(env("MQTT_USERNAME"), env("MQTT_PASSWORD"))
    if env("MQTT_TLS", "1") == "1":
        client.tls_set()

    def on_connect(client, userdata, flags, reason_code, properties):
        if reason_code.is_failure:
            log.error("MQTT connection refused: %s", reason_code)
            return
        client.subscribe(topic, qos=1)
        log.info("subscribed to %s", topic)

    def on_message(client, userdata, msg):
        process_with_retry(relayer, msg.topic, msg.payload)
        client.ack(msg.mid, msg.qos)

    client.on_connect = on_connect
    client.on_message = on_message
    client.connect_async(env("MQTT_HOST"), int(env("MQTT_PORT", "8883")))
    client.loop_forever(retry_first_connection=True)


if __name__ == "__main__":
    main()
