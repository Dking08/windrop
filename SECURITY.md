# Security Policy

## Supported versions

Security fixes are provided for the latest release only. Please update to the newest version before reporting.

| Version | Supported |
| :--- | :---: |
| Latest release (2.1.x) | Yes |
| Older releases | No |

## Reporting a vulnerability

Please **do not** open a public issue for a security problem.

Report it privately through GitHub:

1. Go to the [Security tab](https://github.com/Dking08/windrop/security) of this repository.
2. Choose **Report a vulnerability**, or use [this direct link](https://github.com/Dking08/windrop/security/advisories/new).

Helpful details to include:

- The Windrop version (`windrop --version`) and your Windows version
- Steps to reproduce, and a proof of concept if you have one
- What you expect the impact to be

You will get an acknowledgment as soon as I can manage, and I will keep you updated as I investigate. Windrop is maintained by one person, so please allow a reasonable amount of time. Once a fix is released, the advisory will be published and reporters are credited unless they prefer to stay anonymous.

## Scope

Examples of issues in scope:

- Memory-safety bugs (crashes, overflows, out-of-bounds access) triggered by crafted file paths, wildcards, or command-line arguments
- Windrop exposing more data to a drop target than documented in the [Privacy Policy](PRIVACY.md)
- Any unexpected file access, network activity, or persistence
- Misuse of the `F8`/`Esc` hotkeys or synthetic mouse input beyond what is documented

Out of scope:

- What a drop target does with files after you drop them onto it
- Actions that require you to deliberately drop a file onto an untrusted application
- Issues that require an attacker to already have code execution or administrator rights on the machine

## Verifying releases

Windrop is built with deterministic compiler and linker flags (see `CMakeLists.txt` and `Architecture.md`), so you can rebuild from source and compare the binary hash against a release.
