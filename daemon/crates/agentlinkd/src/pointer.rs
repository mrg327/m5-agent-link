//! The watch as a trackpad (proto 0.20, EXPERIMENTS.md): `cmd pointer` moves, clicks and scrolls the
//! pointer of the machine this daemon runs on. macOS posts CoreGraphics events (needs the Accessibility
//! permission), Windows uses `SendInput`, Linux on X11 runs `xdotool`. Elsewhere it is unsupported.

use agentlink_proto::{PointerArg, PointerBtn};
use std::sync::Mutex;
use std::time::{Duration, Instant};

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum PointerError {
    /// No backend on this machine (Wayland without X11, no xdotool, …).
    Unsupported,
    /// The OS refused the events; the message says what to allow. Only the macOS and Windows backends
    /// construct it (xdotool reports failures as Internal); tests construct it on every platform.
    #[cfg_attr(all(unix, not(target_os = "macos"), not(test)), allow(dead_code))]
    Denied(String),
    Internal(String),
}

pub trait Pointer: Send + Sync {
    /// Whether the welcome offers the trackpad. A permission still missing shows on first use.
    fn available(&self) -> bool;
    /// Move by (`dx`, `dy`), then click, then scroll.
    fn apply(&self, a: &PointerArg) -> Result<(), PointerError>;
}

/// The highest click count (`n`: 2 = the second click of a double click, 3 a triple); wheel notches and
/// pixels per command at most. Every command presses once: the watch sends each tap as it lands.
const MAX_CLICKS: u64 = 3;
const MAX_WHEEL: i64 = 20;
const MAX_MOVE: i64 = 2000;

/// What a command asks for, clamped: (dx, dy, click, wheel).
pub fn parts(a: &PointerArg) -> (i64, i64, Option<(PointerBtn, u64)>, i64) {
    let c = |v: Option<i64>, m: i64| v.unwrap_or(0).clamp(-m, m);
    let click = a.btn.map(|b| (b, a.n.unwrap_or(1).clamp(1, MAX_CLICKS)));
    (
        c(a.dx, MAX_MOVE),
        c(a.dy, MAX_MOVE),
        click,
        c(a.wheel, MAX_WHEEL),
    )
}

/// Whether a command only moves the pointer: those are not traced one by one.
pub fn is_move_only(a: &PointerArg) -> bool {
    a.btn.is_none() && a.wheel.unwrap_or(0) == 0
}

/// Failures answer the device at most once per [`ERR_GAP`]; a trackpad stream would flood it otherwise.
pub const ERR_GAP: Duration = Duration::from_secs(1);

#[derive(Default)]
pub struct ErrGate(Mutex<Option<Instant>>);

impl ErrGate {
    pub fn pass(&self, now: Instant) -> bool {
        let mut last = self.0.lock().unwrap();
        if last.is_some_and(|t| now.duration_since(t) < ERR_GAP) {
            return false;
        }
        *last = Some(now);
        true
    }
}

pub fn system() -> std::sync::Arc<dyn Pointer> {
    #[cfg(target_os = "macos")]
    return std::sync::Arc::new(mac::Mac::default());
    #[cfg(windows)]
    return std::sync::Arc::new(win::Win);
    #[cfg(all(unix, not(target_os = "macos")))]
    return std::sync::Arc::new(x11::X11);
    #[allow(unreachable_code)]
    std::sync::Arc::new(Unsupported)
}

pub struct Unsupported;

impl Pointer for Unsupported {
    fn available(&self) -> bool {
        false
    }
    fn apply(&self, _: &PointerArg) -> Result<(), PointerError> {
        Err(PointerError::Unsupported)
    }
}

#[cfg(target_os = "macos")]
mod mac {
    use super::*;
    use std::ffi::c_void;
    use std::sync::atomic::{AtomicBool, Ordering};

