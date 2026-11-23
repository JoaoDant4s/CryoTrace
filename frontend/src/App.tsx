import { useCallback, useEffect, useState } from "react";
import { getAddress } from "viem";
import { AccountBadge } from "./components/AccountBadge";
import { ActivityPanel } from "./components/ActivityPanel";
import { AuditPanel } from "./components/AuditPanel";
import { ManagePanel, type RoleFlags } from "./components/ManagePanel";
import { RegistryPanel } from "./components/RegistryPanel";
import { AddressLink, Notice } from "./components/ui";
import { VerifyPanel } from "./components/VerifyPanel";
import {
  type Wallet,
  connectWallet,
  contract,
  describeError,
  hasBrowserWallet,
  publicClient,
  switchToAppChain,
} from "./lib/chain";
import { CONTRACT_ADDRESS, chain } from "./lib/config";
import { ROLES } from "./lib/roles";
import { refreshContractLogs } from "./lib/useContractLogs";

const TABS = [
  { id: "audit", label: "Audit" },
  { id: "registry", label: "Registry" },
  { id: "activity", label: "Activity" },
  { id: "verify", label: "Verify event" },
  { id: "manage", label: "Manage" },
] as const;
type TabId = (typeof TABS)[number]["id"];

function tabFromHash(): TabId {
  const id = window.location.hash.slice(1).split("?")[0];
  return TABS.some((t) => t.id === id) ? (id as TabId) : "audit";
}

export function App() {
  const [tab, setTab] = useState<TabId>(tabFromHash);
  const [wallet, setWallet] = useState<Wallet | null>(null);
  const [roles, setRoles] = useState<RoleFlags>({});
  const [walletError, setWalletError] = useState<string | null>(null);

  useEffect(() => {
    const onHash = () => setTab(tabFromHash());
    window.addEventListener("hashchange", onHash);
    return () => window.removeEventListener("hashchange", onHash);
  }, []);

  const loadRoles = useCallback(async () => {
    if (!wallet) return setRoles({});
    try {
      const flags = await Promise.all(
        ROLES.map((r) =>
          publicClient.readContract({ ...contract, functionName: "hasRole", args: [r.id, wallet.account] }),
        ),
      );
      setRoles(Object.fromEntries(ROLES.map((r, i) => [r.id, flags[i]])));
    } catch (err) {
      setWalletError(describeError(err));
    }
  }, [wallet]);

  useEffect(() => {
    void loadRoles();
  }, [loadRoles]);

  // Follow account / network changes made in the wallet.
  useEffect(() => {
    if (!wallet) return;
    const eth = wallet.provider;
    const onAccounts = (accounts: string[]) =>
      setWallet((w) => (w && accounts.length ? { ...w, account: getAddress(accounts[0]) } : null));
    const onChain = (id: string) => setWallet((w) => (w ? { ...w, onChain: Number(id) === chain.id } : null));
    eth.on("accountsChanged", onAccounts);
    eth.on("chainChanged", onChain);
    return () => {
      eth.removeListener("accountsChanged", onAccounts);
      eth.removeListener("chainChanged", onChain);
    };
    // re-subscribe only when the provider changes, not on every account/network update
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [wallet?.provider]);

  function onChanged() {
    void loadRoles();
    refreshContractLogs();
  }

  async function connect() {
    setWalletError(null);
    try {
      setWallet(await connectWallet());
    } catch (err) {
      setWalletError(describeError(err));
    }
  }

  async function switchNetwork() {
    if (!wallet) return;
    setWalletError(null);
    try {
      await switchToAppChain(wallet);
      setWallet({ ...wallet, onChain: true });
    } catch (err) {
      setWalletError(describeError(err));
    }
  }

  const heldRoles = ROLES.filter((r) => roles[r.id]).map((r) => r.name);

  return (
    <>
      <header>
        <div className="brand">
          <span className="logo" aria-hidden>
            ❄
          </span>
          <div>
            <h1>CryoTrace</h1>
            <div className="muted small">
              Vaccine cold-chain audit · {chain.name} · contract <AddressLink address={CONTRACT_ADDRESS} />
            </div>
          </div>
        </div>
        <div className="wallet">
          {wallet ? (
            <AccountBadge address={wallet.account} roles={heldRoles} />
          ) : (
            <button onClick={connect} title={hasBrowserWallet() ? "" : "Install MetaMask"}>
              Connect wallet
            </button>
          )}
        </div>
      </header>

      <nav>
        {TABS.map((t) => (
          <a key={t.id} href={`#${t.id}`} className={tab === t.id ? "active" : ""}>
            {t.label}
          </a>
        ))}
      </nav>

      <main>
        {walletError && <Notice tone="bad">{walletError}</Notice>}
        {wallet && !wallet.onChain && (
          <Notice tone="warn">
            {wallet.name} is connected but on another network. Reading works; to send transactions,{" "}
            <button className="secondary" onClick={switchNetwork}>
              switch to {chain.name}
            </button>
          </Notice>
        )}
        {tab === "audit" && <AuditPanel />}
        {tab === "registry" && <RegistryPanel />}
        {tab === "activity" && <ActivityPanel />}
        {tab === "verify" && <VerifyPanel wallet={wallet} />}
        {tab === "manage" && <ManagePanel wallet={wallet} roles={roles} onChanged={onChanged} />}
      </main>

      <footer className="muted small">
        CryoTrace — IA376 Edge Computing (FEEC/Unicamp). Events are signed inside each box (ESP32-S3, EIP-712) and
        verified by the contract; this page only reads them.
      </footer>
    </>
  );
}
