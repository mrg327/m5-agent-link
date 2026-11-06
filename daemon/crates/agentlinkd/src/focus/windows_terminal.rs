//! Windows Terminal: find the top-level window whose process is the WindowsTerminal.exe
//! that is an ancestor of the agent, restore it and force it to the foreground.
//!
//! Limits: Windows Terminal exposes no public API to map a process to a tab, so the right
//! *window* is raised but the tab is not selected. When several windows belong to one
//! WindowsTerminal.exe process (tab drag-out / `wt -w`), the first one found is used.

use super::{FocusError, HostCtx};
use std::collections::HashMap;
use windows::core::BOOL;
use windows::Win32::Foundation::{CloseHandle, HWND, LPARAM};
use windows::Win32::System::Diagnostics::ToolHelp::{
    CreateToolhelp32Snapshot, Process32FirstW, Process32NextW, PROCESSENTRY32W, TH32CS_SNAPPROCESS,
};
use windows::Win32::System::Threading::{AttachThreadInput, GetCurrentThreadId};
use windows::Win32::UI::Input::KeyboardAndMouse::{
    keybd_event, KEYBD_EVENT_FLAGS, KEYEVENTF_KEYUP, VK_MENU,
};
use windows::Win32::UI::WindowsAndMessaging::{
    BringWindowToTop, EnumWindows, GetForegroundWindow, GetWindowThreadProcessId, IsIconic,
    IsWindowVisible, SetForegroundWindow, ShowWindow, SW_RESTORE,
};

struct Proc {
    ppid: u32,
    exe: String,
}

fn snapshot() -> Result<HashMap<u32, Proc>, FocusError> {
    let mut map = HashMap::new();
    // SAFETY: plain Toolhelp enumeration with a correctly sized PROCESSENTRY32W.
    unsafe {
        let snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
            .map_err(|e| FocusError::Internal(format!("process snapshot: {e}")))?;
        let mut e = PROCESSENTRY32W {
            dwSize: std::mem::size_of::<PROCESSENTRY32W>() as u32,
            ..Default::default()
        };
        let mut ok = Process32FirstW(snap, &mut e).is_ok();
        while ok {
            let len = e.szExeFile.iter().position(|&c| c == 0).unwrap_or(0);
            map.insert(
                e.th32ProcessID,
                Proc {
                    ppid: e.th32ParentProcessID,
                    exe: String::from_utf16_lossy(&e.szExeFile[..len]).to_ascii_lowercase(),
                },
            );
            ok = Process32NextW(snap, &mut e).is_ok();
        }
        let _ = CloseHandle(snap);
    }
    Ok(map)
}

/// First ancestor (including the start itself) that is WindowsTerminal.exe.
fn terminal_pid(procs: &HashMap<u32, Proc>, start: u32) -> Option<u32> {
    let mut pid = start;
    for _ in 0..32 {
        let p = procs.get(&pid)?;
        if p.exe == "windowsterminal.exe" {
            return Some(pid);
        }
        pid = p.ppid;
    }
    None
}

struct Search {
    pid: u32,
    found: Option<HWND>,
}

unsafe extern "system" fn visit(hwnd: HWND, lp: LPARAM) -> BOOL {
    // SAFETY: lp is the &mut Search passed by `find_window`, alive for the whole call.
    let s = &mut *(lp.0 as *mut Search);
    let mut pid = 0u32;
    GetWindowThreadProcessId(hwnd, Some(&mut pid));
    if pid == s.pid && IsWindowVisible(hwnd).as_bool() {
        s.found = Some(hwnd);
        return BOOL(0);
    }
    BOOL(1)
}

fn find_window(pid: u32) -> Option<HWND> {
    let mut s = Search { pid, found: None };
    // SAFETY: the callback only dereferences the pointer we pass here.
    unsafe {
        let _ = EnumWindows(Some(visit), LPARAM(&mut s as *mut Search as isize));
    }
    s.found
}

fn raise(hwnd: HWND) {
    // SAFETY: Win32 window calls on a handle we just enumerated; failures are tolerated.
    unsafe {
        if IsIconic(hwnd).as_bool() {
            let _ = ShowWindow(hwnd, SW_RESTORE);
        }
        // A synthetic ALT press lifts the foreground lock; attaching to the foreground
        // thread's input queue is the second half of the usual workaround.
        keybd_event(VK_MENU.0 as u8, 0, KEYBD_EVENT_FLAGS(0), 0);
        let fg = GetForegroundWindow();
        let fg_thread = GetWindowThreadProcessId(fg, None);
        let me = GetCurrentThreadId();
        let attached =
            fg_thread != 0 && fg_thread != me && AttachThreadInput(me, fg_thread, true).as_bool();
        let _ = BringWindowToTop(hwnd);
        let _ = SetForegroundWindow(hwnd);
        if attached {
            let _ = AttachThreadInput(me, fg_thread, false);
        }
        keybd_event(VK_MENU.0 as u8, 0, KEYEVENTF_KEYUP, 0);
    }
}

pub fn focus(ctx: &HostCtx) -> Result<(), FocusError> {
    let procs = snapshot()?;
    let wt = ctx
        .pids
        .iter()
        .find_map(|&p| terminal_pid(&procs, p))
        .ok_or(FocusError::NoWindow)?;
    let hwnd = find_window(wt).ok_or(FocusError::NoWindow)?;
    raise(hwnd);
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn walks_up_to_windows_terminal() {
        let p = |ppid, exe: &str| Proc {
            ppid,
            exe: exe.into(),
        };
        let procs = HashMap::from([
            (1, p(0, "explorer.exe")),
            (2, p(1, "windowsterminal.exe")),
            (3, p(2, "openconsole.exe")),
            (4, p(3, "pwsh.exe")),
            (9, p(1, "cmd.exe")),
        ]);
        assert_eq!(terminal_pid(&procs, 4), Some(2));
        assert_eq!(terminal_pid(&procs, 9), None);
    }
}
