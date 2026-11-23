import { type ReactNode, useState } from "react";
import { type Address, type Hex, isAddress, zeroAddress } from "viem";
import { type Wallet, contract, describeError, publicClient, sendContractTx } from "../lib/chain";
import { CONTRACT_ADDRESS, chain } from "../lib/config";
import { anomalyName, formatDeviceTime, formatTemp, kindName } from "../lib/format";
import { idOf, labelOf } from "../lib/names";
import {
  PayloadError,
  type SignedEvent,
  eventDigest,
  eventRuleViolation,
  parsePayload,
  recoverSigner,
} from "../lib/payload";
import { AddressLink, Badge, Notice, TxStatus, useTx } from "./ui";

/**
 * Shared test vector (contracts/test/FirmwareVector.t.sol, test/host/test_signed_event.c):
 * signed by the firmware code with key 0xC0FFEE for a test contract address, so it only
 * verifies against that domain.
 */
const VECTOR_CONTRACT = "0xC0FFEE00000000000000000000000000c0FFEe00" as Address;
const VECTOR_PAYLOAD = JSON.stringify(
  {
    boxId: idOf("box01"),
    batchId: idOf("batch-demo-001"),
    kind: 1, // recovery
    anomalyType: 1, // above range
    startTime: 1767225600000,
    endTime: 1767225615000,
    temperature: 790,
    peak: 1121,
    counter: 42,
    signature:
      "0x08a7c85b3031fc6059aec942042ef1e10e1bc7b17ecf9fa752a3a1fc44c74cb55a2f3eca400efae2f3591feba1e1aad4e5dcd42b02381dad3d2a12883133eee81c",
  },
  null,
  2,
);

type Check = { ok: boolean | null; label: string; detail?: ReactNode };

interface Report {
  signed: SignedEvent;
  digest: Hex;
  signer: Address;
  checks: Check[];
  liveDomain: boolean;
}

async function verify(text: string, verifyingContract: Address): Promise<Report> {
  const signed = parsePayload(text);
  const ev = signed.event;
  const digest = eventDigest(ev, chain.id, verifyingContract);
  const signer = await recoverSigner(signed, chain.id, verifyingContract);
  const rule = eventRuleViolation(ev);
  const checks: Check[] = [
    { ok: true, label: "Payload format", detail: "all fields present, with valid types and ranges" },
    { ok: rule === null, label: "Event rules", detail: rule ?? "kind, type and times are consistent" },
  ];

  const liveDomain = verifyingContract.toLowerCase() === CONTRACT_ADDRESS.toLowerCase();
  if (!liveDomain) {
    checks.push({
      ok: null,
      label: "On-chain checks",
      detail: "skipped: the event was verified against another contract address, not the deployed one",
    });
    return { signed, digest, signer, checks, liveDomain };
  }

  const [device, boxOfBatch, recorded] = await Promise.all([
    publicClient.readContract({ ...contract, functionName: "deviceOf", args: [ev.boxId] }),
    publicClient.readContract({ ...contract, functionName: "boxOfBatch", args: [ev.batchId] }),
    publicClient.readContract({ ...contract, functionName: "recorded", args: [digest] }),
  ]);
  checks.push(
    device === zeroAddress
      ? { ok: false, label: "Signed by the box's device", detail: `box ${labelOf(ev.boxId)} has no device registered` }
      : {
          ok: device === signer,
          label: "Signed by the box's device",
          detail:
            device === signer ? (
              <>
                signer is the registered key of box {labelOf(ev.boxId)}: <AddressLink address={device} />
              </>
            ) : (
              <>
                signer <AddressLink address={signer} /> is not the registered key <AddressLink address={device} />: the
                event was altered, or signed by another key
              </>
            ),
        },
    {
      ok: boxOfBatch === ev.boxId,
      label: "Batch is on this box",
      detail:
        boxOfBatch === ev.boxId
          ? `batch ${labelOf(ev.batchId)} is assigned to box ${labelOf(ev.boxId)}`
          : `batch ${labelOf(ev.batchId)} is not assigned to box ${labelOf(ev.boxId)}`,
    },
    {
      ok: null,
      label: "Already on-chain",
      detail: recorded ? <Badge tone="ok">yes, recorded</Badge> : <Badge tone="warn">not yet</Badge>,
    },
  );
  return { signed, digest, signer, checks, liveDomain };
}

function mark(ok: boolean | null) {
  return ok === null ? <span className="mark info">i</span> : ok ? <span className="mark ok">✓</span> : <span className="mark bad">✗</span>;
}