    #[repr(C)]
    #[derive(Clone, Copy)]
    struct CGPoint {
        x: f64,
        y: f64,
    }
    #[repr(C)]
    #[derive(Clone, Copy)]
    struct CGSize {
        width: f64,
        height: f64,
    }
    #[repr(C)]
    #[derive(Clone, Copy)]
    struct CGRect {
        origin: CGPoint,
        size: CGSize,
    }
    type Ev = *mut c_void;

    #[link(name = "CoreGraphics", kind = "framework")]
    extern "C" {
        fn CGEventCreate(source: *const c_void) -> Ev;
        fn CGEventGetLocation(e: Ev) -> CGPoint;
        fn CGEventCreateMouseEvent(src: *const c_void, t: u32, at: CGPoint, button: u32) -> Ev;
        fn CGEventSetIntegerValueField(e: Ev, field: u32, v: i64);
        fn CGEventCreateScrollWheelEvent(
            src: *const c_void,
            units: u32,
            count: u32,
            w1: i32,
            ...
        ) -> Ev;
        fn CGEventPost(tap: u32, e: Ev);
        fn CGGetActiveDisplayList(max: u32, displays: *mut u32, count: *mut u32) -> i32;
        fn CGDisplayBounds(display: u32) -> CGRect;
        fn CGPreflightPostEventAccess() -> bool;
        fn CGRequestPostEventAccess() -> bool;
    }
    #[link(name = "CoreFoundation", kind = "framework")]
    extern "C" {
        fn CFRelease(p: *const c_void);
    }

    const MOVED: u32 = 5;
    const HID_TAP: u32 = 0;
    const CLICK_STATE: u32 = 1; // kCGMouseEventClickState
    const UNIT_LINE: u32 = 1;

    #[derive(Default)]
    pub struct Mac {
        granted: AtomicBool,
        asked: AtomicBool,
    }

    fn post(e: Ev) -> Result<(), PointerError> {
        if e.is_null() {
            return Err(PointerError::Internal("CGEvent create failed".into()));
        }
        // SAFETY: `e` is a CGEvent we created and own; CFRelease balances the create.
        unsafe {
            CGEventPost(HID_TAP, e);
            CFRelease(e);
        }
        Ok(())
    }

    fn location() -> CGPoint {
        // SAFETY: an event created with no source reports the current pointer location.
        unsafe {
            let e = CGEventCreate(std::ptr::null());
            if e.is_null() {
                return CGPoint { x: 0.0, y: 0.0 };
            }
            let p = CGEventGetLocation(e);
            CFRelease(e);
            p
        }
    }

    /// The pointer stays within the bounding box of the active displays.
    fn clamp(p: CGPoint) -> CGPoint {
        let mut ids = [0u32; 16];
        let mut n = 0u32;
        // SAFETY: the buffer holds 16 ids and CoreGraphics writes at most `max` of them.
        if unsafe { CGGetActiveDisplayList(16, ids.as_mut_ptr(), &mut n) } != 0 || n == 0 {
            return p;
        }
        let (mut x0, mut y0, mut x1, mut y1) = (f64::MAX, f64::MAX, f64::MIN, f64::MIN);
        for &d in &ids[..n as usize] {
            // SAFETY: `d` is an active display id from the list above.
            let r = unsafe { CGDisplayBounds(d) };
            x0 = x0.min(r.origin.x);
            y0 = y0.min(r.origin.y);
            x1 = x1.max(r.origin.x + r.size.width - 1.0);
            y1 = y1.max(r.origin.y + r.size.height - 1.0);
        }
        CGPoint {
            x: p.x.clamp(x0, x1),
            y: p.y.clamp(y0, y1),
        }
    }

    impl Mac {
        fn allowed(&self) -> Result<(), PointerError> {
            if self.granted.load(Ordering::Relaxed) {
                return Ok(());
            }
            // SAFETY: plain queries of the TCC state; the request shows the system prompt once.
            if unsafe { CGPreflightPostEventAccess() } {
                self.granted.store(true, Ordering::Relaxed);
                return Ok(());
            }
            if !self.asked.swap(true, Ordering::Relaxed) && unsafe { CGRequestPostEventAccess() } {
                self.granted.store(true, Ordering::Relaxed);
                return Ok(());
            }
            Err(PointerError::Denied(
                "Allow agentlinkd in Privacy & Security > Accessibility".into(),
            ))
        }
    }

