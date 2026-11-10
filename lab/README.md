# Windows Server laboratory

This directory contains an opt-in Windows QEMU supervisor and a deployment template for testing the C++ RDS provider in a **real Windows Server guest**. The supervisor is implemented: 16 tests using a fake Windows API passed, and two native checks terminated only newly created sleeping Python helper processes. A QEMU startup attempt rejected an unsupported Windows option before guest boot. **Guest boot and guest session creation have not been demonstrated.**

[plan.json](plan.json) is a reusable deployment template, not a record of anyone's approval or computer. Its deployment-state fields start unapproved and unprovisioned. Each deployment needs approval for its resource use and applicable evaluation terms; the repository does not supply that approval, installation media or credentials.

## Scope of the experiment

The proposed guest is Windows Server 2022 Standard Evaluation with **Desktop Experience** and the **RD Session Host** role. Role installation, registration of the test provider and any required reboots would occur inside the disposable guest.

A successful experiment would need to demonstrate a newly created RDS session, independently verify its identity and ownership, launch the probe in that exact session, and remove only the resources created by the test. An existing reference session must remain usable throughout the experiment. Return codes, mocks and device creation alone are insufficient evidence.

This experiment does **not** establish physical multiseat support on Windows 11 Home, ownership of physical monitors or input devices, hardware graphics acceleration, or compatibility with a particular game. Those require separate evidence.

## Fixed resource budget

| Resource | Allocation |
| --- | --- |
| Guest memory | 4 GiB |
| Virtual CPUs | 2 |
| Accelerator | QEMU TCG software emulation |
| Host process priority | BelowNormal |
| Guest disk | Sparse qcow2, 64 GiB virtual capacity, no preallocation |
| Guest disk write throttle | 16 MiB/s |
| Desired free-space floor on C: | 25 GiB |
| Supervisor stop threshold on C: | At or below 26 GiB |

TCG is the baseline and fallback: the plan does not assume that WHPX or Hyper-V is usable and does not enable either feature. TCG is supported on Windows, but boot time, responsiveness and CPU consumption have not been measured for this guest. BelowNormal priority and two virtual CPUs do not constitute a hard CPU-use ceiling. [QEMU accelerator documentation](https://www.qemu.org/docs/master/system/introduction.html).

The fixed command uses q35, an emulated NVMe disk, UEFI firmware, emulated VGA and a virtual USB tablet. The exact QEMU build, its dependencies and firmware files must be verified for each deployment. Guest boot compatibility has not yet been demonstrated. An existing QEMU executable is not sufficient proof of that compatibility.

The Windows QEMU build rejected `dump-guest-core=off` with `Dumping guest memory cannot be disabled on this host`. That unsupported option was removed. The supervisor cannot enforce exclusion of guest memory from host crash dumps through that flag; no host crash-dump setting was changed. Startup stderr is retained even when QEMU exits before QMP becomes available.

### Storage monitoring and its limits

**64 GiB is the guest-visible capacity, not an initial 64 GiB allocation.** The disk can grow as the guest writes. Installation media, firmware variables, logs and any snapshots also consume host storage.

[supervise_qemu.py](supervise_qemu.py) checks free space on C: before launch and during its 200 ms polling loop. At or below 26 GiB it terminates its own job, providing a 1 GiB margin above the desired 25 GiB floor. Disk-space errors, QMP failures and loss of the controller's standard-input channel also stop the guest. A watchdog terminates the job if the supervisor heartbeat becomes more than two seconds old. Guest disk writes are throttled to 16 MiB/s; this is not a cap on every host write, including firmware and logs.

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

All disk images, media, variable firmware files and test outputs belong in a private ordinary directory on C:. The supervisor rejects non-C paths and reparse points. The plan contains only relative paths and placeholders. Resolve those placeholders privately; do not publish concrete host paths or add guest images to the repository.

## Unattended guest setup

[autounattend.xml.template](autounattend.xml.template) and [guest-bootstrap.ps1](guest-bootstrap.ps1) have been reviewed as guest setup artifacts, but have not yet run in a VM. A private builder must substitute the selected image index, bootstrap command and a guest-only administrator password. The generated answer XML and optional answer ISO contain that password in plaintext. Read-only attachment does not encrypt them: keep both private, restrict access, and never commit, publish or include them in logs. Guest setup may retain copies inside the disposable disk. Disabling automatic logon does not certify that every credential copy has been erased.

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
- [ ] Private C: paths and resource headroom checked.
- [x] Supervisor and fixed launch configuration implemented; 16 fake-API tests passed.
- [x] Native job termination and kill-on-close checked with two owned helper children.
- [ ] VM storage-growth behavior and guest boot verified.
- [ ] Loopback ports and client redirection settings checked.
- [ ] Activation and updates network arrangement agreed.
- [ ] Private answer ISO, credential handling and guest-disk-only wipe reviewed for this deployment.
- [ ] Guest installed, required guest reboots completed, and `rdsh-configured` evidence reviewed.
- [ ] Provider readiness verified separately from role installation and service status.
- [ ] Real session lifecycle experiment executed and evidence reviewed.

Only the checked component-level validations have passed. VM installation and the real session lifecycle remain untested; GitHub compilation and unit tests do not complete those experiments.
