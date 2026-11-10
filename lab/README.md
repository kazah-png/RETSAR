# Proposed Windows Server laboratory

This is a reviewable plan for testing the C++ RDS provider in a **real Windows Server guest**. It is not a launcher, a prepared virtual machine, or evidence that session creation works. No ISO has been obtained and no guest has been provisioned by this plan.

The machine-readable proposal is [plan.json](plan.json). Resource use and the applicable evaluation license terms require approval before provisioning. The launcher and storage guard described below are not implemented.

## Scope of the experiment

The proposed guest is Windows Server 2022 Standard Evaluation with **Desktop Experience** and the **RD Session Host** role. Role installation, registration of the test provider and any required reboots would occur inside the disposable guest.

A successful experiment would need to demonstrate a newly created RDS session, independently verify its identity and ownership, launch the probe in that exact session, and remove only the resources created by the test. An existing reference session must remain usable throughout the experiment. Return codes, mocks and device creation alone are insufficient evidence.

This experiment does **not** establish physical multiseat support on Windows 11 Home, ownership of physical monitors or input devices, hardware graphics acceleration, or compatibility with a particular game. Those require separate evidence.

## Proposed resource budget

| Resource | Proposed allocation |
| --- | --- |
| Guest memory | 4 GiB |
| Virtual CPUs | 2 |
| Accelerator | QEMU TCG software emulation |
| Host process priority | BelowNormal |
| Guest disk | Sparse qcow2, 64 GiB virtual capacity, no preallocation |
| Required free-space floor on C: | 25 GiB |

TCG is the baseline and fallback: the plan does not assume that WHPX or Hyper-V is usable and does not enable either feature. TCG is supported on Windows, but boot time, responsiveness and CPU consumption have not been measured for this guest. BelowNormal priority and two virtual CPUs do not constitute a hard CPU-use ceiling. [QEMU accelerator documentation](https://www.qemu.org/docs/master/system/introduction.html).

The initial machine proposal uses q35, an emulated NVMe disk, UEFI firmware and emulated VGA. The exact QEMU build, its dependencies, firmware files and guest boot compatibility must be verified before provisioning. An existing QEMU executable is not sufficient proof that these prerequisites are satisfied.

### Sparse disk growth is not yet controlled

**64 GiB is the guest-visible capacity, not an initial 64 GiB allocation.** The disk can grow as the guest writes. Installation media, firmware variables, logs and any snapshots also consume host storage.

A future launcher must refuse to start until a tested guard can check space before provisioning and starting, account for all laboratory files, monitor growth, and stop guest writes before free space falls below 25 GiB. If monitoring or stopping writes cannot be confirmed, it must fail closed. A startup check or periodic polling alone must not be described as an absolute guarantee: unrelated host applications can also consume disk space.

**That guard is pending. The current plan does not enforce the floor and is not ready to launch.** No snapshots or additional disk copies are budgeted by default.

## Host separation and access

The proposed guest runs headless, without a local QEMU display window. Access is limited to:

- VNC listening on `127.0.0.1:5905`.
- RDP forwarded from `127.0.0.1:13389` to guest TCP port `3389`.

Both ports must be checked before launch; an occupied port must stop provisioning rather than redirect access to a different process. Networking uses QEMU's user-mode backend with restricted networking. This backend does not require an administrator-owned network adapter, and explicit forwarding can be bound to loopback. [QEMU networking documentation](https://www.qemu.org/docs/master/system/invocation.html).

The baseline provides no guest outbound Internet access. It creates no bridge or TAP adapter, installs no host drivers, changes no host services or hypervisor features, and does not restart the host. Only the guest may restart.

No physical disks or host devices are attached. There are no shared folders, clipboard integration or guest-agent integration. RDP and VNC clients must have clipboard transfer disabled; RDP drive and device redirection must also be disabled. Guest credentials must not be placed in command lines, logs, this plan or the repository.

All disk images, media, variable firmware files and test outputs belong in a private ordinary directory on C:. The plan contains only relative paths and placeholders. Resolve those placeholders privately; do not publish concrete host paths or add guest images to the repository.

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
- [ ] Disk growth guard implemented and validated, including loss of monitoring.
- [ ] Loopback ports and client redirection settings checked.
- [ ] Activation and updates network arrangement agreed.
- [ ] Effectful launcher reviewed separately.
- [ ] Guest installed, required guest reboots completed, and RDSH readiness verified.
- [ ] Real session lifecycle experiment executed and evidence reviewed.

Until these conditions are met, results remain **planned**, not passed. GitHub compilation and unit tests do not complete this checklist.
