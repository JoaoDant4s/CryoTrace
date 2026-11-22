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

## Deploy to Sepolia

1. **RPC and environment.** Get a Sepolia RPC URL (e.g. Alchemy or Infura, free
   tier), then:

   ```sh
   cp .env.example .env          # fill in SEPOLIA_RPC_URL
   set -a; . ./.env; set +a
   ```

2. **Deployer wallet** in an encrypted Foundry keystore (the key never sits in
   plain text in a file):

   ```sh
   cast wallet new                                   # prints a new address + private key
   cast wallet import cryotrace-deployer --interactive   # paste the key, choose a password
   ```

   Fund its address with Sepolia ETH from a faucet.

3. **Deploy.** The deployer becomes admin and holds the provisioner and logistics
   roles (set `PROVISIONER` / `LOGISTICS` to give them to other addresses):

   ```sh
   forge script script/Deploy.s.sol --rpc-url sepolia --account cryotrace-deployer --broadcast
   # add --verify (with ETHERSCAN_API_KEY set) to publish the source on Etherscan
   ```

   The address is printed and saved in `broadcast/Deploy.s.sol/11155111/run-latest.json`.
   Put it in `CONTRACT_ADDRESS` (here, in the relayer's `.env` and in the firmware's
   menuconfig > CryoTrace - Blockchain).

4. **Provision a box and a batch.** `DEVICE_ADDRESS` is the address the ESP32 logs
   on its first boot (`device address (register in the contract): 0x...`);
   `BOX_ID` / `BATCH_ID` are the strings set in the firmware's menuconfig.
   Already-done steps are skipped, so it is safe to rerun:

   ```sh
   forge script script/Provision.s.sol --rpc-url sepolia --account cryotrace-deployer --broadcast
   ```

## Signed event (EIP-712)

Domain: name `CryoTrace`, version `1`, chain id, contract address.

```
ColdChainEvent(bytes32 boxId,bytes32 batchId,uint8 kind,uint8 anomalyType,
               uint64 startTime,uint64 endTime,int16 temperature,int16 peak,uint32 counter)
```

Any change to this struct changes what the firmware signs.
`test_DigestMatchesEip712Spec` spells out the exact bytes.
