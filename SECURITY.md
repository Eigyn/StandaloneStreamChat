# Security policy

## Supported versions

Security fixes target the latest StandaloneStreamChat release available for
each maintained Quest game version.

| Beat Saber target | Security support |
| --- | --- |
| Standalone Quest 1.40.8 | Latest compatible StandaloneStreamChat release |
| Standalone Quest 1.45.3 | Latest compatible StandaloneStreamChat release |
| Other game versions and legacy PC builds | Unsupported |

When possible, confirm the issue with the latest release for your game version.
Reports affecting older releases are still welcome if the issue may remain in
the maintained code.

## Reporting a vulnerability

Use GitHub's private reporting form through
[Report a vulnerability](https://github.com/Eigyn/StandaloneStreamChat/security/advisories/new).
The form is available when private vulnerability reporting is enabled for the
repository. It can also be reached from the repository's Security tab under
Advisories.

If the form is unavailable, open an
[issue](https://github.com/Eigyn/StandaloneStreamChat/issues/new) titled
`Request for private security contact`. Include only that you need a confidential
reporting channel, and wait for the maintainer to arrange one. Keep vulnerability
details, exploit code, credentials, and sensitive logs out of public issues and
pull requests.

Include the following in the private report.

- A summary of the vulnerability and its likely impact.
- Affected mod and Beat Saber versions, Quest model, and relevant dependencies.
- Reproduction steps and a minimal proof of concept, if available.
- Sanitized logs or example input, such as chat content or a download response.
- Any known workaround or suggested fix.

Use accounts and devices you control when reproducing an issue. Remove access
tokens, cookies, authorization headers, and private user information from shared
evidence.

## Handling reports

The maintainer will assess the report, request clarification when needed, and
coordinate a fix and disclosure through the private reporting channel. Response
and fix times depend on availability and the issue's complexity. Please
coordinate public disclosure with the maintainer so users can receive a fix.

Ordinary rendering problems, connection failures, and song-request bugs belong
in [GitHub Issues](https://github.com/Eigyn/StandaloneStreamChat/issues). Use the
private reporting process when the issue could compromise user data, accounts,
or the device, including through untrusted chat content, images, or downloads.
