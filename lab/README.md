# Windows Server laboratory

This directory contains an opt-in Windows QEMU supervisor and a deployment template for testing the C++ RDS provider in a **real Windows Server guest**. The guest booted with WHPX and verified the installed RD Session Host role and running service. Microsoft's guest-local RDP reference produced concurrent logons, an exported candidate probe and a separately verified manual same-boot sign-out/cleanup cycle. The connection helper still reaches its observation timeout. **RETSAR provider readiness and physical-seat creation remain unverified.** See the [reference results](RDP-REFERENCE.md) and [verified progress](../docs/PROGRESS.md).

The supervisor and answer template passed 39 inert Python tests, including simulated WHP capability responses. Two separate native checks terminated only newly created sleeping Python helper processes. The laboratory was shut down cleanly after its final reference checks.

[plan.json](plan.json) is a reusable deployment template, not a record of anyone's approval or computer. Its approval and guest deployment-state fields remain unapproved and unprovisioned defaults. The separate `validation` fields record observations from a controlled laboratory, including WHPX desktop boot and the bootstrap's role/service checks; those observations do not provision or authorize another deployment. Each deployment needs approval for its resource use and applicable evaluation terms; the repository does not supply that approval, installation media or credentials.

## Scope of the experiment

The proposed guest is Windows Server 2022 Standard Evaluation with **Desktop Experience** and the **RD Session Host** role. Role installation, registration of the test provider and any required reboots would occur inside the disposable guest.

A successful experiment would need to demonstrate a newly created RDS session, independently verify its identity and ownership, launch the probe in that exact session, and remove only the resources created by the test. An existing reference session must remain usable throughout the experiment. Return codes, mocks and device creation alone are insufficient evidence.

This experiment does **not** establish physical multiseat support on Windows 11 Home, ownership of physical monitors or input devices, hardware graphics acceleration, or compatibility with a particular game. Those require separate evidence.

## Fixed resource budget

| Resource | Allocation |
| --- | --- |
| Guest memory | 4 GiB |
| Virtual CPUs | 2 |
| Accelerator | TCG by default; WHPX only with explicit opt-in and an already available hypervisor |
| Host process priority | BelowNormal |
| Guest disk | Sparse qcow2, 64 GiB virtual capacity, no preallocation |
| Guest disk write throttle | 16 MiB/s |
| Allowed laboratory volumes | C: or D: |
| Desired free-space floor on C: and the laboratory volume | 25 GiB on each distinct volume |
| Supervisor stop threshold | At or below 26 GiB on either monitored volume |

