# CryoTrace frontend

A React app for auditing and operating the CryoTrace contract. Reads go straight
to the chain through a public RPC, so anyone can audit a batch without a wallet
and without trusting a backend. Transactions go through the browser wallet
(MetaMask).

| Tab | What it does | Wallet |
|---|---|---|
| **Audit** | Pick a batch ID and see its box and device, every signed event, the excursions (each anomaly paired with its recovery), and the total time outside 2–8 °C. Refreshes live. | no |
| **Registry** | Every registered box with its device key (and earlier keys, if rotated) and every batch with its box and the device keys that signed its events, with a link to its audit. Rebuilt from the contract's event log. | no |
| **Activity** | Every contract event since the deploy (device and batch registrations, recorded events, role changes), newest first, with transaction links. | no |
| **Verify event** | Paste the JSON a box publishes over MQTT. The browser recomputes the EIP-712 digest, recovers the signer and checks it against the box's registered device key, the batch assignment and whether the event is already on-chain. Can record the event from your own wallet: the contract accepts signed events from anyone, so this works even when every relayer is down or censoring. | only to submit |
| **Manage** | Register a box device key (Provisioner), assign a batch to a box (Logistics), grant and revoke roles (Admin). | yes |

Every transaction is simulated first: if the contract would refuse it
(missing role, batch already assigned, bad signature, …) the reason is shown
before the wallet asks for a signature, and no gas is spent.

## Run

Requires Node.js 18+.

```sh
npm install
npm run dev        # http://localhost:5173
npm run build      # static site in dist/ (can be hosted anywhere)
```

Configuration is optional: the defaults point to the Sepolia deployment. To change
it, copy `.env.example` to `.env`. Everything there is bundled into the page,
so the read RPC must not carry a private API key (e.g. your Infura key); the
default is a free public Sepolia node, which also keeps Infura credits for the
relayer.

### Against a local chain

```sh
anvil                                   # in another terminal
# deploy with ../contracts/script/Deploy.s.sol, then:
VITE_CHAIN_ID=31337 VITE_CONTRACT_ADDRESS=0x... npm run dev
```

## Notes

- **Names and hashes.** The contract stores `keccak256` of box and batch IDs
  (`"box01"` → `0x8c4d…`), as the firmware does. A hash cannot be turned back
  into its name, so the app labels hashes with the names typed in this browser
  and shows a shortened hash otherwise.
- **Pairing.** Events are ordered by the device counter (relayers may deliver
  them out of order). A recovery carries both the start and end time, so an
  excursion and its duration stay provable even if its anomaly event never
  arrived. Events built before the device clock synced have `startTime = 0`;
  their duration is shown as unknown.
- **RPC usage.** Live views poll every 30 s: the audit makes one `eventCount`
  call per poll and reloads only when it changes; the activity log asks only
  for blocks it has not seen.

## Test

```sh
npm test
```

- `payload.test.ts`: the signing vector shared with the firmware and the contract
  (same digest, same recovered device address), tampering and wrong-domain cases,
  and payload validation mirroring the relayer.
- `audit.test.ts`: excursion pairing (out of order, open, missing anomaly event,
  unsynced clock) and the exposure summary.
- `registry.test.ts`: boxes, key rotation, reused boxes and which key signed each batch's events.
- `abi.test.ts`: the ABI written in `src/lib/abi.ts` against the Foundry build
  (`forge build` in `../contracts`; skipped if it is missing).