    impl Pointer for Mac {
        fn available(&self) -> bool {
            true
        }

        fn apply(&self, a: &PointerArg) -> Result<(), PointerError> {
            self.allowed()?;
            let (dx, dy, click, wheel) = parts(a);
            let mut at = location();
            if dx != 0 || dy != 0 {
                at = clamp(CGPoint {
                    x: at.x + dx as f64,
                    y: at.y + dy as f64,
                });
                // SAFETY: creates an owned event, posted and released by `post`.
                post(unsafe { CGEventCreateMouseEvent(std::ptr::null(), MOVED, at, 0) })?;
            }
            if let Some((btn, n)) = click {
                let (down, up, b) = match btn {
                    PointerBtn::Left => (1, 2, 0),
                    PointerBtn::Right => (3, 4, 1),
                };
                for t in [down, up] {
                    // SAFETY: as above. Posted events get no click count from the OS, so the watch's
                    // count goes into the click state: 2 makes this press the second of a double click.
                    unsafe {
                        let e = CGEventCreateMouseEvent(std::ptr::null(), t, at, b);
                        if !e.is_null() {
                            CGEventSetIntegerValueField(e, CLICK_STATE, n as i64);
                        }
                        post(e)?;
                    }
                }
            }
            if wheel != 0 {
                // CoreGraphics: positive scrolls towards the top; ours is positive = down.
                // SAFETY: one wheel axis, so one variadic argument.
                post(unsafe {
                    CGEventCreateScrollWheelEvent(std::ptr::null(), UNIT_LINE, 1, -wheel as i32)
                })?;
            }
            Ok(())
        }
    }
}

#[cfg(windows)]
mod win {
    use super::*;
    use windows::Win32::UI::Input::KeyboardAndMouse::{
        SendInput, INPUT, INPUT_0, INPUT_MOUSE, MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP,
        MOUSEEVENTF_MOVE, MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP, MOUSEEVENTF_WHEEL,
        MOUSEINPUT, MOUSE_EVENT_FLAGS,
    };

    pub struct Win;

    fn mouse(dx: i32, dy: i32, data: i32, flags: MOUSE_EVENT_FLAGS) -> INPUT {
        INPUT {
            r#type: INPUT_MOUSE,
            Anonymous: INPUT_0 {
                mi: MOUSEINPUT {
                    dx,
                    dy,
                    mouseData: data as u32,
                    dwFlags: flags,
                    time: 0,
                    dwExtraInfo: 0,
                },
            },
        }
    }

    impl Pointer for Win {
        fn available(&self) -> bool {
            true
        }

        fn apply(&self, a: &PointerArg) -> Result<(), PointerError> {
            let (dx, dy, click, wheel) = parts(a);
            let mut ins = Vec::new();
            if dx != 0 || dy != 0 {
                ins.push(mouse(dx as i32, dy as i32, 0, MOUSEEVENTF_MOVE));
            }
            // One press; Windows counts a double click itself from the timing of two.
            if let Some((btn, _)) = click {
                let (down, up) = match btn {
                    PointerBtn::Left => (MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP),
                    PointerBtn::Right => (MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP),
                };
                ins.push(mouse(0, 0, 0, down));
                ins.push(mouse(0, 0, 0, up));
            }
            if wheel != 0 {
                // WHEEL_DELTA (120) per notch; positive rotates away from the user (up).
                ins.push(mouse(0, 0, -(wheel as i32) * 120, MOUSEEVENTF_WHEEL));
            }
            if ins.is_empty() {
                return Ok(());
            }
            // SAFETY: a slice of fully initialised mouse inputs and their size.
            let sent = unsafe { SendInput(&ins, std::mem::size_of::<INPUT>() as i32) };
            if sent as usize == ins.len() {
                Ok(())
            } else {
                // blocked by UIPI (an elevated window has the focus) or the secure desktop
                Err(PointerError::Denied("Windows blocked the input".into()))
            }
        }
    }
}

