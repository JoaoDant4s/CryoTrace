import { type Hex, keccak256, toBytes, zeroHash } from "viem";

export interface Role {
  id: Hex;
  name: string;
  does: string;
}

export const ROLES: Role[] = [
  { id: zeroHash, name: "Admin", does: "grants and revokes roles" },
  { id: keccak256(toBytes("PROVISIONER_ROLE")), name: "Provisioner", does: "registers box device keys" },
  { id: keccak256(toBytes("LOGISTICS_ROLE")), name: "Logistics", does: "assigns batches to boxes" },
];

export const [ADMIN_ROLE, PROVISIONER_ROLE, LOGISTICS_ROLE] = ROLES;

export function roleName(id: Hex): string {
  return ROLES.find((r) => r.id === id.toLowerCase())?.name ?? `${id.slice(0, 10)}…`;
}
