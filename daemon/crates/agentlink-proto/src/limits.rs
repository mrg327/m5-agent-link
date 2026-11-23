//! How much of each text field a device holds, in UTF-8 bytes.
//!
//! One table for the daemon: `protocol/agentlink.cddl` states the same numbers on its keys, and the
//! watch's buffers (`model.h`, `link_rx.c`) are each at least one byte (the NUL) larger, which a
//! daemon test checks. A sender cuts a longer string to its limit, on a character boundary, and ends
//! it with `…` so a cut text never reads as whole.

/// `session.repo`.
pub const REPO: usize = 32;
/// `session.br`.
pub const BRANCH: usize = 32;
/// `session.tool`.
pub const TOOL: usize = 24;
/// `session.sum`.
pub const SUMMARY: usize = 80;
/// `session.hn`, a remote host's name.
pub const HOST_NAME: usize = 16;
/// `session.q`, the agent's closing question.
pub const QUESTION: usize = 120;
/// Each of `session.rp` and `welcome.replies`.
pub const REPLY: usize = 24;
/// `session.mdl`.
pub const MODEL: usize = 16;
/// `session.qd`, the queued message's start.
pub const QUEUED: usize = 48;
/// `session.lp`, the last message's start.
pub const LAST_PREVIEW: usize = 160;
/// `attention.title`.
pub const TITLE: usize = 40;
/// `attention.text`, the preview.
pub const TEXT: usize = 160;
/// Each of `attention.opts`.
pub const OPTION: usize = 24;
/// `ask_q.h`.
pub const ASK_HEADER: usize = 16;
/// `ask_q.q`.
pub const ASK_QUESTION: usize = 160;
/// `ask_opt.l`.
pub const ASK_LABEL: usize = 48;
/// `ask_opt.d`.
pub const ASK_DESC: usize = 120;
/// `host_info.name` in `welcome`.
pub const HOST: usize = 32;
/// `device_cfg.badge_handle`.
pub const HANDLE: usize = 24;
/// `result.msg`.
pub const RESULT_MSG: usize = 47;
/// `scroll_pos.preview`.
pub const SCROLL_PREVIEW: usize = 48;
