# Venera CLI Wallet

A lightweight command-line wallet for **Venera**, written in C++.

Venera CLI provides access to VNR wallet functionality, USDT/USDC balances and transfers, staking, Burn-to-Earn rewards, transaction history, and encrypted local wallet files directly from the terminal.

```text
 __      __
 \ \    / /
  \ \  / /__ _ __   ___ _ __ __ _
   \ \/ / _ \ '_ \ / _ \ '__/ _` |
    \  /  __/ | | |  __/ | | (_| |
     \/ \___|_| |_|\___|_|  \__,_|

          Venera CLI v0.2.1
```

## Features

- Create a new Venera wallet
- Restore from a 25-word Venera mnemonic
- Open password-encrypted `.keys` wallet files
- AES-256-GCM local wallet encryption
- VNR balance and receive address
- USDT balance and receive address
- USDC balance and receive address
- Send VNR
- Send USDT
- Send USDC
- Stake VNR
- Burn VNR
- Burn Points / BP statistics
- Burn-to-Earn USDT yield tracking
- Collect accumulated USDT yield
- Transaction history
- Manual wallet refresh
- 2FA support
- Arrow-key command history
- Transfer confirmation prompts

## Requirements

Venera CLI requires:

There are no requirements for release binaries. Compiled binaries are fully static.
Originally built with:
- Linux
- C++17 compiler
- libcurl
- OpenSSL
- json-c
- pthread

Run:

```bash
./venera-cli
```

## Opening a Wallet

The CLI supports three wallet sources.

### Restore from mnemonic

Start the wallet and enter your 25-word Venera seed.

The wallet is restored through the Venera network infrastructure.

### Create a wallet

Use:

```text
create
```

when prompted for the wallet source.

The CLI creates a new deterministic Venera wallet and displays the generated 25-word mnemonic.

**Back up your mnemonic securely. Losing it may permanently remove access to your wallet.**

### Encrypted `.keys` wallet

After entering or creating a mnemonic, the CLI can optionally save it locally:

```text
Save encrypted wallet file? [Y/n]:
```

The wallet file is encrypted using:

- AES-256-GCM
- PBKDF2-HMAC-SHA256
- Random salt
- Random IV
- Authentication tag

The wallet password is used only to decrypt the local wallet file.

There is currently **no inactivity password lockout**. Once the wallet has been unlocked, it remains unlocked for the lifetime of the CLI process.

## Commands

Run:

```text
help
```

to display the available commands.

### Balance

```text
balance
```

Displays:

- VNR
- USDT
- USDC

Example:

```text
venera> balance

Balances
  VNR     12,450.392100000
  USDT    15.42000000
  USDC    2.00000000
```

A loading indicator is displayed while the wallet API request is processing.

## Receive

Display your receive address:

```text
receive vnr
receive usdt
receive usdc
```

USDT and USDC use the wallet's associated EVM/BSC address.

## Send

### VNR

```text
send vnr <address> <amount>
```

Example:

```text
send vnr VENeRa... 25
```

### USDT

```text
send usdt <address> <amount>
```

Example:

```text
send usdt 0x1234... 5
```

### USDC

```text
send usdc <address> <amount>
```

Example:

```text
send usdc 0x1234... 10
```

Before broadcasting a transaction, the wallet displays the destination and amount and asks for confirmation:

```text
Send this transfer? [y/N]:
```

No transfer is submitted unless confirmed.

## Staking

Stake VNR with one of the supported lock periods:

```text
stake <amount> <days>
```

Supported periods:

| Lock | Reward |
|---:|---:|
| 30 days | 1.3% |
| 60 days | 2.8% |
| 120 days | 5.3% |
| 360 days | 18% |

Example:

```text
stake 1000 360
```

The CLI submits the stake to the Venera staking backend and displays:

- Transaction hash
- Amount staked
- Expected amount output
- Unlock time
- Total staked amounts
- Total expected output

A confirmation prompt is shown before the staking transaction is submitted.

## Burn VNR

Burn VNR permanently:

```text
burn <amount>
```

Example:

```text
burn 250
```

The CLI asks for confirmation:

```text
Burn 250 VNR? [y/N]:
```

Burning removes VNR from circulation and generates Burn Points according to the active Burn-to-Earn reward model.

## Burn-to-Earn Statistics

Use:

```text
burnstats
```

Aliases:

```text
bp
yield
```

Example:

```text
Burn-to-Earn

  Burned VNR          3833
  Burn transactions  4
  Burn Points         388702.142857143 BP
  Available Yield     0.31482715 USDT
  Earned Total        1.92837162 USDT

Network
  Current BP / VNR    101.41
  Rolling 7d Burn     4133 VNR
  Target 7d Burn      4250 VNR
  Full Weight         7 days
  BP Expiry           30 days
```

### Burn Points

Burn Points represent a wallet's weighted contribution to Venera's Burn-to-Earn system.

The dynamic burn rate may change depending on network burn activity.

## Collect USDT Yield

Collect accumulated Burn-to-Earn yield:

```text
collectyield
```

Alias:

```text
collect
```

The wallet asks for confirmation before requesting the payout.

Example:

```text
venera> collectyield

Available yield: 0.31482715 USDT
Collect available USDT yield? [y/N]: y

Collecting USDT yield...

Yield collected  0.31482715 USDT
To:      0x...
TX hash: 0x...
```

Yield payouts are made over BNB Smart Chain using USDT.

## Transaction History

```text
history
```

Displays up to the latest 100 wallet transactions.

History uses terminal colors for quick identification:

- **Green** — received transactions
- **Red** — sent transactions
- **Yellow** — burns
- **Cyan** — staking-related transactions

Example:

```text
TYPE       AMOUNT              DATE                  TX HASH

RECEIVE    +500.000000000 VNR  2026-09-29 20:41     ...
SEND       -25.000000000 VNR   2026-09-29 18:13     ...
BURN       -100.000000000 VNR  2026-09-29 14:52     ...
```

## Refresh

Manually refresh the wallet:

```text
refresh
```

This synchronizes the wallet with the Venera blockchain using a trusted remote node. Local nodes will be available in the upcoming updates.

The CLI performs an initial refresh when the wallet is opened.

There is currently **no recurring background auto-refresh** inside the CLI.

This avoids background wallet synchronization interfering with commands that are actively using the same wallet session.

Run `refresh` whenever you want to force a new wallet synchronization.

## Command History

The CLI includes interactive terminal history.

Use:

```text
↑
```

to recall previous commands and:

```text
↓
```

## 2FA

Venera CLI supports wallets with Venera 2FA enabled previously in the web wallet, turned off by default.

If enabled:

```text
OTP Code:
```

## Wallet Sessions

Wallet sessions automatically expire after 2 hours and requires restoring the wallet key and entering password again.

### Wallet Data

When using an encrypted `.keys` file, the wallet data is stored locally in encrypted form.

For maximum security:

- Protect your `.keys` file
- Use a strong wallet password
- Never share your 25-word mnemonic
- Never paste your mnemonic into untrusted software
- Do not run unofficial builds of the CLI
- Keep your operating system secure

### Transaction Confirmation

All transfer CLI commands require interactive confirmation before submission.

This includes:

- VNR transfers
- USDT transfers
- USDC transfers
- VNR burns
- VNR staking
- USDT yield collection

## Supported Assets

| Asset | Balance | Receive | Send |
|---|:---:|:---:|:---:|
| VNR | ✓ | ✓ | ✓ |
| USDT | ✓ | ✓ | ✓ |
| USDC | ✓ | ✓ | ✓ |

USDT and USDC functionality currently targets BNB Smart Chain.

## Example Session

```text
$ ./venera-cli

 __      __
 \ \    / /
  \ \  / /__ _ __   ___ _ __ __ _
   \ \/ / _ \ '_ \ / _ \ '__/ _` |
    \  /  __/ | | |  __/ | | (_| |
     \/ \___|_| |_|\___|_|  \__,_|

          Venera CLI v0.2.1

Wallet file, 25-word seed, or 'create':
> wallet.keys

Wallet password:
Loading...
Restoring wallet...
Synchronizing wallet...

Wallet ready.

venera> balance
Loading [==========          ]

Balances
  VNR     4210.500000000
  USDT    3.42000000
  USDC    0.00000000

venera> burnstats

Burn-to-Earn
  Burn Points         152483 BP
  Estimated Daily     0.08431248 USDT
  Reward Share        0.08431248
  Available Yield     0.18329120 USDT

venera> history
```

## Available Commands

```text
balance
receive <vnr|usdt|usdc>
send <vnr|usdt|usdc> <address> <amount>

stake <amount> <30|60|120|360>

burn <amount>
burnstats
collectyield

history
refresh

help
exit
```

Aliases:

```text
bp       -> burnstats
yield    -> burnstats
collect  -> collectyield
quit     -> exit
```

## Disclaimer

Venera CLI is wallet software that can control real digital assets.

Always verify:

- Recipient addresses
- Transfer amounts
- Network
- Wallet backups
- Transaction details

before confirming a transaction.

Transactions submitted to a blockchain may be irreversible.