TCG remains the default (`--accelerator tcg`), using `-accel tcg,thread=multi -cpu max`. The supervisor never switches accelerators automatically. TCG is supported on Windows, but boot time, responsiveness and CPU consumption have not been measured for this guest. BelowNormal priority and two virtual CPUs do not constitute a hard CPU-use ceiling. [QEMU accelerator documentation](https://www.qemu.org/docs/master/system/introduction.html).

`--accelerator whpx` is an explicit option for a Windows hypervisor that is already available. Before creating any child, the supervisor loads `WinHvPlatform.dll` with the System32-only search flag and makes the read-only `WHvGetCapability(WHvCapabilityCodeHypervisorPresent)` query. It requires `S_OK`, exactly four returned bytes and `BOOL TRUE`; a missing DLL/export, failed query, absent hypervisor or malformed response stops the launch. The query creates no partition, and the supervisor neither enables Windows features nor changes host services, drivers or boot settings. There is no automatic TCG fallback. [Microsoft capability API](https://learn.microsoft.com/en-us/virtualization/api/hypervisor-platform/funcs/whvgetcapability).

The WHPX command uses only `-accel whpx`, with no CPU-model or interrupt-controller override. The verified QEMU build's documentation states that x86_64 WHPX ignores `-cpu`; that command has now booted the selected guest to its desktop. The q35 machine, firmware, disk attachments, 4 GiB memory, two virtual CPUs, write throttle, private networking and job/storage guardians remain the same. Hardware CPU acceleration does not provide GPU passthrough or prove physical multiseat support. [Matching QEMU WHPX documentation](https://github.com/qemu/qemu/blob/54e84cdc7a/docs/system/whpx.rst).

A separate read-only host probe returned `S_OK`, `HypervisorPresent=TRUE` and four written bytes. This establishes capability availability on that laboratory host only. The 39 automated tests use fake WHP functions and do not query a real hypervisor or create partitions. Separately, the TCG guest shut down cleanly with QMP reason `guest-shutdown` and exit code zero, and its stopped disk/writable-firmware checkpoint was verified with SHA-256 before the real WHPX boot. That boot reached the desktop and the bootstrap's final role/service marker. No host configuration changes were made.

The WHPX run emitted the QEMU warning `Ignoring request for interrupt vector 0`. It did not prevent the observed desktop or `rdsh-configured` marker. Its implications for longer operation and other workloads remain unverified; the warning is not classified as harmless.

The fixed command uses q35, an emulated NVMe disk, UEFI firmware, emulated VGA and a virtual USB tablet. The exact QEMU build, its dependencies and firmware files must be verified for each deployment. Windows Setup and the corrected installed system have reached the guest desktop; workload compatibility and prolonged operation still require verification.

The Windows QEMU build rejected `dump-guest-core=off` with `Dumping guest memory cannot be disabled on this host`. That unsupported option was removed. The supervisor cannot enforce exclusion of guest memory from host crash dumps through that flag; no host crash-dump setting was changed. Startup stderr is retained even when QEMU exits before QMP becomes available.

### Storage monitoring and its limits

**64 GiB is the guest-visible capacity, not an initial 64 GiB allocation.** The disk can grow as the guest writes. Installation media, firmware variables, logs and any snapshots also consume host storage.

[supervise_qemu.py](supervise_qemu.py) permits the private laboratory root on C: or D:. It checks free space on both C: and the selected laboratory volume before launch and during its 200 ms polling loop; when they are the same volume, one check is sufficient. At or below 26 GiB on either monitored volume it terminates its own job, providing a 1 GiB margin above the desired 25 GiB floor. Disk-space errors, QMP failures and loss of the controller's standard-input channel also stop the guest. A watchdog terminates the job if the supervisor heartbeat becomes more than two seconds old. Guest disk writes are throttled to 16 MiB/s; this is not a cap on every host write, including firmware and logs.

This is a monitored target, **not a reservation or absolute guarantee**. Other applications can consume disk space, scheduling can delay a check, and buffered writes can continue briefly during termination. Native process cleanup was tested; VM disk-growth behavior under load has not been tested. Abrupt termination can leave the disposable guest disk unclean.

The supervisor does not download media or create disks. Provisioning tools must separately check the storage budget before downloads, copies and disk creation. No snapshots or additional disk copies are budgeted by default. The supervisor rejects qcow2 backing files, external data files, internal snapshots and an unexpected virtual capacity.

## Process ownership and controller

The supervisor creates its own QEMU child suspended, assigns it to an unnamed, non-inheritable Windows Job Object with `KILL_ON_JOB_CLOSE`, and only then resumes the primary thread. It owns process handles rather than discovering processes by PID. Cleanup affects that job and its descendants; it does not attach to existing QEMU processes. Closing the supervisor closes its job and terminates the guest. [Windows Job Object behavior](https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects).

QEMU starts with its guest CPUs paused until the supervisor completes QMP negotiation over private standard-input/output pipes. The controller must keep the supervisor's stdin open: **EOF stops the VM**. The JSON command allowlist supports status queries, guest keys and pointer events, guest power/reset commands, and captures with simple `.ppm` names inside the private reports directory. It does not expose arbitrary QMP, monitor commands, device hotplug or extra QEMU arguments.

The virtual guest serial port writes to `logs/guest-serial.log`; an existing file is refused at launch. The monitor stops the guest when this file reaches 16 MiB. This threshold can be exceeded between checks. It is an emulated port, not access to a host COM device. QEMU stderr is separately capped at 4 MiB.

Run the inert checks with `python -B -m unittest discover -s lab -p test_supervise_qemu.py -v`. The separate opt-in `python -B lab/guardian_smoke.py --run-owned-child-test` starts two disposable sleeping Python children to check job termination, kill on job closure, priority and non-inheritance. That native check is not a VM or RDS test.

## Host separation and access

The guest runs headless, without a local QEMU display window. Access is limited to:

- VNC listening on `127.0.0.1:5905`.
- RDP forwarded from `127.0.0.1:13389` to guest TCP port `3389`.

Both ports must be checked before launch; an occupied port must stop provisioning rather than redirect access to a different process. Networking uses QEMU's user-mode backend with restricted networking. This backend does not require an administrator-owned network adapter, and explicit forwarding can be bound to loopback. [QEMU networking documentation](https://www.qemu.org/docs/master/system/invocation.html).

The baseline provides no guest outbound Internet access. It creates no bridge or TAP adapter, installs no host drivers, changes no host services or hypervisor features, and does not restart the host. Only the guest may restart.

No physical disks or host devices are attached. There are no shared folders, clipboard integration or guest-agent integration. RDP and VNC clients must have clipboard transfer disabled; RDP drive and device redirection must also be disabled. Guest credentials must not be placed in command lines, logs, this plan or the repository.

All disk images, media, variable firmware files and test outputs belong in one private ordinary laboratory directory on C: or D:. The supervisor rejects other volumes before filesystem access and rejects reparse points. The plan contains only relative paths and placeholders. Resolve those placeholders privately; do not publish concrete host paths or add guest images to the repository. This laboratory path support does not change the transactional core: its journals and CLI report files remain restricted to C:.

## Unattended guest setup

[autounattend.xml.template](autounattend.xml.template) was used by Windows Setup in the guest. Read-only inspection of the failed guest's setup logs identified an invalid `FirstLogonCommands/SynchronousCommand/CommandLine` in `oobeSystem`, with error `0x80220005`. The original generated command was 1,318 characters; Microsoft limits this field to 1,024 characters. The corrected template has a fixed 309-character launcher instead of an encoded-command placeholder. An inert regression test checks that the launcher stays within the limit. The fresh corrected installation reached its first desktop and started [guest-bootstrap.ps1](guest-bootstrap.ps1). [CommandLine constraint](https://learn.microsoft.com/en-us/windows-hardware/customize/desktop/unattend/microsoft-windows-shell-setup-firstlogoncommands-synchronouscommand-commandline).

The observed serial records include schema `retsar.guest-bootstrap.v1`, phase `environment-verified`, computer `RETSARLAB`, Windows Server 2022 Standard Evaluation and `createdSeat=false`. After the WHPX desktop boot, rerunning the bootstrap inside the guest emitted `rdsh-configured` with `installed=true`, `serviceStatus=Running`, `providerRegistered=false` and `createdSeat=false`. This verifies the installed role and running service; it does not verify a successful client connection, an operational provider or session independence. The RDP reference experiment has not been executed.

A private builder must substitute only the selected image index and a guest-only administrator password, preserving the fixed launcher. The generated answer XML and optional answer ISO contain that password in plaintext. Read-only attachment does not encrypt them: keep both private, restrict access, and never commit, publish or include them in logs. Guest setup may retain copies inside the disposable disk. Disabling automatic logon does not certify that every credential copy has been erased.

Use `--installed-guest` when resuming an existing guest disk. This mode omits the bootable Windows installation ISO and selects disk boot; the optional nonbootable answer/bootstrap CD remains available. It does not repair a disk or certify that installation finished. Preserve the old serial log under a different name before a new launch: the supervisor refuses to overwrite it. Do not resume using installation media to work around an unexplained boot failure.

**The answer template wipes guest disk 0.** Its scope depends on using the supervisor's fixed attachment set: one disposable qcow2 guest disk, firmware and read-only ISO media, with no physical disk or host device attached. The later bootstrap checks do not protect this earlier Windows Setup operation. Do not reuse the answer file with a different disk configuration or run the bootstrap on the host. The template also accepts the evaluation EULA during setup; only generate and use a private answer ISO after the applicable terms have been approved.

Before changing guest settings, the bootstrap checks all three conditions: hostname `RETSARLAB`, QEMU manufacturer and Windows Server. It also checks the resource ceiling. These are deployment checks, not an authentication boundary. Only inside that gated guest does it install RD Session Host, arrange any needed guest reboot, configure guest RDP and disable automatic logon.

Its final marker is `rdsh-configured`: the role is checked as installed and `TermService` as running. The same record explicitly reports `providerRegistered=false` and `createdSeat=false`. **This marker does not establish provider readiness, session creation or isolation.**

## Media and licensing prerequisites

Use media from the [Microsoft Evaluation Center](https://www.microsoft.com/en-us/evalcenter/evaluate-windows-server-2022). The applicable terms must be reviewed and accepted before installation; this proposal does not accept them on anyone's behalf.

Microsoft states that the Server evaluation expires after 180 days and requires Internet activation within the first 10 days to avoid automatic shutdown. The baseline guest has no outbound access, so activation and updates require a separately agreed network arrangement before provisioning can be considered ready. The test launcher must not silently enable network access. [Evaluation conditions](https://www.microsoft.com/en-us/evalcenter/evaluate-windows-server-2022).

RDS has a separate initial licensing grace period of 120 days without a license server; valid RDS CALs are required after that period. This plan neither extends those periods nor treats evaluation as a permanent deployment license. [Microsoft RDS licensing](https://learn.microsoft.com/en-us/windows-server/remote/remote-desktop-services/rds-client-access-license).

QEMU is a separate tool under GPLv2, with component-specific licenses. Its binaries, firmware and dependencies must be obtained and verified independently; this repository does not redistribute them. [QEMU licensing](https://www.qemu.org/docs/master/about/license.html).

## Readiness checklist

- [ ] Resource consumption and evaluation terms approved.
- [ ] Official media obtained and verified; Desktop Experience selected.
- [ ] QEMU build, dependencies, firmware and guest boot configuration verified.
- [ ] Private C: or D: laboratory paths checked, with resource headroom on C: and the laboratory volume.
- [x] Supervisor, fixed launch configuration, simulated WHP guard and answer-template regression checks implemented; 39 inert Python tests passed.
- [x] Separate read-only WHP presence probe returned a valid positive capability result.
- [x] WHPX guest boot reached the desktop with the selected build; TCG remains the default.
- [ ] WHPX workload behavior and the observed interrupt-vector warning assessed.
- [x] Native job termination and kill-on-close checked with two owned helper children.
- [x] Failed guest setup diagnosed from read-only evidence; oversized first-logon command corrected.
- [x] Fresh corrected installation reached the first guest desktop.
- [x] Guest bootstrap entry and `environment-verified` serial evidence observed.
- [ ] VM storage-growth behavior under load verified.
- [ ] Loopback ports and client redirection settings checked.
- [ ] Activation and updates network arrangement agreed.
- [ ] Private answer ISO, credential handling and guest-disk-only wipe reviewed for this deployment.
- [x] `rdsh-configured` evidence confirms the installed role and running service after WHPX boot.
- [ ] Provider readiness verified separately from role installation and service status.
- [ ] Real session lifecycle experiment executed and evidence reviewed.

Checked items record component validations, guest desktop boot and the installed role/running service. They do not carry deployment approval. The reference client connection and real session lifecycle remain untested. GitHub compilation and unit tests do not complete either experiment.