#[cfg(all(unix, not(target_os = "macos")))]
mod x11 {
    use super::*;
    use crate::focus::{run, FocusError};

    /// X11 through `xdotool`. Under Wayland only XWayland windows would see it, so it is offered only
    /// with a `DISPLAY` and no `WAYLAND_DISPLAY`.
    pub struct X11;

    fn xdotool(args: &[String]) -> Result<(), PointerError> {
        run("xdotool", args).map(|_| ()).map_err(|e| match e {
            FocusError::Unsupported => PointerError::Unsupported,
            e => PointerError::Internal(format!("{e:?}")),
        })
    }

    impl Pointer for X11 {
        fn available(&self) -> bool {
            std::env::var_os("DISPLAY").is_some()
                && std::env::var_os("WAYLAND_DISPLAY").is_none()
                && xdotool(&["version".into()]).is_ok()
        }

        fn apply(&self, a: &PointerArg) -> Result<(), PointerError> {
            let (dx, dy, click, wheel) = parts(a);
            let s = |v: &str| v.to_owned();
            if dx != 0 || dy != 0 {
                xdotool(&[
                    s("mousemove_relative"),
                    s("--"),
                    dx.to_string(),
                    dy.to_string(),
                ])?;
            }
            // One press; X11 clients count a double click themselves from the timing of two.
            if let Some((btn, _)) = click {
                let b = match btn {
                    PointerBtn::Left => "1",
                    PointerBtn::Right => "3",
                };
                xdotool(&[s("click"), s(b)])?;
            }
            if wheel != 0 {
                let b = if wheel > 0 { "5" } else { "4" };
                xdotool(&[s("click"), s("--repeat"), wheel.abs().to_string(), s(b)])?;
            }
            Ok(())
        }
    }
}

#[cfg(test)]
pub mod fake {
    use super::*;

    /// Records every command; fails with `fail` when set.
    #[derive(Default)]
    pub struct Fake {
        pub got: Mutex<Vec<PointerArg>>,
        pub fail: Mutex<Option<PointerError>>,
    }

    impl Pointer for Fake {
        fn available(&self) -> bool {
            true
        }
        fn apply(&self, a: &PointerArg) -> Result<(), PointerError> {
            if let Some(e) = self.fail.lock().unwrap().clone() {
                return Err(e);
            }
            self.got.lock().unwrap().push(a.clone());
            Ok(())
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn arg(dx: i64, btn: Option<PointerBtn>, n: Option<u64>, wheel: i64) -> PointerArg {
        PointerArg {
            dx: Some(dx),
            dy: None,
            btn,
            n,
            wheel: Some(wheel),
        }
    }

    #[test]
    fn parts_clamp_what_one_command_may_do() {
        let (dx, dy, click, wheel) = parts(&arg(99_999, Some(PointerBtn::Left), Some(9), -99));
        assert_eq!((dx, dy, wheel), (MAX_MOVE, 0, -MAX_WHEEL));
        assert_eq!(click, Some((PointerBtn::Left, MAX_CLICKS)));
        let (_, _, click, _) = parts(&arg(0, Some(PointerBtn::Right), None, 0));
        assert_eq!(
            click,
            Some((PointerBtn::Right, 1)),
            "a click without n is one click"
        );
    }

    #[test]
    fn only_pure_moves_skip_the_trace() {
        assert!(is_move_only(&arg(5, None, None, 0)));
        assert!(!is_move_only(&arg(5, Some(PointerBtn::Left), None, 0)));
        assert!(!is_move_only(&arg(0, None, None, 1)));
    }

    #[test]
    fn failures_answer_at_most_once_a_second() {
        let g = ErrGate::default();
        let t = Instant::now();
        assert!(g.pass(t));
        assert!(!g.pass(t + Duration::from_millis(400)));
        assert!(g.pass(t + ERR_GAP));
    }
}
