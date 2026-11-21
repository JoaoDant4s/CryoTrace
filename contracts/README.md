# CryoTrace contracts

`src/CryoTrace.sol` records cold-chain anomaly and recovery events signed by each
box's ESP32-S3 (EIP-712, secp256k1). Anyone can submit an event (in practice, the
relayer); the contract accepts it only if the signature matches the key registered
for that box.

## Setup

```sh
forge install foundry-rs/forge-std --no-git
forge install OpenZeppelin/openzeppelin-contracts@v5.1.0 --no-git
```

## Test

```sh
forge test               # all tests
forge test --gas-report  # gas per function
```

## Signed event (EIP-712)

Domain: name `CryoTrace`, version `1`, chain id, contract address.

```
ColdChainEvent(bytes32 boxId,bytes32 batchId,uint8 kind,uint8 anomalyType,
               uint64 startTime,uint64 endTime,int16 temperature,int16 peak,uint32 counter)
```

Any change to this struct changes what the firmware signs.
`test_DigestMatchesEip712Spec` spells out the exact bytes.
