import { useEffect, useState } from "react";
import { type Address, type Hex, isAddress, zeroAddress, zeroHash } from "viem";
import { type Wallet, contract, publicClient, sendContractTx } from "../lib/chain";
import { idOf, labelOf, remember } from "../lib/names";
import { ADMIN_ROLE, LOGISTICS_ROLE, PROVISIONER_ROLE, ROLES } from "../lib/roles";
import { AddressLink, Notice, TxStatus, useTx } from "./ui";

export type RoleFlags = Record<Hex, boolean>;

interface Props {
  wallet: Wallet | null;
  roles: RoleFlags;
  onChanged: () => void;
}

/** Re-reads a value whenever its key changes (debounced while typing). */
function useLookup<T>(key: string, read: (key: string) => Promise<T>, refresh: number): T | undefined {
  const [value, setValue] = useState<T>();
  useEffect(() => {
    setValue(undefined);
    if (!key) return;
    let cancelled = false;
    const timer = setTimeout(() => {
      read(key)
        .then((v) => !cancelled && setValue(v))
        .catch(() => {});
    }, 400);
    return () => {
      cancelled = true;
      clearTimeout(timer);
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [key, refresh]);
  return value;
}

const readDevice = (box: string) =>
  publicClient.readContract({ ...contract, functionName: "deviceOf", args: [idOf(box)] });
const readBoxOfBatch = (batch: string) =>
  publicClient.readContract({ ...contract, functionName: "boxOfBatch", args: [idOf(batch)] });

function MissingRole({ role, wallet }: { role: string; wallet: Wallet | null }) {
  return (
    <p className="muted small">
      {wallet ? `The connected account does not have the ${role} role.` : `Connect a wallet with the ${role} role.`}
    </p>
  );
}

function RegisterDevice({ wallet, roles, onChanged }: Props) {
  const [box, setBox] = useState("box01");
  const [device, setDevice] = useState("");
  const [refresh, setRefresh] = useState(0);
  const tx = useTx();
  const current = useLookup(box.trim(), readDevice, refresh);
  const allowed = roles[PROVISIONER_ROLE.id];

  async function submit(e: React.FormEvent) {
    e.preventDefault();
    if (!wallet) return;
    remember(box);
    const ok = await tx.run((onSent) =>
      sendContractTx(wallet, "registerDevice", [idOf(box), device.trim() as Address], onSent),
    );
    if (ok) {
      setRefresh((n) => n + 1);
      onChanged();
    }
  }

  return (
    <form className="subpanel" onSubmit={submit}>
      <h3>Register a box device</h3>
      <p className="muted small">
        Binds a box to the address its ESP32 prints on first boot (<code>device address (register in the contract)</code>
        ). Registering again replaces the key: events signed by the old key are refused from then on.
      </p>
      <label>
        Box ID
        <input value={box} onChange={(e) => setBox(e.target.value)} placeholder="box01" />
      </label>
      <p className="muted small">
        Currently:{" "}
        {current === undefined ? "…" : current === zeroAddress ? "no device registered" : <AddressLink address={current} full />}
      </p>
      <label>
        Device address
        <input value={device} onChange={(e) => setDevice(e.target.value)} placeholder="0x…" />
      </label>
      {device.trim() && !isAddress(device.trim()) && <p className="error small">Not a valid address.</p>}
      <button type="submit" disabled={!allowed || tx.busy || !box.trim() || !isAddress(device.trim())}>
        Register device
      </button>
      {!allowed && <MissingRole role="Provisioner" wallet={wallet} />}
      <TxStatus state={tx.state} />
    </form>
  );
}

function RegisterBatch({ wallet, roles, onChanged }: Props) {
  const [batch, setBatch] = useState("");
  const [box, setBox] = useState("box01");
  const [refresh, setRefresh] = useState(0);
  const tx = useTx();
  const currentBox = useLookup(batch.trim(), readBoxOfBatch, refresh);
  const boxDevice = useLookup(box.trim(), readDevice, refresh);
  const allowed = roles[LOGISTICS_ROLE.id];
  const taken = currentBox !== undefined && currentBox !== zeroHash;

  async function submit(e: React.FormEvent) {
    e.preventDefault();
    if (!wallet) return;
    remember(batch);
    remember(box);
    const ok = await tx.run((onSent) => sendContractTx(wallet, "registerBatch", [idOf(batch), idOf(box)], onSent));
    if (ok) {
      setRefresh((n) => n + 1);
      onChanged();
    }
  }

  return (
    <form className="subpanel" onSubmit={submit}>
      <h3>Assign a batch to a box</h3>
      <p className="muted small">
        One batch per trip: done once, when the batch is packed. The box is reused for later batches. The box firmware
        must be set to the same batch ID.
      </p>
      <label>
        Batch ID
        <input value={batch} onChange={(e) => setBatch(e.target.value)} placeholder="e.g. batch-2026-11-001" />
      </label>
      {taken && (
        <p className="error small">Already assigned to box {labelOf(currentBox)}: a batch can only be assigned once.</p>
      )}
      <label>
        Box ID
        <input value={box} onChange={(e) => setBox(e.target.value)} placeholder="box01" />
      </label>
      {boxDevice === zeroAddress && <p className="error small">This box has no device registered yet.</p>}
      <button type="submit" disabled={!allowed || tx.busy || !batch.trim() || !box.trim() || taken}>
        Assign batch
      </button>
      {!allowed && <MissingRole role="Logistics" wallet={wallet} />}
      <TxStatus state={tx.state} />
    </form>
  );
}

function ManageRoles({ wallet, roles, onChanged }: Props) {
  const [role, setRole] = useState<Hex>(PROVISIONER_ROLE.id);
  const [account, setAccount] = useState("");
  const [refresh, setRefresh] = useState(0);
  const tx = useTx();
  const valid = isAddress(account.trim());
  const has = useLookup(
    valid ? `${role}:${account.trim()}` : "",
    (key) => {
      const [r, a] = key.split(":");
      return publicClient.readContract({ ...contract, functionName: "hasRole", args: [r as Hex, a as Address] });
    },
    refresh,
  );
  const allowed = roles[ADMIN_ROLE.id];

  async function send(fn: "grantRole" | "revokeRole") {
    if (!wallet) return;
    const ok = await tx.run((onSent) => sendContractTx(wallet, fn, [role, account.trim() as Address], onSent));
    if (ok) {
      setRefresh((n) => n + 1);
      onChanged();
    }
  }

  return (
    <div className="subpanel">
      <h3>Roles</h3>
      <p className="muted small">
        Give the provisioner and logistics roles to the people (or systems) that do those jobs. Relayers need no role:
        anyone may submit a signed event.
      </p>
      <label>
        Role
        <select value={role} onChange={(e) => setRole(e.target.value as Hex)}>
          {ROLES.map((r) => (
            <option key={r.id} value={r.id}>
              {r.name} — {r.does}
            </option>
          ))}
        </select>
      </label>
      <label>
        Account
        <input value={account} onChange={(e) => setAccount(e.target.value)} placeholder="0x…" />
      </label>
      {valid && <p className="muted small">Currently: {has === undefined ? "…" : has ? "has this role" : "does not have it"}</p>}
      <div className="row">
        <button onClick={() => send("grantRole")} disabled={!allowed || tx.busy || !valid || has === true}>
          Grant
        </button>
        <button className="secondary" onClick={() => send("revokeRole")} disabled={!allowed || tx.busy || !valid || has === false}>
          Revoke
        </button>
      </div>
      {!allowed && <MissingRole role="Admin" wallet={wallet} />}
      <TxStatus state={tx.state} />
    </div>
  );
}

export function ManagePanel(props: Props) {
  return (
    <section className="panel">
      <h2>Manage</h2>
      <p className="muted">
        Provisioning and logistics, done with your browser wallet. Every change is a transaction on the public chain
        and appears in the activity log.
      </p>
      {!props.wallet && <Notice tone="info">Connect a wallet (top right) to make changes. Lookups work without one.</Notice>}
      <div className="grid">
        <RegisterDevice {...props} />
        <RegisterBatch {...props} />
        <ManageRoles {...props} />
      </div>
    </section>
  );
}
