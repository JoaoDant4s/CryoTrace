Signed event payload: 422 B; reading payload: 60 B.
Per message on the wire: reading (QoS 0) 198 B, event (QoS 1) 634 B, keep-alive 182 B.

| Strategy | Messages/day | Payload/day | Wire/day | Reduction vs every reading (wire) | Fleet of 100, 30 days (wire) |
|---|---|---|---|---|---|
| Every reading (2 Hz) | 172,800 | 10.4 MB | 34.2 MB | 1x | 103 GB |
| Periodic reading every 10 s | 8,640 | 518 kB | 1.7 MB | 20x | 5.1 GB |
| Signed events, 10 excursions/day | 20 | 8.4 kB | 12.7 kB | 2,698x | 38.0 MB |
| Signed events, 2 excursions/day | 4 | 1.7 kB | 2.5 kB | 13,491x | 7.6 MB |
| Keep-alive only, every 30 s | 2,880 | 0 B | 524 kB | 65x | 1.6 GB |
| Keep-alive only, every 5 min | 288 | 0 B | 52.4 kB | 653x | 157 MB |

On-chain (one transaction per message, recordEvent gas):

- Every reading (2 Hz): 30,702.2 Mgas/day = 11.8450 % of Ethereum's daily gas capacity (assumed 36 M gas per block, 7200 blocks/day)
- Periodic reading every 10 s: 1,535.1 Mgas/day = 0.5923 % of Ethereum's daily gas capacity (assumed 36 M gas per block, 7200 blocks/day)
- Signed events, 10 excursions/day: 3.6 Mgas/day = 0.0014 % of Ethereum's daily gas capacity (assumed 36 M gas per block, 7200 blocks/day)
- Signed events, 2 excursions/day: 0.7 Mgas/day = 0.0003 % of Ethereum's daily gas capacity (assumed 36 M gas per block, 7200 blocks/day)