export function VerifyPanel({ wallet }: { wallet: Wallet | null }) {
  const [text, setText] = useState("");
  const [domainContract, setDomainContract] = useState<string>(CONTRACT_ADDRESS);
  const [report, setReport] = useState<Report | null>(null);
  const [error, setError] = useState<string | null>(null);
  const tx = useTx();

  async function check() {
    setReport(null);
    setError(null);
    if (!isAddress(domainContract)) {
      setError("The contract address of the signing domain is not a valid address.");
      return;
    }
    try {
      setReport(await verify(text, domainContract));
    } catch (err) {
      setError(err instanceof PayloadError ? `Invalid payload: ${err.message}` : describeError(err));
    }
  }

  function loadVector() {
    setText(VECTOR_PAYLOAD);
    setDomainContract(VECTOR_CONTRACT);
    setReport(null);
    setError(null);
  }

  async function submit() {
    if (!wallet || !report) return;
    const { event, signature } = report.signed;
    const ok = await tx.run((onSent) => sendContractTx(wallet, "recordEvent", [event, signature], onSent));
    if (ok) await check();
  }

  const allPass = report?.liveDomain && report.checks.every((c) => c.ok !== false);

  return (
    <section className="panel">
      <h2>Verify a signed event</h2>
      <p className="muted">
        Paste the JSON a box publishes on <code>cryotrace/&lt;box&gt;/anomaly</code>. The signature is checked here in
        the browser, independently of the relayer. Change any field (say, the temperature) and the signer no longer
        matches the box.
      </p>
      <textarea
        value={text}
        onChange={(e) => setText(e.target.value)}
        rows={13}
        spellCheck={false}
        placeholder='{"boxId": "0x…", "batchId": "0x…", "kind": 0, …, "signature": "0x…"}'
        aria-label="Signed event JSON"
      />
      <details>
        <summary>Signing domain</summary>
        <p className="muted small">
          A signature is only valid for one network and one contract (EIP-712 domain "CryoTrace" v1, chain{" "}
          {chain.id}). Change the contract address only to check events signed for another deployment.
        </p>
        <input value={domainContract} onChange={(e) => setDomainContract(e.target.value)} aria-label="Domain contract" />
      </details>
      <div className="row">
        <button onClick={check} disabled={!text.trim()}>
          Verify
        </button>
        <button className="secondary" onClick={loadVector}>
          Load firmware test vector
        </button>
      </div>

      {error && <Notice tone="bad">{error}</Notice>}

      {report && (
        <>
          <ul className="checks">
            {report.checks.map((c) => (
              <li key={c.label}>
                {mark(c.ok)}
                <div>
                  <b>{c.label}</b>
                  {c.detail && <div className="muted small">{c.detail}</div>}
                </div>
              </li>
            ))}
          </ul>
          <div className="table-wrap">
            <table className="kv">
              <tbody>
                <tr>
                  <th>EIP-712 digest</th>
                  <td className="mono break">{report.digest}</td>
                </tr>
                <tr>
                  <th>Recovered signer</th>
                  <td className="mono break">{report.signer}</td>
                </tr>
                <tr>
                  <th>Box / batch</th>
                  <td>
                    {labelOf(report.signed.event.boxId)} / {labelOf(report.signed.event.batchId)}
                  </td>
                </tr>
                <tr>
                  <th>Event</th>
                  <td>
                    {kindName(report.signed.event.kind)} · {anomalyName(report.signed.event.anomalyType)} · counter{" "}
                    {report.signed.event.counter}
                  </td>
                </tr>
                <tr>
                  <th>Start / end</th>
                  <td>
                    {formatDeviceTime(report.signed.event.startTime)} /{" "}
                    {report.signed.event.kind === 0 ? "—" : formatDeviceTime(report.signed.event.endTime)}
                  </td>
                </tr>
                <tr>
                  <th>Reading / peak</th>
                  <td>
                    {formatTemp(report.signed.event.temperature)} / {formatTemp(report.signed.event.peak)}
                  </td>
                </tr>
              </tbody>
            </table>
          </div>

          {report.liveDomain && (
            <div className="relay">
              <h3>Submit it yourself</h3>
              <p className="muted small">
                The contract accepts a signed event from anyone, so if every relayer is down or censoring, any wallet
                can record it. The contract runs the same checks; a refused event is caught before you sign, at no
                cost.
              </p>
              <button onClick={submit} disabled={!wallet || tx.busy}>
                {wallet ? "Record on-chain (pay the gas)" : "Connect a wallet to submit"}
              </button>
              {!allPass && wallet && <span className="muted small"> (some checks failed: expect a refusal)</span>}
              <TxStatus state={tx.state} />
            </div>
          )}
        </>
      )}
    </section>
  );
}

