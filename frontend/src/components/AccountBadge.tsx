import { type Address, hexToBytes, keccak256 } from "viem";
import { AddressLink } from "./ui";

/** Deterministic 5x5 mirrored avatar for an address (same address, same picture). */
export function Identicon({ address, size = 28 }: { address: Address; size?: number }) {
  const bytes = hexToBytes(keccak256(address.toLowerCase() as Address));
  const hue = Math.round((bytes[0] / 255) * 360);
  const cells = [];
  for (let row = 0; row < 5; row++) {
    for (let col = 0; col < 3; col++) {
      if (bytes[1 + row * 3 + col] % 2 === 0) continue;
      cells.push(<rect key={`${row}-${col}`} x={col} y={row} width={1} height={1} />);
      if (col < 2) cells.push(<rect key={`${row}-${4 - col}`} x={4 - col} y={row} width={1} height={1} />);
    }
  }
  return (
    <svg
      className="identicon"
      width={size}
      height={size}
      viewBox="-0.5 -0.5 6 6"
      aria-hidden
      style={{ background: `hsl(${(hue + 180) % 360} 45% 88%)` }}
    >
      <g fill={`hsl(${hue} 65% 42%)`} shapeRendering="crispEdges">
        {cells}
      </g>
    </svg>
  );
}

export function AccountBadge({ address, roles }: { address: Address; roles: string[] }) {
  return (
    <div className="account">
      <div className="account-text">
        <AddressLink address={address} />
        <span className="muted small">{roles.length ? roles.join(" · ") : "no roles"}</span>
      </div>
      <Identicon address={address} size={36} />
    </div>
  );
}
