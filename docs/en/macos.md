# macOS: the companion app

*[Italiano](../it/macos.md)*

macOS has no driver for Windows Precision Touchpads, so without help the trackpad works only as a
plain mouse (pointer and click, no gestures). The
[macos-trackpad-companion](https://github.com/mathias-sm/macos-trackpad-companion) app reads the
raw touches and turns them into native macOS events: pointer, smooth scrolling with momentum,
pinch, rotate, desktop swipes and Mission Control that follow the fingers 1:1.

This project ships a **patch** for version 0.9.3 (commit `1c1bc17`) in
[`macos-companion/`](../../macos-companion). It adds:

- **Apple's acceleration curves.** Pointer and scroll use the parametric curves of a MacBook's
  built-in trackpad (`HIDAccelCurves` / `HIDScrollAccelCurves`, IOHIDFamily algorithm). The curve is
  picked by *System Settings → Trackpad → Tracking speed*, as for the built-in trackpad. The
  pointer acceleration is applied to the total speed, not per axis.
- **No lost first millimetre.** When tap-to-click is off, the pointer starts moving at once;
  originally the first 1 mm / 150 ms of every movement was dropped.
- **Tap to click follows the macOS setting.** If tap to click is on, taps shorter than 40 ms are
  ignored (edge brushes, bounces).
- **Two-finger physical click = right click**, as on Apple trackpads.
- **Sub-threshold motion accumulates** instead of being discarded, so very slow, precise movements
  work.
- **Adaptive anti-jitter buffer for Bluetooth**: at most 20 ms, based on the 90th percentile of the
  delivery delay.
- **macOS 27 Dock swipes**: desktop swipes and Mission Control through an attached `IOHIDEvent`,
  as macOS 27 requires.

## Build and install

You need the Xcode command line tools and Rust (`rustup`).

```
git clone https://github.com/mathias-sm/macos-trackpad-companion.git
cd macos-trackpad-companion
git checkout 1c1bc17
git apply /path/to/this/repo/macos-companion/macos-trackpad-companion.patch
INSTALL=1 INSTALL_DIR=/Applications ./scripts/bundle.sh
open "/Applications/Trackpad Companion.app"
```

Or use [`macos-companion/build.sh`](../../macos-companion/build.sh), which does the same and
signs with a stable certificate (below).

On first launch grant **Input Monitoring** and **Accessibility**, then quit and reopen the app
(Input Monitoring takes effect only after a restart).

### Keep the permissions across rebuilds

An ad-hoc signed app looks like a new app to macOS after every rebuild, so the permissions must
be granted again each time. A personal code-signing certificate avoids that:

1. Open **Keychain Access** → menu *Keychain Access → Certificate Assistant → Create a
   Certificate…*
2. Name `Trackpad Companion Dev`, identity type **Self-Signed Root**, certificate type **Code
   Signing**. Create.
3. Double-click it → **Trust** → *Code Signing*: **Always Trust**.
4. Build with `SIGN_IDENTITY="Trackpad Companion Dev"` (`build.sh` does it).

If permissions get stuck ("Denied" although the switch is on), reset them and grant again:

```
tccutil reset ListenEvent net.guemez.trackpad-companion
tccutil reset Accessibility net.guemez.trackpad-companion
```

## Tuning

- **Pointer speed**: *System Settings → Trackpad → Tracking speed* chooses Apple's curve. The
  app's own *Cursor → Sensitivity* is a multiplier on top: **25 = identical to a MacBook**.
- **Scroll speed**: the app's *Scroll → Sensitivity* multiplies Apple's scroll curve: **20 = as
  Apple**.
- **Tap to click** follows *System Settings → Trackpad*.

## Bluetooth smoothness on a Mac

On a MacBook, Wi-Fi and Bluetooth share one radio and antenna. If the Mac's Wi-Fi is on
**2.4 GHz**, Bluetooth gets fewer time slots and touches arrive in bursts. **Connect the Mac to a
5 GHz network.** AWDL (AirDrop, AirPlay, Handoff) also steals radio time: `sudo ifconfig awdl0 down`
is a quick test, reverted at reboot or by using AirDrop. Over BLE the Mac grants a 7.5 ms connection
interval. The cable is always the smoothest option.

## Known limitations

- "Shake mouse pointer to locate" does not trigger: the pointer is moved with synthetic events.
- Without the app (for example at the login window) the trackpad works as a plain mouse.
